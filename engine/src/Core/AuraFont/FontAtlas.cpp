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

namespace aura3d
{
struct FontAtlas::FontImpl
{
    stbtt_fontinfo info{};
    std::vector<u8> data;
};

struct FontAtlas::Storage
{
    std::vector<u8> coverage;
    std::vector<u8> scratch;
    std::optional<glm::vec2> solidUv;
    std::array<std::optional<UvRect>, kMaxCornerRadius + 1> cornerMasks{};
    std::unordered_map<u32, UvRect> cornerRingMasks;
    std::unordered_map<u32, UvRect> convexMasks;
    u32 shelfX = 0, shelfY = 0, shelfHeight = 0;
    u32 dirtyX0 = 0, dirtyY0 = 0, dirtyX1 = 0, dirtyY1 = 0;
    bool dirty = false;
    u64 revision = 0, cleanRevision = 0;
};

namespace
{
constexpr char32_t kReplacementGlyph = U'?';

} // namespace

char32_t decodeUtf8(std::string_view text, size_t &offset) noexcept
{
    if (offset >= text.size())
        return 0;

    const auto byteAt = [&](size_t i) noexcept
    {
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

    constexpr std::array<char32_t, 4> minimum{0, 0x80u, 0x800u, 0x10000u};
    if (scalar < minimum[extraBytes] || scalar > 0x10FFFFu || (scalar >= 0xD800u && scalar <= 0xDFFFu))
    {
        ++offset;
        return 0xFFFDu;
    }
    offset += extraBytes + 1;
    return scalar;
}

FontAtlas::FontAtlas(const Desc &desc, std::shared_ptr<Storage> storage) : _desc(desc), _storage(std::move(storage))
{
    if (!_storage)
    {
        _storage = std::make_shared<Storage>();
        _storage->coverage.assign(static_cast<size_t>(_desc.width) * _desc.height, 0);
    }
}

FontAtlas::~FontAtlas() = default;
FontAtlas::FontAtlas(FontAtlas &&) noexcept = default;
FontAtlas &FontAtlas::operator=(FontAtlas &&) noexcept = default;

std::expected<std::unique_ptr<FontAtlas>, std::string> FontAtlas::fromFile(const std::string &path, const Desc &desc)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::unexpected("FontAtlas: cannot open font file '" + path + "'");

    const std::streamsize size = file.tellg();
    if (size <= 0)
        return std::unexpected("FontAtlas: font file '" + path + "' is empty");

    std::vector<u8> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
        return std::unexpected("FontAtlas: failed to read font file '" + path + "'");

    return fromMemory(std::move(bytes), desc);
}

std::expected<std::unique_ptr<FontAtlas>, std::string> FontAtlas::fromMemory(std::vector<u8> fontData, const Desc &desc)
{
    if (fontData.empty())
        return std::unexpected("FontAtlas: empty font image");

    std::unique_ptr<FontAtlas> atlas(new FontAtlas(desc));
    atlas->_font = std::make_shared<FontImpl>();
    atlas->_font->data = std::move(fontData);

    const int offset = stbtt_GetFontOffsetForIndex(atlas->_font->data.data(), 0);
    if (offset < 0)
        return std::unexpected("FontAtlas: no usable face in the font image");

    if (!stbtt_InitFont(&atlas->_font->info, atlas->_font->data.data(), offset))
        return std::unexpected("FontAtlas: stb_truetype rejected the font image");

    atlas->initializeMetrics();

    return atlas;
}

std::unique_ptr<FontAtlas> FontAtlas::builtinBitmap(const Desc &desc)
{
    std::unique_ptr<FontAtlas> atlas(new FontAtlas(desc));

    atlas->initializeMetrics();
    return atlas;
}

void FontAtlas::initializeMetrics()
{
    if (_font)
    {
        _scale = stbtt_ScaleForPixelHeight(&_font->info, _desc.pixelHeight);
        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&_font->info, &ascent, &descent, &lineGap);
        _ascent = static_cast<float>(ascent) * _scale;
        _descent = static_cast<float>(descent) * _scale;
        _lineHeight = static_cast<float>(ascent - descent + lineGap) * _scale;
        return;
    }
    const AuraBitmapFont &font = GetDefaultBitmapFont();
    _desc.pixelHeight = std::max(1.0f, std::round(_desc.pixelHeight));
    _bitmapLayout = font.layout(static_cast<u32>(_desc.pixelHeight));
    _ascent = static_cast<float>(_bitmapLayout.ascent);
    _descent = _ascent - _desc.pixelHeight;
    _lineHeight = _desc.pixelHeight;
}

