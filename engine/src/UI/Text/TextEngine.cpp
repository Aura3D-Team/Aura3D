#include "aura/UI/Text/TextEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <ink/Inkogger.h>

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/UI/Text/Utf8.h"

namespace aura3d::ui {

namespace {

/// Rasterization sizes are snapped to whole device pixels: a font hinted at
/// 15.6px is rasterized at 16 and scaled by 0.975 in layout, which is
/// invisible, where a page per fractional size would thrash the cache during
/// any animation that touches a font size.
[[nodiscard]] u32 snapToDevicePixels(f32 logicalSize, f32 scale) noexcept
{
    const f32 device = std::round(logicalSize * scale);
    return static_cast<u32>(std::clamp(device, 4.0f, 256.0f));
}

} // namespace

// -----------------------------------------------------------------------------
// Utf8
// -----------------------------------------------------------------------------

namespace utf8 {

void append(std::string& out, char32_t codepoint)
{
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
        return;

    const auto emit = [&out](u32 byte) { out.push_back(static_cast<char>(byte)); };

    if (codepoint < 0x80u)
    {
        emit(codepoint);
    }
    else if (codepoint < 0x800u)
    {
        emit(0xC0u | (codepoint >> 6));
        emit(0x80u | (codepoint & 0x3Fu));
    }
    else if (codepoint < 0x10000u)
    {
        emit(0xE0u | (codepoint >> 12));
        emit(0x80u | ((codepoint >> 6) & 0x3Fu));
        emit(0x80u | (codepoint & 0x3Fu));
    }
    else
    {
        emit(0xF0u | (codepoint >> 18));
        emit(0x80u | ((codepoint >> 12) & 0x3Fu));
        emit(0x80u | ((codepoint >> 6) & 0x3Fu));
        emit(0x80u | (codepoint & 0x3Fu));
    }
}

} // namespace utf8

// -----------------------------------------------------------------------------
// ShapedText
// -----------------------------------------------------------------------------

void ShapedText::clear() noexcept
{
    glyphs.clear();
    lines.clear();
    size = {0.0f, 0.0f};
}

u32 ShapedText::lineOf(usize byte) const noexcept
{
    if (lines.empty())
        return 0;

    for (u32 i = 0; i < lines.size(); ++i)
    {
        if (byte <= lines[i].byteEnd)
            return i;
    }

    return static_cast<u32>(lines.size() - 1);
}

usize ShapedText::byteAt(glm::vec2 point) const noexcept
{
    if (lines.empty())
        return 0;

    //! Vertical first: a click below the last line belongs at its end, not at
    //! the start of nothing.
    u32 lineIndex = static_cast<u32>(lines.size() - 1);
    for (u32 i = 0; i < lines.size(); ++i)
    {
        if (point.y < lines[i].top + lineHeight)
        {
            lineIndex = i;
            break;
        }
    }

    const ShapedLine& line = lines[lineIndex];

    for (u32 i = 0; i < line.glyphCount; ++i)
    {
        const ShapedGlyph& glyph = glyphs[line.firstGlyph + i];

        //! The caret snaps to whichever side of the glyph the click is nearer,
        //! which is what makes clicking "the left half of a letter" put the
        //! caret before it.
        if (point.x < glyph.penX + glyph.advance * 0.5f)
            return glyph.cluster;
    }

    return line.byteEnd;
}

glm::vec2 ShapedText::caretPosition(usize byte) const noexcept
{
    if (lines.empty())
        return {0.0f, 0.0f};

    const u32 lineIndex = lineOf(byte);
    const ShapedLine& line = lines[lineIndex];

    for (u32 i = 0; i < line.glyphCount; ++i)
    {
        const ShapedGlyph& glyph = glyphs[line.firstGlyph + i];
        if (glyph.cluster >= byte)
            return {glyph.penX, line.top};
    }

    //! Past the last glyph: the end of the line, which is where a caret at the
    //! end of the string belongs.
    const f32 x = line.glyphCount > 0
                      ? glyphs[line.firstGlyph + line.glyphCount - 1].penX +
                            glyphs[line.firstGlyph + line.glyphCount - 1].advance
                      : lineStartX(lineIndex);

    return {x, line.top};
}

Rect ShapedText::caretRect(usize byte, f32 width) const noexcept
{
    const glm::vec2 position = caretPosition(byte);
    return Rect::fromSize(position, {width, lineHeight});
}

void ShapedText::selectionRects(usize begin, usize end, std::vector<Rect>& out) const
{
    if (begin >= end || lines.empty())
        return;

    for (u32 i = 0; i < lines.size(); ++i)
    {
        const ShapedLine& line = lines[i];
        if (end <= line.byteBegin || begin > line.byteEnd)
            continue;

        const usize from = std::max<usize>(begin, line.byteBegin);
        const usize to = std::min<usize>(end, line.byteEnd);

        const f32 x0 = caretPosition(from).x;
        f32 x1 = caretPosition(to).x;

        //! A line whose break was consumed (a wrap, or a '\n') shows the
        //! selection running past its last glyph, so a multi-line selection
        //! reads as continuous rather than as a ragged stack of words.
        if (end > line.byteEnd)
            x1 = std::max(x1, lineStartX(i) + line.width) + lineHeight * 0.25f;

        if (x1 > x0)
            out.push_back(Rect::fromSize({x0, line.top}, {x1 - x0, lineHeight}));
    }
}

f32 ShapedText::lineStartX(u32 lineIndex) const noexcept
{
    if (lineIndex >= lines.size())
        return 0.0f;

    const ShapedLine& line = lines[lineIndex];
    return line.glyphCount > 0 ? glyphs[line.firstGlyph].penX : 0.0f;
}

// -----------------------------------------------------------------------------
// AtlasTextShaper
// -----------------------------------------------------------------------------

AtlasTextShaper::AtlasTextShaper(const TextShaperDesc& desc) : _desc(desc)
{
    _desc.maxPages = std::max(1u, _desc.maxPages);

    if (_desc.fontPath.empty())
        return;

    /*
     * Read once, copy per page: every page rasterizes the same face at a
     * different size, and stb_truetype keeps pointing at the bytes it was
     * handed, so FontAtlas::fromMemory() has to own its copy.
     */
    if (std::FILE* file = std::fopen(_desc.fontPath.c_str(), "rb"))
    {
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);

        if (size > 0)
        {
            _fontData.resize(static_cast<usize>(size));
            if (std::fread(_fontData.data(), 1, _fontData.size(), file) != _fontData.size())
                _fontData.clear();
        }

        std::fclose(file);
    }

