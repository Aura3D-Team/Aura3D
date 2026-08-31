#include "aura/Core/AuraFont/FontAtlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

#include "aura/Core/AuraFont/AuraBitmapFont.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace aura3d {
struct FontAtlas::FontImpl {
    stbtt_fontinfo info{};
};

namespace {
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

    if (lead < 0x80u)
    {
        ++offset;
        return static_cast<char32_t>(lead);
    }

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

    std::unique_ptr<FontAtlas> atlas(new FontAtlas(desc));
    atlas->_fontData = std::move(fontData);
    atlas->_font = std::make_unique<FontImpl>();

    const int offset = stbtt_GetFontOffsetForIndex(atlas->_fontData.data(), 0);
    if (offset < 0)
        return std::unexpected("FontAtlas: no usable face in the font image");

    if (!stbtt_InitFont(&atlas->_font->info, atlas->_fontData.data(), offset))
        return std::unexpected("FontAtlas: stb_truetype rejected the font image");

    atlas->_scale = stbtt_ScaleForPixelHeight(&atlas->_font->info, desc.pixelHeight);

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&atlas->_font->info, &ascent, &descent, &lineGap);

    atlas->_ascent = static_cast<float>(ascent) * atlas->_scale;
    atlas->_descent = static_cast<float>(descent) * atlas->_scale;

    atlas->_lineHeight = static_cast<float>(ascent - descent + lineGap) * atlas->_scale;

    return atlas;
}

std::unique_ptr<FontAtlas> FontAtlas::builtinBitmap(const Desc& desc)
{
    std::unique_ptr<FontAtlas> atlas(new FontAtlas(desc));

    const AuraBitmapFont& font = GetDefaultBitmapFont();

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
        if (codepoint == kReplacementGlyph)
            return nullptr;

        const GlyphInfo* substitute = glyph(kReplacementGlyph);
        if (!substitute)
            return nullptr;

        return &(_glyphs[codepoint] = *substitute);
    }

    return &(_glyphs[codepoint] = info);
}

bool FontAtlas::rasterizeTrueType(char32_t codepoint, GlyphInfo& out)
{
    const int glyphIndex = stbtt_FindGlyphIndex(&_font->info, static_cast<int>(codepoint));
    if (glyphIndex == 0)
        return false;

    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetGlyphHMetrics(&_font->info, glyphIndex, &advance, &leftSideBearing);

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&_font->info, glyphIndex, _scale, _scale, &x0, &y0, &x1, &y1);

    const u32 glyphW = static_cast<u32>(std::max(0, x1 - x0));
    const u32 glyphH = static_cast<u32>(std::max(0, y1 - y0));

    out.advance = static_cast<float>(advance) * _scale;
    out.size = {static_cast<float>(glyphW), static_cast<float>(glyphH)};

    out.bearing = {static_cast<float>(x0), _ascent + static_cast<float>(y0)};

    if (glyphW == 0 || glyphH == 0)
        return true;

    const auto origin = reserveCell(glyphW, glyphH);
    if (!origin)
        return false;

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

    if (codepoint >= 128)
        return false;

    const u32 scale = _bitmapScale;
    const u32 glyphW = static_cast<u32>(font.charWidth) * scale;
    const u32 glyphH = static_cast<u32>(font.charHeight) * scale;

    out.advance = static_cast<float>((font.charWidth + font.charSpacing) * static_cast<i32>(scale));
    out.bearing = {0.0f, 0.0f};

    if (codepoint == U' ')
    {
        out.size = {0.0f, 0.0f};
        return true;
    }

    out.size = {static_cast<float>(glyphW), static_cast<float>(glyphH)};

    const auto origin = reserveCell(glyphW, glyphH);
    if (!origin)
        return false;

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
    if (!_font)
        return 0.0f;

    const u64 pair = (static_cast<u64>(left) << 32) | static_cast<u32>(right);
    KernEntry& slot = _kernCache[pair & (kKernCacheSlots - 1)];

    if (slot.pair == pair)
        return slot.value;

    const int raw = stbtt_GetCodepointKernAdvance(&_font->info,
                                                  static_cast<int>(left),
                                                  static_cast<int>(right));

    slot = {pair, static_cast<f32>(raw) * _scale};
    return slot.value;
}

std::optional<glm::vec2> FontAtlas::solidTexelUv() noexcept
{
    if (_solidUv)
        return _solidUv;

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
        return std::nullopt;

    if (_shelfX + strideW > _desc.width)
    {
        _shelfY += _shelfHeight;
        _shelfX = 0;
        _shelfHeight = 0;
    }

    if (_shelfY + strideH > _desc.height)
        return std::nullopt;

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
