#include "aura/Core/AuraFont/FontAtlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

#include "aura/Core/AuraFont/AuraBitmapFont.h"

/*
 * stb_truetype is compiled into this translation unit only. Keeping the
 * implementation here (rather than in the public header) is what lets
 * FontAtlas.h stay free of the vendored dependency: vendor/stb is a PRIVATE
 * include directory of the Aura3D target, so consumers of the engine never
 * need it on their include path.
 */
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace aura3d {

/// Wraps the vendored font handle so the public header can stay opaque.
struct FontAtlas::FontImpl {
    stbtt_fontinfo info{};
};

namespace {

//! Substituted when a codepoint is missing from the font.
constexpr char32_t kReplacementGlyph = U'?';

} // namespace

char32_t decodeUtf8(std::string_view text, size_t& offset) noexcept
{
    if (offset >= text.size())
        return 0;

    const auto byteAt = [&](size_t i) noexcept {
        return static_cast<unsigned char>(text[i]);
    };

    const unsigned char lead = byteAt(offset);

    //! ASCII fast path: one byte, value is the scalar itself.
    if (lead < 0x80u) 
    {
        ++offset;
        return static_cast<char32_t>(lead);
    }

    /*
     * The lead byte encodes the sequence length in its high bits (110x/1110/
     * 11110) and the top bits of the scalar; every continuation byte is 10xxxxxx
     * and carries six more bits. A truncated or mis-tagged sequence yields
     * U+FFFD after consuming exactly one byte, which guarantees progress.
     */
    u32 extraBytes = 0;
    char32_t scalar = 0;

    if ((lead & 0xE0u) == 0xC0u) 
    { 
        extraBytes = 1; 
        scalar = lead & 0x1Fu; 
    }
    else if ((lead & 0xF0u) == 0xE0u) 
    { 
        extraBytes = 2; 
        scalar = lead & 0x0Fu; 
    }
    else if ((lead & 0xF8u) == 0xF0u) 
    { 
        extraBytes = 3; 
        scalar = lead & 0x07u; 
    }
    else 
    { 
        ++offset; 
        return 0xFFFDu;
    }

    if (offset + extraBytes >= text.size())
    {
        ++offset;
        return 0xFFFDu;
    }

    for (u32 i = 1; i <= extraBytes; ++i) 
    {
        const unsigned char continuation = byteAt(offset + i);
        if ((continuation & 0xC0u) != 0x80u) 
        {
            ++offset;
            return 0xFFFDu;
        }
        scalar = (scalar << 6) | (continuation & 0x3Fu);
    }

    offset += extraBytes + 1;
    return scalar;
}

FontAtlas::FontAtlas(const Desc& desc)
    : _desc(desc)
{
    //! One byte per texel: the atlas holds coverage, not colour.
    _coverage.assign(static_cast<size_t>(_desc.width) * _desc.height, 0);
}

FontAtlas::~FontAtlas() = default;
FontAtlas::FontAtlas(FontAtlas&&) noexcept = default;
FontAtlas& FontAtlas::operator=(FontAtlas&&) noexcept = default;

std::expected<std::unique_ptr<FontAtlas>, std::string>
FontAtlas::fromFile(const std::string& path, const Desc& desc)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::unexpected("FontAtlas: cannot open font file '" + path + "'");

    const std::streamsize size = file.tellg();
    if (size <= 0)
        return std::unexpected("FontAtlas: font file '" + path + "' is empty");

    std::vector<u8> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        return std::unexpected("FontAtlas: failed to read font file '" + path + "'");

    return fromMemory(std::move(bytes), desc);
}