    if (_fontData.empty())
        INK_WARN << "AuraUI: font '" << _desc.fontPath
                 << "' unreadable; using the embedded bitmap font";
}

AtlasTextShaper::~AtlasTextShaper() = default;

void AtlasTextShaper::setScale(f32 scale)
{
    scale = std::clamp(scale, 0.25f, 8.0f);
    if (scale == _scale)
        return;

    _scale = scale;

    //! Every page's rasterization size was derived from the old scale, so they
    //! are all the wrong resolution now. Dropping them rebuilds lazily.
    _pages.clear();
}

u32 AtlasTextShaper::pageCount() const noexcept
{
    return static_cast<u32>(_pages.size());
}

FontAtlas* AtlasTextShaper::page(u32 index) noexcept
{
    return index < _pages.size() ? _pages[index].atlas.get() : nullptr;
}

u32 AtlasTextShaper::_devicePixelsFor(const TextStyle& style) const noexcept
{
    return snapToDevicePixels(style.pixelSize, _scale);
}

AtlasTextShaper::Page* AtlasTextShaper::_pageFor(const TextStyle& style)
{
    const u32 wanted = _devicePixelsFor(style);

    for (Page& page : _pages)
    {
        if (page.devicePixels == wanted)
            return &page;
    }

    if (_pages.size() >= _desc.maxPages)
    {
        /*
         * Cap reached. Serving the request from the nearest existing size
         * scales the glyphs a little, which is far better than the two
         * alternatives: evicting a page still on screen (every label that used
         * it re-rasterizes on the same frame) or refusing to draw.
         */
        Page* nearest = &_pages.front();
        for (Page& page : _pages)
        {
            const u32 delta = page.devicePixels > wanted ? page.devicePixels - wanted
                                                         : wanted - page.devicePixels;
            const u32 best = nearest->devicePixels > wanted ? nearest->devicePixels - wanted
                                                            : wanted - nearest->devicePixels;
            if (delta < best)
                nearest = &page;
        }
        return nearest;
    }

    const FontAtlasDesc desc{.width = _desc.pageSize,
                             .height = _desc.pageSize,
                             .pixelHeight = static_cast<f32>(wanted)};

    Page page{};
    page.devicePixels = wanted;

    if (!_fontData.empty())
    {
        if (auto atlas = FontAtlas::fromMemory(_fontData, desc))
            page.atlas = std::move(*atlas);
    }

    if (!page.atlas)
        page.atlas = FontAtlas::builtinBitmap(desc);

    if (!page.atlas)
        return nullptr;

    //! Placed now, while the page is empty and placement cannot fail: the
    //! backend needs it for every solid rectangle it draws.
    (void)page.atlas->solidTexelUv();

    const f32 unit = 1.0f / static_cast<f32>(wanted);
    page.lineHeight = page.atlas->lineHeight() * unit;
    page.ascent = page.atlas->ascent() * unit;

    _pages.push_back(std::move(page));
    return &_pages.back();
}