std::unique_ptr<FontAtlas> FontAtlas::createSharedSize(float pixelHeight) const
{
    Desc desc = _desc;
    desc.pixelHeight = pixelHeight;
    std::unique_ptr<FontAtlas> atlas(new FontAtlas(desc, _storage));
    atlas->_font = _font;
    atlas->initializeMetrics();
    return atlas;
}

const void *FontAtlas::storageIdentity() const noexcept
{
    return _storage.get();
}

u64 FontAtlas::coverageRevision() const noexcept
{
    return _storage->revision;
}

bool FontAtlas::dirty() const noexcept
{
    return _storage->dirty;
}

bool FontAtlas::takeAllocationFailure() noexcept
{
    const bool failed = _failedAllocations != 0;
    _failedAllocations = 0;
    return failed;
}

const GlyphInfo *FontAtlas::glyph(char32_t codepoint)
{
    if (const auto it = _glyphs.find(codepoint); it != _glyphs.end())
        return &it->second;

    GlyphInfo info{};
    const u32 failures = _failedAllocations;
    const bool ok = _font ? rasterizeTrueType(codepoint, info) : rasterizeBitmap(codepoint, info);

    if (!ok)
    {
        if (codepoint == kReplacementGlyph)
            return nullptr;

        const bool full = failures != _failedAllocations;
        const GlyphInfo *substitute = glyph(kReplacementGlyph);
        if (!substitute)
            return nullptr;

        // A full sheet may still fit this glyph later after a failed taller cell.
        // Keep real missing-glyph substitutes cached, but never cache exhaustion.
        return full ? substitute : &(_glyphs[codepoint] = *substitute);
    }

    return &(_glyphs[codepoint] = info);
}

bool FontAtlas::rasterizeTrueType(char32_t codepoint, GlyphInfo &out)
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

    u8 *dst = _storage->coverage.data() + static_cast<size_t>(origin->y) * _desc.width + origin->x;
    stbtt_MakeGlyphBitmap(&_font->info, dst, static_cast<int>(glyphW), static_cast<int>(glyphH),
                          static_cast<int>(_desc.width), _scale, _scale, glyphIndex);

    markDirty(origin->x, origin->y, glyphW, glyphH);

    const float atlasW = static_cast<float>(_desc.width);
    const float atlasH = static_cast<float>(_desc.height);
    out.uvMin = {static_cast<float>(origin->x) / atlasW, static_cast<float>(origin->y) / atlasH};
    out.uvMax = {static_cast<float>(origin->x + glyphW) / atlasW, static_cast<float>(origin->y + glyphH) / atlasH};

    return true;
}

bool FontAtlas::rasterizeBitmap(char32_t codepoint, GlyphInfo &out)
{
    const AuraBitmapFont &font = GetDefaultBitmapFont();

    if (codepoint >= 128)
        return false;

    const auto &glyph = font.data[static_cast<size_t>(codepoint)];
    const AuraBitmapFont::ScaledGlyph &scaled = _bitmapLayout.glyphs[static_cast<size_t>(codepoint)];
    const auto glyphW = static_cast<u32>(scaled.columns.size());
    const auto glyphH = static_cast<u32>(scaled.rows.size());

    out.advance = static_cast<float>(scaled.advance);
    out.bearing = {0.0f, 0.0f};
    out.size = {0.0f, 0.0f};
    if (glyphW == 0)
        return true;
    out.size = {static_cast<float>(glyphW), static_cast<float>(glyphH)};

    const auto origin = reserveCell(glyphW, glyphH);
    if (!origin)
        return false;

    // Rasterize at device size with binary coverage, so drawing needs no resampling.
    for (u32 row = 0; row < glyphH; ++row)
    {
        u8 *dst = _storage->coverage.data() + static_cast<size_t>(origin->y + row) * _desc.width + origin->x;
        for (u32 col = 0; col < glyphW; ++col)
            dst[col] = font.inked(glyph[scaled.rows[row]], scaled.columns[col]) ? 255 : 0;
    }
    markDirty(origin->x, origin->y, glyphW, glyphH);

    const float atlasW = static_cast<float>(_desc.width);
    const float atlasH = static_cast<float>(_desc.height);
    out.uvMin = {static_cast<float>(origin->x) / atlasW, static_cast<float>(origin->y) / atlasH};
    out.uvMax = {static_cast<float>(origin->x + glyphW) / atlasW, static_cast<float>(origin->y + glyphH) / atlasH};

    return true;
}