std::expected<std::unique_ptr<FontAtlas>, std::string>
FontAtlas::fromMemory(std::vector<u8> fontData, const Desc& desc)
{
    if (fontData.empty())
        return std::unexpected("FontAtlas: empty font image");

    //! Not make_unique: the constructor is private and this is its only caller.
    std::unique_ptr<FontAtlas> atlas(new FontAtlas(desc));
    atlas->_fontData = std::move(fontData);
    atlas->_font = std::make_unique<FontImpl>();

    /*
     * A .ttc collection holds several faces; index 0 is the first. Both .ttf and
     * .otf are single-face containers, so the offset is 0 for them too.
     */
    const int offset = stbtt_GetFontOffsetForIndex(atlas->_fontData.data(), 0);
    if (offset < 0)
        return std::unexpected("FontAtlas: no usable face in the font image");

    if (!stbtt_InitFont(&atlas->_font->info, atlas->_fontData.data(), offset))
        return std::unexpected("FontAtlas: stb_truetype rejected the font image");

    /*
     * Font outlines live in abstract "em" units; scale converts them to the
     * requested pixel height. Vertical metrics are stored once, scaled, so the
     * per-glyph path never repeats the conversion.
     */
    atlas->_scale = stbtt_ScaleForPixelHeight(&atlas->_font->info, desc.pixelHeight);

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&atlas->_font->info, &ascent, &descent, &lineGap);

    atlas->_ascent = static_cast<float>(ascent) * atlas->_scale;
    atlas->_descent = static_cast<float>(descent) * atlas->_scale;
    //! descent is negative (below the baseline), so subtracting adds its depth.
    atlas->_lineHeight = static_cast<float>(ascent - descent + lineGap) * atlas->_scale;

    return atlas;
}

std::unique_ptr<FontAtlas> FontAtlas::builtinBitmap(const Desc& desc)
{
    std::unique_ptr<FontAtlas> atlas(new FontAtlas(desc));

    const AuraBitmapFont& font = GetDefaultBitmapFont();

    /*
     * The embedded font is a fixed 5x8 grid, so it can only be scaled by whole
     * texels without turning to mush. Round the requested pixel height to the
     * nearest integer multiple of the glyph height, floored at 1x.
     */
    const float requested = desc.pixelHeight / static_cast<float>(font.charHeight);
    atlas->_bitmapScale = std::max(1u, static_cast<u32>(std::lround(requested)));

    const float glyphHeight = static_cast<float>(font.charHeight * atlas->_bitmapScale);
    atlas->_ascent = glyphHeight;
    atlas->_descent = 0.0f;
    atlas->_lineHeight = glyphHeight + static_cast<float>(font.charSpacing * atlas->_bitmapScale);

    return atlas;
}

const GlyphInfo* FontAtlas::glyph(char32_t codepoint)
{
    if (const auto it = _glyphs.find(codepoint); it != _glyphs.end())
        return &it->second;

    GlyphInfo info{};
    const bool ok = _font ? rasterizeTrueType(codepoint, info)
                          : rasterizeBitmap(codepoint, info);

    if (!ok) 
    {
        /*
         * Missing codepoint: fall back to '?' so text still reads. Guard against
         * recursing forever if the font lacks '?' as well.
         */
        if (codepoint == kReplacementGlyph)
            return nullptr;

        const GlyphInfo* substitute = glyph(kReplacementGlyph);
        if (!substitute)
            return nullptr;

        //! Cache the substitution so the miss is paid only once per codepoint.
        return &(_glyphs[codepoint] = *substitute);
    }

    return &(_glyphs[codepoint] = info);
}

bool FontAtlas::rasterizeTrueType(char32_t codepoint, GlyphInfo& out)
{
    const int glyphIndex = stbtt_FindGlyphIndex(&_font->info, static_cast<int>(codepoint));
    if (glyphIndex == 0)
        return false; //! .notdef -- the font has no such character.

    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetGlyphHMetrics(&_font->info, glyphIndex, &advance, &leftSideBearing);

    /*
     * The bitmap box is in pixel space with y growing downwards and the origin
     * on the baseline, so y0 is normally negative (the glyph rises above it).
     */
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&_font->info, glyphIndex, _scale, _scale, &x0, &y0, &x1, &y1);

    const u32 glyphW = static_cast<u32>(std::max(0, x1 - x0));
    const u32 glyphH = static_cast<u32>(std::max(0, y1 - y0));

    out.advance = static_cast<float>(advance) * _scale;
    out.size = {static_cast<float>(glyphW), static_cast<float>(glyphH)};
    /*
     * Callers position from the top of the line box, so shift the baseline-
     * relative box down by the ascent. That keeps every glyph on a common top
     * edge without the caller ever handling the baseline itself.
     */
    out.bearing = {static_cast<float>(x0), _ascent + static_cast<float>(y0)};

    //! Whitespace has metrics but no coverage; leave its UVs degenerate.
    if (glyphW == 0 || glyphH == 0)
        return true;

    const auto origin = reserveCell(glyphW, glyphH);
    if (!origin)
        return false;

    /*
     * Rasterize straight into the atlas: passing the atlas width as the stride
     * lets stb write the glyph in place, with no intermediate buffer or copy.
     */
    u8* dst = _coverage.data() + static_cast<size_t>(origin->y) * _desc.width + origin->x;
    stbtt_MakeGlyphBitmap(&_font->info, dst,
                          static_cast<int>(glyphW), static_cast<int>(glyphH),
                          static_cast<int>(_desc.width),
                          _scale, _scale, glyphIndex);

    markDirty(origin->x, origin->y, glyphW, glyphH);

    const float atlasW = static_cast<float>(_desc.width);
    const float atlasH = static_cast<float>(_desc.height);
    out.uvMin = {static_cast<float>(origin->x) / atlasW, static_cast<float>(origin->y) / atlasH};
    out.uvMax = {static_cast<float>(origin->x + glyphW) / atlasW,
                 static_cast<float>(origin->y + glyphH) / atlasH};

    return true;
}