f32 AtlasTextShaper::lineHeight(const TextStyle& style)
{
    const Page* page = _pageFor(style);
    return page ? page->lineHeight * style.pixelSize * style.lineSpacing : 0.0f;
}

f32 AtlasTextShaper::ascent(const TextStyle& style)
{
    const Page* page = _pageFor(style);
    if (!page)
        return 0.0f;

    const f32 natural = page->lineHeight * style.pixelSize;
    const f32 leading = (natural * style.lineSpacing - natural) * 0.5f;

    return page->ascent * style.pixelSize + leading;
}

void AtlasTextShaper::shape(std::string_view utf8, const TextStyle& style, f32 maxWidth,
                            ShapedText& out)
{
    out.clear();

    Page* page = _pageFor(style);
    if (!page)
        return;

    FontAtlas& atlas = *page->atlas;

    //! Atlas metrics are in the page's device pixels; this converts them to the
    //! logical pixels layout works in, and absorbs the rounding that snapping
    //! the rasterization size introduced.
    const f32 unit = style.pixelSize / static_cast<f32>(page->devicePixels);

    const f32 natural = atlas.lineHeight() * unit;
    const f32 lineHeight = natural * style.lineSpacing;
    const f32 leading = (lineHeight - natural) * 0.5f;

    out.page = static_cast<u32>(page - _pages.data());
    out.lineHeight = lineHeight;
    out.ascent = atlas.ascent() * unit + leading;

    if (utf8.empty())
    {
        out.size = {0.0f, lineHeight};
        out.lines.push_back(ShapedLine{.byteBegin = 0, .byteEnd = 0, .top = 0.0f,
                                       .baseline = out.ascent});
        return;
    }

    const bool wrapping = style.wrap != TextWrap::None && !isUnbounded(maxWidth);

    f32 lineTop = 0.0f;
    f32 penX = 0.0f;
    char32_t previous = 0;

    ShapedLine line{};
    line.byteBegin = 0;
    line.top = 0.0f;
    line.baseline = out.ascent;
    line.firstGlyph = 0;

    /// Last position a wrap may legally happen at: the glyph index and the pen
    /// x it would start from, plus the width the line has *excluding* the run
    /// of spaces that offered the break.
    u32 breakGlyph = 0;
    f32 breakPenX = 0.0f;
    f32 breakWidth = 0.0f;
    bool haveBreak = false;

    const auto closeLine = [&](u32 glyphEnd, u32 byteEnd, f32 width) {
        line.glyphCount = glyphEnd - line.firstGlyph;
        line.byteEnd = byteEnd;
        line.width = width;
        out.lines.push_back(line);

        lineTop += lineHeight;

        line = ShapedLine{};
        line.firstGlyph = glyphEnd;
        line.byteBegin = byteEnd;
        line.top = lineTop;
        line.baseline = lineTop + out.ascent;

        haveBreak = false;
        previous = 0;
    };

    usize offset = 0;
    while (offset < utf8.size())
    {
        const usize glyphStart = offset;
        const char32_t codepoint = decodeUtf8(utf8, offset);
        if (codepoint == 0)
            break;

        if (codepoint == U'\n')
        {
            closeLine(static_cast<u32>(out.glyphs.size()), static_cast<u32>(glyphStart), penX);
            line.byteBegin = static_cast<u32>(offset);
            penX = 0.0f;
            continue;
        }

        if (codepoint == U'\r')
            continue;

        const GlyphInfo* info = atlas.glyph(codepoint);
        if (!info)
            continue;

        if (previous != 0)
            penX += atlas.kerning(previous, codepoint) * unit;

        const f32 advance = info->advance * unit + style.letterSpacing;

        //! Recorded *before* the glyph is placed: a break takes effect at the
        //! start of the word that follows the space, not after it.
        if (wrapping && style.wrap == TextWrap::Word && utf8::isWordSeparator(utf8[glyphStart]))
        {
            haveBreak = true;
            breakGlyph = static_cast<u32>(out.glyphs.size()) + 1;
            breakPenX = penX + advance;
            breakWidth = penX;
        }

        const bool overflows = wrapping && penX + advance > maxWidth &&
                               out.glyphs.size() > line.firstGlyph;

        if (overflows)
        {
            if (style.wrap == TextWrap::Word && haveBreak && breakGlyph > line.firstGlyph &&
                breakGlyph <= out.glyphs.size())
            {
                /*
                 * The overflowing word already has glyphs on this line. Close
                 * the line at the break and slide that word down: cheaper and
                 * simpler than backtracking the decoder, and exact, because a
                 * horizontal shift is all a re-layout of an already-shaped run
                 * amounts to.
                 */
                const u32 moved = static_cast<u32>(out.glyphs.size()) - breakGlyph;
                const u32 wordByte = moved > 0 ? out.glyphs[breakGlyph].cluster
                                               : static_cast<u32>(glyphStart);

                closeLine(breakGlyph, wordByte, breakWidth);

                for (usize i = breakGlyph; i < out.glyphs.size(); ++i)
                {
                    ShapedGlyph& moving = out.glyphs[i];
                    moving.bounds = moving.bounds.translated({-breakPenX, lineHeight});
                    moving.penX -= breakPenX;
                    moving.line = static_cast<u32>(out.lines.size());
                }

                penX -= breakPenX;
            }
            else
            {
                //! No break opportunity (one long word, or character wrapping):
                //! break right here.
                closeLine(static_cast<u32>(out.glyphs.size()), static_cast<u32>(glyphStart), penX);
                penX = 0.0f;
            }
        }

        const glm::vec2 origin{penX + info->bearing.x * unit,
                               lineTop + leading + info->bearing.y * unit};

        out.glyphs.push_back(ShapedGlyph{
            .bounds = Rect::fromSize(origin, info->size * unit),
            .uvMin = info->uvMin,
            .uvMax = info->uvMax,
            .penX = penX,
            .advance = advance,
            .cluster = static_cast<u32>(glyphStart),
            .line = static_cast<u32>(out.lines.size()),
        });

        penX += advance;
        previous = codepoint;
    }

    closeLine(static_cast<u32>(out.glyphs.size()), static_cast<u32>(utf8.size()), penX);

    f32 blockWidth = 0.0f;
    for (const ShapedLine& shaped : out.lines)
        blockWidth = std::max(blockWidth, shaped.width);

    out.size = {blockWidth, lineTop};

    if (style.align == Align::Left)
        return;

    const f32 factor = style.align == Align::Center ? 0.5f : 1.0f;

    for (ShapedLine& shaped : out.lines)
    {
        const f32 shift = (blockWidth - shaped.width) * factor;
        if (shift <= 0.0f)
            continue;

        for (u32 i = 0; i < shaped.glyphCount; ++i)
        {
            ShapedGlyph& glyph = out.glyphs[shaped.firstGlyph + i];
            glyph.bounds = glyph.bounds.translated({shift, 0.0f});
            glyph.penX += shift;
        }
    }
}

} // namespace aura3d::ui