float FontAtlas::kerning(char32_t left, char32_t right) const noexcept
{
    if (!_font)
        return 0.0f;

    const u64 pair = (static_cast<u64>(left) << 32) | static_cast<u32>(right);
    KernEntry &slot = _kernCache[pair & (kKernCacheSlots - 1)];

    if (slot.pair == pair)
        return slot.value;

    const int raw = stbtt_GetCodepointKernAdvance(&_font->info, static_cast<int>(left), static_cast<int>(right));

    slot = {pair, static_cast<f32>(raw) * _scale};
    return slot.value;
}

std::optional<glm::vec2> FontAtlas::solidTexelUv() noexcept
{
    if (_storage->solidUv)
        return _storage->solidUv;

    constexpr u32 kCellSize = 4;

    const auto origin = reserveCell(kCellSize, kCellSize);
    if (!origin)
        return std::nullopt;

    std::array<u8, kCellSize * kCellSize> opaque{};
    opaque.fill(255);

    blitCoverage(opaque.data(), kCellSize, *origin, kCellSize, kCellSize);
    markDirty(origin->x, origin->y, kCellSize, kCellSize);

    _storage->solidUv =
        glm::vec2{(static_cast<float>(origin->x) + 0.5f * kCellSize) / static_cast<float>(_desc.width),
                  (static_cast<float>(origin->y) + 0.5f * kCellSize) / static_cast<float>(_desc.height)};

    return _storage->solidUv;
}

const FontAtlas::UvRect *FontAtlas::cornerMask(u32 radius) noexcept
{
    if (radius == 0 || radius > kMaxCornerRadius)
        return nullptr;

    std::optional<UvRect> &slot = _storage->cornerMasks[radius];
    if (slot)
        return &*slot;

    const auto origin = reserveCell(radius, radius);
    if (!origin)
        return nullptr;

    /*
     * Exact coverage, not a point sample: each texel gets the area of the disc
     * that actually falls inside it. Point sampling would put a hard 0/255
     * edge back on the curve and undo the reason for the mask.
     *
     * F is the antiderivative of sqrt(R^2 - x^2), so the area under the arc
     * over [a, b] is F(b) - F(a). Within one texel column the arc is either
     * above the texel, crossing it, or below it; xa and xb are the two x where
     * it enters and leaves, which splits the integral into a full-height part,
     * an under-the-arc part and an empty part.
     */
    const auto R = static_cast<float>(radius);

    const auto F = [R](float x) noexcept
    {
        const float clamped = std::clamp(x / R, -1.0f, 1.0f);
        return 0.5f * (x * std::sqrt(std::max(R * R - x * x, 0.0f)) + R * R * std::asin(clamped));
    };

    std::vector<u8> cell(static_cast<size_t>(radius) * radius);

    for (u32 j = 0; j < radius; ++j)
    {
        //! Measured from the disc's centre, which sits at the cell's far
        //! corner -- so texel (0,0) is the outermost and least covered.
        const float y0 = R - static_cast<float>(j) - 1.0f;
        const float y1 = R - static_cast<float>(j);

        const float xa = std::sqrt(std::max(R * R - y1 * y1, 0.0f));
        const float xb = std::sqrt(std::max(R * R - y0 * y0, 0.0f));

        for (u32 i = 0; i < radius; ++i)
        {
            const float x0 = R - static_cast<float>(i) - 1.0f;
            const float x1 = R - static_cast<float>(i);

            const float a = std::clamp(xa, x0, x1);
            const float b = std::clamp(xb, x0, x1);

            const float area = (a - x0) * (y1 - y0) + (F(b) - F(a)) - (b - a) * y0;

            cell[static_cast<size_t>(j) * radius + i] =
                static_cast<u8>(std::lround(std::clamp(area, 0.0f, 1.0f) * 255.0f));
        }
    }

    blitCoverage(cell.data(), radius, *origin, radius, radius);
    markDirty(origin->x, origin->y, radius, radius);

    const auto atlasW = static_cast<float>(_desc.width);
    const auto atlasH = static_cast<float>(_desc.height);

    //! Cell edges, not texel centres: a corner drawn `radius` pixels wide then
    //! samples each texel at its centre, one to one.
    slot = UvRect{{static_cast<float>(origin->x) / atlasW, static_cast<float>(origin->y) / atlasH},
                  {static_cast<float>(origin->x + radius) / atlasW, static_cast<float>(origin->y + radius) / atlasH}};

    return &*slot;
}