bool FontAtlas::rasterizeBitmap(char32_t codepoint, GlyphInfo& out)
{
    const AuraBitmapFont& font = GetDefaultBitmapFont();

    //! The embedded font only covers 7-bit ASCII.
    if (codepoint >= 128)
        return false;

    const u32 scale = _bitmapScale;
    const u32 glyphW = static_cast<u32>(font.charWidth) * scale;
    const u32 glyphH = static_cast<u32>(font.charHeight) * scale;

    out.advance = static_cast<float>((font.charWidth + font.charSpacing) * static_cast<i32>(scale));
    out.bearing = {0.0f, 0.0f};

    /*
     * Space carries advance only, so it needs no cell -- and its size must stay
     * zero, because a zero size is precisely how callers know not to emit a quad
     * for it. Giving it the font's cell size here would emit a full-sized quad
     * whose degenerate UVs sample whatever happens to sit at texel (0,0).
     */
    if (codepoint == U' ') 
    {
        out.size = {0.0f, 0.0f};
        return true;
    }

    out.size = {static_cast<float>(glyphW), static_cast<float>(glyphH)};

    const auto origin = reserveCell(glyphW, glyphH);
    if (!origin)
        return false;

    /*
     * Expand the 1-bit rows to 8-bit coverage, nearest-neighbour upscaled by
     * `scale`. Bits are stored MSB-first within each row, matching the order
     * CpuFrameBufferManager::drawText walks them, so both paths draw the same
     * shapes.
     */
    const auto& bits = font.data[static_cast<size_t>(codepoint)];
    std::vector<u8> expanded(static_cast<size_t>(glyphW) * glyphH, 0);

    for (i32 row = 0; row < font.charHeight; ++row) 
    {
        const u8 rowBits = bits[static_cast<size_t>(row)];
        for (i32 col = 0; col < font.charWidth; ++col) 
        {
            if ((rowBits & (1u << (font.charWidth - 1 - col))) == 0)
                continue;

            for (u32 sy = 0; sy < scale; ++sy) 
            {
                u8* dstRow = expanded.data()
                           + (static_cast<size_t>(row) * scale + sy) * glyphW
                           + static_cast<size_t>(col) * scale;
                std::memset(dstRow, 0xFF, scale);
            }
        }
    }

    blitCoverage(expanded.data(), glyphW, *origin, glyphW, glyphH);
    markDirty(origin->x, origin->y, glyphW, glyphH);

    const float atlasW = static_cast<float>(_desc.width);
    const float atlasH = static_cast<float>(_desc.height);
    out.uvMin = {static_cast<float>(origin->x) / atlasW, static_cast<float>(origin->y) / atlasH};
    out.uvMax = {static_cast<float>(origin->x + glyphW) / atlasW,
                 static_cast<float>(origin->y + glyphH) / atlasH};

    return true;
}

float FontAtlas::kerning(char32_t left, char32_t right) const noexcept
{
    //! The bitmap fallback is monospaced, so it has no kerning pairs.
    if (!_font)
        return 0.0f;

    const int raw = stbtt_GetCodepointKernAdvance(&_font->info,
                                                  static_cast<int>(left),
                                                  static_cast<int>(right));
    return static_cast<float>(raw) * _scale;
}