const FontAtlas::UvRect *FontAtlas::cornerRingMask(u32 radius, f32 width) noexcept
{
    if (!radius || radius > kMaxCornerRadius || !std::isfinite(width) || width <= 0)
        return nullptr;
    const u32 quantized =
        std::clamp(static_cast<u32>(std::lround(std::min(width, f32(radius)) * 256)), 1u, radius * 256);
    const u32 key = (radius << 16) | quantized;
    if (auto it = _storage->cornerRingMasks.find(key); it != _storage->cornerRingMasks.end())
        return &it->second;
    const auto origin = reserveCell(radius, radius);
    if (!origin)
        return nullptr;

    // Difference of exact disc areas. Drawing the outer disc and covering its
    // centre with the fill is incorrect for a translucent fill or an outline.
    const auto area = [](double radius, double x0, double y0)
    {
        if (radius <= 0 || x0 >= radius || y0 >= radius)
            return 0.0;
        const double x1 = x0 + 1, y1 = y0 + 1;
        const auto integral = [radius](double x)
        {
            x = std::clamp(x, 0.0, radius);
            return .5 *
                   (x * std::sqrt(std::max(0.0, radius * radius - x * x)) + radius * radius * std::asin(x / radius));
        };
        const double a = std::clamp(std::sqrt(std::max(0.0, radius * radius - y1 * y1)), x0, x1);
        const double b = std::clamp(std::sqrt(std::max(0.0, radius * radius - y0 * y0)), x0, x1);
        return std::clamp((a - x0) + integral(b) - integral(a) - (b - a) * y0, 0.0, 1.0);
    };
    const double inner = double(radius) - double(quantized) / 256;
    std::vector<u8> cell(usize(radius) * radius);
    for (u32 y = 0; y < radius; ++y)
        for (u32 x = 0; x < radius; ++x)
        {
            const double x0 = double(radius - x - 1), y0 = double(radius - y - 1);
            const double coverage = area(radius, x0, y0) - area(inner, x0, y0);
            cell[usize(y) * radius + x] = u8(std::lround(std::clamp(coverage, 0.0, 1.0) * 255));
        }
    blitCoverage(cell.data(), radius, *origin, radius, radius);
    markDirty(origin->x, origin->y, radius, radius);
    const UvRect uv{{f32(origin->x) / _desc.width, f32(origin->y) / _desc.height},
                    {f32(origin->x + radius) / _desc.width, f32(origin->y + radius) / _desc.height}};
    return &_storage->cornerRingMasks.emplace(key, uv).first->second;
}

namespace
{

//! Widest polygon convexMask() will clip, and the working buffer it needs.
//! Each half-plane can add at most one vertex, and there are four of them.
constexpr size_t kMaxMaskVertices = 16;

using MaskBuffer = std::array<glm::vec2, kMaxMaskVertices>;

/**
 * @brief Clips a convex polygon against one axis-aligned half-plane.
 *
 * The Sutherland-Hodgman step: walk the edges, keep every vertex on the
 * wanted side, and emit the crossing point wherever an edge changes side.
 * Convexity is what makes one output ring enough -- a concave polygon would
 * need the general Weiler-Atherton split.
 *
 * @param axis 0 for x, 1 for y.
 * @param keepGreater Keep the side above @p limit rather than below it.
 * @return Vertex count written to @p out.
 */
[[nodiscard]] size_t clipHalfPlane(std::span<const glm::vec2> in, MaskBuffer &out, int axis, float limit,
                                   bool keepGreater) noexcept
{
    const auto coordinate = [axis](const glm::vec2 &v) noexcept
    {
        return axis == 0 ? v.x : v.y;
    };

    const auto inside = [&](const glm::vec2 &v) noexcept
    {
        return keepGreater ? coordinate(v) >= limit : coordinate(v) <= limit;
    };

    size_t count = 0;

    for (size_t i = 0, n = in.size(); i < n && count + 2 <= kMaxMaskVertices; ++i)
    {
        const glm::vec2 &a = in[i];
        const glm::vec2 &b = in[(i + 1) % n];

        const bool insideA = inside(a);

        if (insideA)
            out[count++] = a;

        if (insideA == inside(b))
            continue;

        //! The edge straddles the plane, so it has exactly one crossing. The
        //! denominator cannot vanish: the two ends are on opposite sides.
        const float ca = coordinate(a);
        const float t = (limit - ca) / (coordinate(b) - ca);

        out[count++] = a + (b - a) * t;
    }

    return count;
}

/// Twice the polygon's area, by the shoelace formula. Unsigned, because a
/// coverage value has no use for the winding direction.
[[nodiscard]] float polygonArea(std::span<const glm::vec2> polygon) noexcept
{
    float twice = 0.0f;

    for (size_t i = 0, n = polygon.size(); i < n; ++i)
    {
        const glm::vec2 &a = polygon[i];
        const glm::vec2 &b = polygon[(i + 1) % n];
        twice += a.x * b.y - b.x * a.y;
    }

    return std::abs(twice) * 0.5f;
}

} // namespace

const FontAtlas::UvRect *FontAtlas::convexMask(u32 id, u32 size, std::span<const glm::vec2> polygon) noexcept
{
    if (size == 0 || size > kMaxMaskSize || polygon.size() < 3 || polygon.size() > kMaxMaskVertices)
        return nullptr;

    if (const auto it = _storage->convexMasks.find(id); it != _storage->convexMasks.end())
        return &it->second;

    const auto origin = reserveCell(size, size);
    if (!origin)
        return nullptr;

    std::vector<u8> cell(static_cast<size_t>(size) * size);

    MaskBuffer front{};
    MaskBuffer back{};

    for (u32 j = 0; j < size; ++j)
    {
        const auto y0 = static_cast<float>(j);

        for (u32 i = 0; i < size; ++i)
        {
            const auto x0 = static_cast<float>(i);

            /*
             * The polygon clipped to this one texel's square; what survives is
             * exactly the part of the shape the texel covers, and its area is
             * the coverage. Four half-planes, alternating buffers so neither
             * clip reads what it is writing.
             */
            size_t count = polygon.size();
            std::copy(polygon.begin(), polygon.end(), front.begin());

            count = clipHalfPlane({front.data(), count}, back, 0, x0, true);
            count = clipHalfPlane({back.data(), count}, front, 0, x0 + 1.0f, false);
            count = clipHalfPlane({front.data(), count}, back, 1, y0, true);
            count = clipHalfPlane({back.data(), count}, front, 1, y0 + 1.0f, false);

            const float area = count >= 3 ? polygonArea({front.data(), count}) : 0.0f;

            cell[static_cast<size_t>(j) * size + i] =
                static_cast<u8>(std::lround(std::clamp(area, 0.0f, 1.0f) * 255.0f));
        }
    }

    blitCoverage(cell.data(), size, *origin, size, size);
    markDirty(origin->x, origin->y, size, size);

    const auto atlasW = static_cast<float>(_desc.width);
    const auto atlasH = static_cast<float>(_desc.height);

    const auto [entry, inserted] = _storage->convexMasks.emplace(
        id, UvRect{{static_cast<float>(origin->x) / atlasW, static_cast<float>(origin->y) / atlasH},
                   {static_cast<float>(origin->x + size) / atlasW, static_cast<float>(origin->y + size) / atlasH}});

    return &entry->second;
}