std::optional<glm::vec2> FontAtlas::solidTexelUv() noexcept
{
    if (_solidUv)
        return _solidUv;

    //! 4x4 rather than 1x1 so the centre UV stays clear of the cell's own edge
    //! texels: a bilinear tap there would otherwise reach into the guard band.
    constexpr u32 kCellSize = 4;

    const auto origin = reserveCell(kCellSize, kCellSize);
    if (!origin)
        return std::nullopt;

    std::array<u8, kCellSize * kCellSize> opaque{};
    opaque.fill(255);

    blitCoverage(opaque.data(), kCellSize, *origin, kCellSize, kCellSize);
    markDirty(origin->x, origin->y, kCellSize, kCellSize);

    _solidUv = glm::vec2{
        (static_cast<float>(origin->x) + 0.5f * kCellSize) / static_cast<float>(_desc.width),
        (static_cast<float>(origin->y) + 0.5f * kCellSize) / static_cast<float>(_desc.height)};

    return _solidUv;
}

std::optional<glm::uvec2> FontAtlas::reserveCell(u32 w, u32 h) noexcept
{
    const u32 padding = _desc.padding;
    const u32 strideW = w + padding;
    const u32 strideH = h + padding;

    if (strideW > _desc.width || strideH > _desc.height)
        return std::nullopt; //! Cannot fit even on an empty shelf.

    //! Current shelf exhausted horizontally: start a new one below it.
    if (_shelfX + strideW > _desc.width)
    {
        _shelfY += _shelfHeight;
        _shelfX = 0;
        _shelfHeight = 0;
    }

    if (_shelfY + strideH > _desc.height)
        return std::nullopt; //! Atlas full.

    const glm::uvec2 origin{_shelfX, _shelfY};

    _shelfX += strideW;
    _shelfHeight = std::max(_shelfHeight, strideH);

    return origin;
}

void FontAtlas::blitCoverage(const u8* src, u32 srcStride, glm::uvec2 origin, u32 w, u32 h) noexcept
{
    for (u32 row = 0; row < h; ++row) 
    {
        u8* dst = _coverage.data()
                + static_cast<size_t>(origin.y + row) * _desc.width
                + origin.x;
        std::memcpy(dst, src + static_cast<size_t>(row) * srcStride, w);
    }
}

void FontAtlas::markDirty(u32 x, u32 y, u32 w, u32 h) noexcept
{
    if (!_dirty) 
    {
        _dirtyX0 = x;
        _dirtyY0 = y;
        _dirtyX1 = x + w;
        _dirtyY1 = y + h;
        _dirty = true;
        return;
    }

    //! Union with what is already pending, so one upload covers every new glyph.
    _dirtyX0 = std::min(_dirtyX0, x);
    _dirtyY0 = std::min(_dirtyY0, y);
    _dirtyX1 = std::max(_dirtyX1, x + w);
    _dirtyY1 = std::max(_dirtyY1, y + h);
}

std::optional<FontAtlas::PendingUpload> FontAtlas::takeDirtyUpload()
{
    if (!_dirty)
        return std::nullopt;

    const DirtyRegion region{
        _dirtyX0,
        _dirtyY0,
        _dirtyX1 - _dirtyX0,
        _dirtyY1 - _dirtyY0,
    };

    /*
     * Expand coverage to RGBA8 because that is the one upload format every
     * backend implements. The glyph is drawn white with coverage in alpha, so
     * the vertex colour alone decides the text's colour at draw time.
     */
    _scratch.resize(static_cast<size_t>(region.width) * region.height * 4);

    for (u32 row = 0; row < region.height; ++row) 
    {
        const u8* src = _coverage.data()
                      + static_cast<size_t>(region.y + row) * _desc.width
                      + region.x;
        u8* dst = _scratch.data() + static_cast<size_t>(row) * region.width * 4;

        for (u32 col = 0; col < region.width; ++col) 
        {
            dst[col * 4 + 0] = 0xFF;
            dst[col * 4 + 1] = 0xFF;
            dst[col * 4 + 2] = 0xFF;
            dst[col * 4 + 3] = src[col];
        }
    }

    _dirty = false;
    return PendingUpload{region, std::span<const u8>(_scratch)};
}

} // namespace aura3d