std::optional<glm::uvec2> FontAtlas::reserveCell(u32 w, u32 h) noexcept
{
    const u32 padding = _desc.padding;
    const auto fail = [&]() -> std::optional<glm::uvec2>
    {
        ++_failedAllocations;
        return std::nullopt;
    };
    if (w > _desc.width || h > _desc.height || padding > _desc.width - w || padding > _desc.height - h)
        return fail();

    const u32 strideW = w + padding;
    const u32 strideH = h + padding;
    u32 x = _storage->shelfX;
    u32 y = _storage->shelfY;
    u32 shelfHeight = _storage->shelfHeight;
    if (strideW > _desc.width - x)
    {
        y += shelfHeight;
        x = 0;
        shelfHeight = 0;
    }
    if (strideH > _desc.height - y)
        return fail();

    // Commit only successful placements: a tall rejected cell must not discard
    // usable space remaining on the current shelf.
    _storage->shelfX = x + strideW;
    _storage->shelfY = y;
    _storage->shelfHeight = std::max(shelfHeight, strideH);
    return glm::uvec2{x, y};
}

void FontAtlas::blitCoverage(const u8 *src, u32 srcStride, glm::uvec2 origin, u32 w, u32 h) noexcept
{
    for (u32 row = 0; row < h; ++row)
    {
        u8 *dst = _storage->coverage.data() + static_cast<size_t>(origin.y + row) * _desc.width + origin.x;
        std::memcpy(dst, src + static_cast<size_t>(row) * srcStride, w);
    }
}

void FontAtlas::markDirty(u32 x, u32 y, u32 w, u32 h) noexcept
{
    ++_storage->revision;
    if (!_storage->dirty)
    {
        _storage->dirtyX0 = x;
        _storage->dirtyY0 = y;
        _storage->dirtyX1 = x + w;
        _storage->dirtyY1 = y + h;
        _storage->dirty = true;
        return;
    }

    _storage->dirtyX0 = std::min(_storage->dirtyX0, x);
    _storage->dirtyY0 = std::min(_storage->dirtyY0, y);
    _storage->dirtyX1 = std::max(_storage->dirtyX1, x + w);
    _storage->dirtyY1 = std::max(_storage->dirtyY1, y + h);
}

std::optional<FontAtlas::PendingUpload> FontAtlas::takeUpload(u64 &revision)
{
    Storage &storage = *_storage;
    if (revision == storage.revision)
        return std::nullopt;

    //! Only the consumer that saw the last clean state can make do with the dirty rectangle.
    DirtyRegion region{0, 0, _desc.width, _desc.height};
    if (storage.dirty && revision == storage.cleanRevision)
        region = {storage.dirtyX0, storage.dirtyY0, storage.dirtyX1 - storage.dirtyX0,
                  storage.dirtyY1 - storage.dirtyY0};

    const u8 *first = storage.coverage.data() + static_cast<size_t>(region.y) * _desc.width + region.x;
    std::span<const u8> coverage{first, static_cast<size_t>(region.width) * region.height};
    //! Whole rows are already contiguous; a narrower rectangle is packed.
    if (region.width != _desc.width)
    {
        storage.scratch.resize(coverage.size());
        for (u32 row = 0; row < region.height; ++row)
            std::memcpy(storage.scratch.data() + static_cast<size_t>(row) * region.width,
                        first + static_cast<size_t>(row) * _desc.width, region.width);
        coverage = storage.scratch;
    }

    storage.dirty = false;
    storage.cleanRevision = revision = storage.revision;
    return PendingUpload{region, coverage};
}

} // namespace aura3d
