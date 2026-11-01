#include "aura/Renderer/Software/CpuAura/CpuFrameBufferManager.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

#include "CpuSimd.h"

namespace aura3d::cpu
{

namespace
{

/*
 * The GPU backends draw into sRGB targets: colors are linear, blending happens
 * on linear values, and the target stores them encoded. The software target
 * does the same through tables, so every backend shows the same picture.
 * 4096 encode steps keep every result within one code of exact rounding, the
 * tolerance hardware sRGB conversion has too.
 */
constexpr u32 kEncodeSteps = 4095;

struct SrgbTables
{
    std::array<f32, 256> decode{};
    std::array<u8, kEncodeSteps + 1> encode{};
};

[[nodiscard]] SrgbTables makeSrgbTables() noexcept
{
    SrgbTables tables;
    for (u32 i = 0; i < tables.decode.size(); ++i)
    {
        const f32 c = static_cast<f32>(i) / 255.0f;
        tables.decode[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    for (u32 i = 0; i <= kEncodeSteps; ++i)
    {
        const f32 l = static_cast<f32>(i) / static_cast<f32>(kEncodeSteps);
        const f32 c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
        tables.encode[i] = static_cast<u8>(std::lround(c * 255.0f));
    }
    return tables;
}

const SrgbTables kSrgb = makeSrgbTables();

//! @p linear is in [0, 1]; blending can overshoot by a rounding error, hence the min. Never
//! negative, so adding a half before truncating rounds to nearest without a libm call.
[[nodiscard]] u32 encodeChannel(f32 linear) noexcept
{
    const f32 step = std::min(linear, 1.0f) * static_cast<f32>(kEncodeSteps);
    return kSrgb.encode[static_cast<u32>(step + 0.5f)]; // NOLINT(bugprone-incorrect-roundings)
}

//! Linear color in [0, 1] to the stored 0xAARRGGBB: RGB encoded, alpha rounded like a UNORM write.
[[nodiscard]] u32 packLinear(const glm::vec4 &color) noexcept
{
    const u32 alpha = static_cast<u32>(std::min(color.a, 1.0f) * 255.0f + 0.5f); // NOLINT(bugprone-incorrect-roundings)
    return (alpha << 24) | (encodeChannel(color.r) << 16) | (encodeChannel(color.g) << 8) | encodeChannel(color.b);
}

struct Rgba
{
    F r, g, b, a;
};

AURA_RASTER_INLINE F clamp01(F a) noexcept
{
    return min(max0(a), splat(1.0f));
}

//! Lanes outside @p active come back zero; callers select them away.
AURA_RASTER_INLINE I encode(const Rgba &color, u32 active) noexcept
{
    const F steps = splat(static_cast<f32>(kEncodeSteps)), half = splat(0.5f);
    const I alpha = shiftLeft<24>(truncate(madd(clamp01(color.a), splat(255.0f), half)));
    alignas(32) Lanes<u32> r, g, b, rgb{};
    store(r.data(), truncate(madd(clamp01(color.r), steps, half)));
    store(g.data(), truncate(madd(clamp01(color.g), steps, half)));
    store(b.data(), truncate(madd(clamp01(color.b), steps, half)));
    forEachLane(active,
                [&](i32 lane)
                {
                    rgb[lane] = (static_cast<u32>(kSrgb.encode[r[lane]]) << 16) |
                                (static_cast<u32>(kSrgb.encode[g[lane]]) << 8) | kSrgb.encode[b[lane]];
                });
    return alpha | load(rgb.data());
}

AURA_RASTER_INLINE Rgba decode(I stored, u32 active) noexcept
{
    const F alpha = toFloat(shiftRight<24>(stored)) * splat(1.0f / 255.0f);
    alignas(32) Lanes<u32> words;
    alignas(32) Lanes<f32> r{}, g{}, b{};
    store(words.data(), stored);
    forEachLane(active,
                [&](i32 lane)
                {
                    r[lane] = kSrgb.decode[(words[lane] >> 16) & 255u];
                    g[lane] = kSrgb.decode[(words[lane] >> 8) & 255u];
                    b[lane] = kSrgb.decode[words[lane] & 255u];
                });
    return {load(r.data()), load(g.data()), load(b.data()), alpha};
}

//! The four texels and weights of a bilinear, clamp-to-edge sample with texel centres at (i + 0.5) / size.
struct Footprint
{
    I t00, t10, t01, t11;
    F tx, ty;
};

//! @p u folded into [0, 1] by @p wrap; clamp01() last, so NaN and infinities land on 0 as before.
AURA_RASTER_INLINE F wrapped(F u, TextureWrap wrap) noexcept
{
    if (wrap == TextureWrap::Repeat)
        return clamp01(u - floor(u));
    if (wrap == TextureWrap::MirroredRepeat)
    {
        const F period = u - floor(u * splat(0.5f)) * splat(2.0f);
        return clamp01(min(period, splat(2.0f) - period));
    }
    return clamp01(u);
}

//! Neighbour index along one axis: wrapped round for Repeat, clamped to the edge otherwise.
[[nodiscard]] AURA_RASTER_INLINE size_t texelIndex(i32 i, i32 size, bool repeat) noexcept
{
    if (repeat)
        return static_cast<size_t>(i < 0 ? size - 1 : i >= size ? 0 : i);
    return static_cast<size_t>(std::clamp(i, 0, size - 1));
}

template <class Texel>
AURA_RASTER_INLINE Footprint footprint(const Texel *texels, const Texture &texture, F u, F v, u32 active) noexcept
{
    const i32 width = texture.width, height = texture.height;
    //! Nearest takes the texel the point falls in, so it skips the half-texel shift and keeps zero weights.
    const F centre = splat(texture.nearest ? 0.0f : 0.5f);
    const F fx = wrapped(u, texture.wrapU) * splat(static_cast<f32>(width)) - centre;
    const F fy = wrapped(v, texture.wrapV) * splat(static_cast<f32>(height)) - centre;
    const F x0 = floor(fx), y0 = floor(fy);
    const bool repeatU = texture.wrapU == TextureWrap::Repeat, repeatV = texture.wrapV == TextureWrap::Repeat;
    alignas(32) Lanes<u32> xs, ys, t00{}, t10{}, t01{}, t11{};
    store(xs.data(), truncate(x0));
    store(ys.data(), truncate(y0));
    const auto row = static_cast<size_t>(width);
    forEachLane(active,
                [&](i32 lane)
                {
                    //! x0 is in [-1, width]: -1 from the half-texel shift, width from nearest at u == 1.
                    const i32 x = static_cast<i32>(xs[lane]), y = static_cast<i32>(ys[lane]);
                    const size_t left = texelIndex(x, width, repeatU);
                    const size_t right = texelIndex(x + 1, width, repeatU);
                    const size_t top = texelIndex(y, height, repeatV) * row;
                    const size_t bottom = texelIndex(y + 1, height, repeatV) * row;
                    t00[lane] = texels[top + left];
                    t10[lane] = texels[top + right];
                    t01[lane] = texels[bottom + left];
                    t11[lane] = texels[bottom + right];
                });
    const F zero = splat(0.0f);
    return {load(t00.data()),
            load(t10.data()),
            load(t01.data()),
            load(t11.data()),
            texture.nearest ? zero : fx - x0,
            texture.nearest ? zero : fy - y0};
}

//! One 8-bit channel of the footprint, filtered, in [0, 255].
template <int Shift> AURA_RASTER_INLINE F filtered(const Footprint &f) noexcept
{
    const I byte = splat(255u);
    const F c00 = toFloat(shiftRight<Shift>(f.t00) & byte), c10 = toFloat(shiftRight<Shift>(f.t10) & byte);
    const F c01 = toFloat(shiftRight<Shift>(f.t01) & byte), c11 = toFloat(shiftRight<Shift>(f.t11) & byte);
    const F top = madd(f.tx, c10 - c00, c00);
    const F bottom = madd(f.tx, c11 - c01, c01);
    return madd(f.ty, bottom - top, top);
}

/**
 * @brief Whether the directed edge @p a -> @p b is a top or left edge of a
 *        positive-area triangle, in this rasteriser's Y-down screen space.
 *
 * A horizontal edge has the interior below it exactly when it runs +x (a top
 * edge); any other edge has it to the right exactly when it runs -y (a left
 * edge). The mirror of the usual Y-up classification.
 */
[[nodiscard]] constexpr bool isTopLeftEdge(const ScreenVertex &a, const ScreenVertex &b) noexcept
{
    const f32 ex = b.x - a.x;
    const f32 ey = b.y - a.y;
    return (ey == 0.0f && ex > 0.0f) || (ey < 0.0f);
}

enum class Shade : u8
{
    Flat,     //!< One color for the whole triangle.
    Vertex,   //!< Interpolated color, times a 1x1 texture's texel if one is bound.
    Texture,  //!< Interpolated color times a bilinear RGBA sample.
    Coverage, //!< Interpolated color, alpha times a bilinear coverage sample.
};

//! Edge i's function is dx * (py - oy) - dy * (px - ox).
struct Edge
{
    f32 ox, oy, dx, dy;
    bool topLeft;
};

/// One triangle clipped to one band. Attributes interpolate as a0 + b1 * d1 + b2 * d2, with uv
/// and color already multiplied by 1/w when the triangle is in perspective.
struct Setup
{
    std::array<Edge, 3> edges;
    f32 invArea2;
    i32 xmin, xmax, ymin, ymax;
    bool walkSpans;
    f32 z0, dz1, dz2;
    f32 iw0, diw1, diw2;
    glm::vec2 uv0, duv1, duv2;
    glm::vec4 c0, dc1, dc2;
    glm::vec4 factor;
    glm::vec4 flat;
    u32 flatPacked;
    const Texture *texture;
};

struct Planes
{
    u32 *color;
    f32 *depth;
    i32 stride;
    bool depthTest;
};

//! [xFirst, xLast] covers every pixel of row @p y inside the triangle, or is empty (xFirst > xLast).
AURA_RASTER_INLINE void rowSpan(const Setup &s, f32 row0, f32 row1, f32 row2, i32 &xFirst, i32 &xLast) noexcept
{
    xFirst = s.xmin;
    xLast = s.xmax;
    if (!s.walkSpans)
        return;
    /*
     * Walk only the row's span: a thin diagonal covers a sliver of its box.
     * Each edge bounds px on one side where its function crosses zero; the
     * bound carries a pixel of slack plus the edge function's float error, so
     * the exact per-pixel test still decides every pixel.
     */
    f32 lo = static_cast<f32>(s.xmin);
    f32 hi = static_cast<f32>(s.xmax);
    bool rowEmpty = false;
    const auto bound = [&](f32 row, const Edge &edge)
    {
        if (edge.dy == 0.0f)
        {
            rowEmpty |= row < 0 || (row == 0 && !edge.topLeft);
            return;
        }
        constexpr f32 kEpsilon = std::numeric_limits<f32>::epsilon();
        const f32 offset = row / edge.dy;
        const f32 slack = 1.0f + 4.0f * kEpsilon * (std::abs(offset) + std::abs(edge.ox));
        //! Pixel x is centred on x + 0.5. A NaN bound leaves lo and hi as they were.
        const f32 crossing = edge.ox + offset - 0.5f;
        if (edge.dy > 0)
            hi = std::min(hi, crossing + slack);
        else
            lo = std::max(lo, crossing - slack);
    };
    bound(row0, s.edges[0]);
    bound(row1, s.edges[1]);
    bound(row2, s.edges[2]);
    if (rowEmpty || !(lo <= hi))
    {
        xFirst = 1;
        xLast = 0;
        return;
    }
    xFirst = static_cast<i32>(std::ceil(lo));
    xLast = static_cast<i32>(std::floor(hi));
}

AURA_RASTER_INLINE M inside(F w, M topLeft) noexcept
{
    const F zero = splat(0.0f);
    return (w > zero) | ((w == zero) & topLeft);
}

template <bool Blended, Shade S, bool Perspective>
AURA_RASTER_NOINLINE void rasterize(const Setup &s, const Planes &planes) noexcept
{
    const Edge &e0 = s.edges[0], &e1 = s.edges[1], &e2 = s.edges[2];
    const F ox0 = splat(e0.ox), ox1 = splat(e1.ox), ox2 = splat(e2.ox);
    const F dy0 = splat(e0.dy), dy1 = splat(e1.dy), dy2 = splat(e2.dy);
    const M tl0 = maskOf(e0.topLeft), tl1 = maskOf(e1.topLeft), tl2 = maskOf(e2.topLeft);
    const F invArea2 = splat(s.invArea2), zero = splat(0.0f), one = splat(1.0f), centers = laneCenters();
    const F z0 = splat(s.z0), dz1 = splat(s.dz1), dz2 = splat(s.dz2);
    const F iw0 = splat(s.iw0), diw1 = splat(s.diw1), diw2 = splat(s.diw2);
    const F u0 = splat(s.uv0.x), du1 = splat(s.duv1.x), du2 = splat(s.duv2.x);
    const F v0 = splat(s.uv0.y), dv1 = splat(s.duv1.y), dv2 = splat(s.duv2.y);
    const Rgba c0{splat(s.c0.r), splat(s.c0.g), splat(s.c0.b), splat(s.c0.a)};
    const Rgba dc1{splat(s.dc1.r), splat(s.dc1.g), splat(s.dc1.b), splat(s.dc1.a)};
    const Rgba dc2{splat(s.dc2.r), splat(s.dc2.g), splat(s.dc2.b), splat(s.dc2.a)};
    const Rgba factor{splat(s.factor.r), splat(s.factor.g), splat(s.factor.b), splat(s.factor.a)};
    const Rgba flat{splat(s.flat.r), splat(s.flat.g), splat(s.flat.b), splat(s.flat.a)};
    const I flatPacked = splat(s.flatPacked);
    const F byteScale = splat(1.0f / 255.0f);

    for (i32 y = s.ymin; y <= s.ymax; ++y)
    {
        const f32 py = static_cast<f32>(y) + 0.5f;
        const f32 row0 = e0.dx * (py - e0.oy), row1 = e1.dx * (py - e1.oy), row2 = e2.dx * (py - e2.oy);
        i32 xFirst = 0, xLast = 0;
        rowSpan(s, row0, row1, row2, xFirst, xLast);
        if (xFirst > xLast)
            continue;

        const F r0 = splat(row0), r1 = splat(row1), r2 = splat(row2);
        const F spanStart = splat(static_cast<f32>(xFirst)), spanEnd = splat(static_cast<f32>(xLast) + 1.0f);
        u32 *const colorRow = planes.color + static_cast<size_t>(y) * static_cast<size_t>(planes.stride);
        f32 *const depthRow = planes.depth + static_cast<size_t>(y) * static_cast<size_t>(planes.stride);

        //! Lane-aligned chunks: the stride is a multiple of kLanes, so one never crosses a row.
        for (i32 x = xFirst & ~(kLanes - 1); x <= xLast; x += kLanes)
        {
            const F px = splat(static_cast<f32>(x)) + centers;
            const F w0 = r0 - dy0 * (px - ox0);
            const F w1 = r1 - dy1 * (px - ox1);
            const F w2 = r2 - dy2 * (px - ox2);
            M mask = (px > spanStart) & (px < spanEnd) & inside(w0, tl0) & inside(w1, tl1) & inside(w2, tl2);
            if (!bits(mask))
                continue;

            const F b1 = w1 * invArea2, b2 = w2 * invArea2;
            const F z = madd(b2, dz2, madd(b1, dz1, z0));
            u32 *const color = colorRow + x;
            f32 *const depth = depthRow + x;
            F storedDepth = zero;
            if (planes.depthTest)
            {
                storedDepth = load(depth);
                mask = mask & (Blended ? z <= storedDepth : z < storedDepth);
                if (!bits(mask))
                    continue;
            }

            Rgba source = flat;
            if constexpr (S != Shade::Flat)
            {
                F w = one;
                if constexpr (Perspective)
                {
                    const F invW = madd(b2, diw2, madd(b1, diw1, iw0));
                    mask = mask & (invW > zero);
                    if (!bits(mask))
                        continue;
                    w = one / invW;
                }
                Rgba vertex{madd(b2, dc2.r, madd(b1, dc1.r, c0.r)), madd(b2, dc2.g, madd(b1, dc1.g, c0.g)),
                            madd(b2, dc2.b, madd(b1, dc1.b, c0.b)), madd(b2, dc2.a, madd(b1, dc1.a, c0.a))};
                if constexpr (Perspective)
                    vertex = {vertex.r * w, vertex.g * w, vertex.b * w, vertex.a * w};
                vertex = {clamp01(vertex.r), clamp01(vertex.g), clamp01(vertex.b), clamp01(vertex.a)};

                if constexpr (S == Shade::Vertex)
                    source = {vertex.r * factor.r, vertex.g * factor.g, vertex.b * factor.b, vertex.a * factor.a};
                else
                {
                    F u = madd(b2, du2, madd(b1, du1, u0)), v = madd(b2, dv2, madd(b1, dv1, v0));
                    if constexpr (Perspective)
                    {
                        u = u * w;
                        v = v * w;
                    }
                    //! Texels are UNORM, read as linear the way the GPU backends sample them.
                    const Texture &texture = *s.texture;
                    if constexpr (S == Shade::Texture)
                    {
                        const Footprint f = footprint(texture.data.data(), texture, u, v, bits(mask));
                        source = {vertex.r * filtered<16>(f) * byteScale, vertex.g * filtered<8>(f) * byteScale,
                                  vertex.b * filtered<0>(f) * byteScale, vertex.a * filtered<24>(f) * byteScale};
                    }
                    else
                    {
                        const Footprint f = footprint(texture.coverage.data(), texture, u, v, bits(mask));
                        source = {vertex.r, vertex.g, vertex.b, vertex.a * filtered<0>(f) * byteScale};
                    }
                }
            }

            const I stored = load(color);
            if constexpr (Blended)
            {
                mask = mask & (source.a > zero);
                const u32 active = bits(mask);
                if (!active)
                    continue;
                const Rgba background = decode(stored, active);
                const F keep = one - source.a;
                const I blended = encode(
                    {madd(source.r, source.a, background.r * keep), madd(source.g, source.a, background.g * keep),
                     madd(source.b, source.a, background.b * keep), madd(background.a, keep, source.a)},
                    active);
                store(color, select(mask, blended, stored));
            }
            else
            {
                const I packed = S == Shade::Flat ? flatPacked : encode(source, bits(mask));
                store(color, bits(mask) == kAllLanes ? packed : select(mask, packed, stored));
                if (planes.depthTest)
                    store(depth, select(mask, z, storedDepth));
            }
        }
    }
}

template <bool Blended>
void rasterizeShaded(const Setup &s, const Planes &planes, Shade shade, bool perspective) noexcept
{
    switch (shade)
    {
    case Shade::Flat:
        return rasterize<Blended, Shade::Flat, false>(s, planes);
    case Shade::Vertex:
        return perspective ? rasterize<Blended, Shade::Vertex, true>(s, planes)
                           : rasterize<Blended, Shade::Vertex, false>(s, planes);
    case Shade::Texture:
        return perspective ? rasterize<Blended, Shade::Texture, true>(s, planes)
                           : rasterize<Blended, Shade::Texture, false>(s, planes);
    case Shade::Coverage:
        return perspective ? rasterize<Blended, Shade::Coverage, true>(s, planes)
                           : rasterize<Blended, Shade::Coverage, false>(s, planes);
    }
}

} // namespace

u32 CpuFrameBufferManager::packLinearColor(const glm::vec4 &color) noexcept
{
    return packLinear(glm::clamp(color, 0.0f, 1.0f));
}

void CpuFrameBufferManager::rasterizeTriangle(const ScreenTriangle &triangle, const Texture *texture, RasterMode mode,
                                              i32 yStart, i32 yEnd, bool depthCleared)
{
    static_assert(kRowAlignment % kLanes == 0, "a chunk must not straddle two rows");
    //! queueTriangle() rejected degenerate and nonfinite triangles once, not once per band.
    const ScreenVertex &v0 = triangle.v0, &v1 = triangle.v1, &v2 = triangle.v2;
    f32 area2 = signedArea2(v0, v1, v2);

    const f32 minX = std::min({v0.x, v1.x, v2.x}), maxX = std::max({v0.x, v1.x, v2.x});
    const f32 minY = std::min({v0.y, v1.y, v2.y}), maxY = std::max({v0.y, v1.y, v2.y});
    if (minX >= static_cast<f32>(settings.width) || maxX < 0 || minY >= static_cast<f32>(yEnd) ||
        maxY < static_cast<f32>(yStart))
        return;

    Setup s{};
    // Clamp before converting: offscreen screen-space batches may exceed the integer range.
    s.xmin = static_cast<i32>(std::max(0.0f, std::floor(minX)));
    s.xmax = static_cast<i32>(std::min(static_cast<f32>(settings.width - 1), std::ceil(maxX)));
    s.ymin = static_cast<i32>(std::max(static_cast<f32>(yStart), std::floor(minY)));
    s.ymax = static_cast<i32>(std::min(static_cast<f32>(yEnd - 1), std::ceil(maxY)));
    //! Narrow boxes keep the plain scan: there a row's span costs more than it skips.
    s.walkSpans = s.xmax - s.xmin >= 16;

    const ScreenVertex *p0 = &v0, *p1 = &v1, *p2 = &v2;
    if (area2 < 0)
    {
        std::swap(p1, p2);
        area2 = -area2;
    }
    s.invArea2 = 1.0f / area2;
    // A shared edge belongs to one triangle, so transparent quads have no diagonal seam.
    s.edges[0] = {p1->x, p1->y, p2->x - p1->x, p2->y - p1->y, isTopLeftEdge(*p1, *p2)};
    s.edges[1] = {p2->x, p2->y, p0->x - p2->x, p0->y - p2->y, isTopLeftEdge(*p2, *p0)};
    s.edges[2] = {p0->x, p0->y, p1->x - p0->x, p1->y - p0->y, isTopLeftEdge(*p0, *p1)};

    //! One mip level for the whole triangle, from its texel-to-pixel area ratio: the same in every band, so
    //! there is no seam. shortcut: no per-pixel LOD or blend between levels; a steep floor shows the level step.
    if (texture && texture->levels > 1)
    {
        const glm::vec2 size{static_cast<f32>(texture->width), static_cast<f32>(texture->height)};
        const glm::vec2 a = (p1->uv - p0->uv) * size, b = (p2->uv - p0->uv) * size;
        const f32 ratio = std::abs(a.x * b.y - a.y * b.x) / area2;
        if (ratio > 1.0f && std::isfinite(ratio))
            texture += std::min(static_cast<u32>(std::lround(0.5f * std::log2(ratio))), texture->levels - 1);
    }

    // A 1x1 texture samples to its one texel everywhere: a constant factor rather than a fetch.
    s.factor = glm::vec4{1.0f};
    const Texture *sampled = nullptr;
    if (texture)
    {
        if (texture->width <= 0 || texture->height <= 0)
            s.factor = glm::vec4{0.0f};
        else if (texture->width == 1 && texture->height == 1)
        {
            const u32 texel = texture->texelClamped(0, 0);
            s.factor = glm::vec4{(texel >> 16) & 255u, (texel >> 8) & 255u, texel & 255u, texel >> 24} / 255.0f;
        }
        else
            sampled = texture;
    }
    s.texture = sampled;

    const bool flat = !sampled && p0->color == p1->color && p1->color == p2->color;
    const Shade shade = flat                        ? Shade::Flat
                        : !sampled                  ? Shade::Vertex
                        : sampled->coverage.empty() ? Shade::Texture
                                                    : Shade::Coverage;
    //! Screen-space batches are affine and skip the per-pixel reciprocal.
    const bool perspective = !(p0->invW == 1 && p1->invW == 1 && p2->invW == 1);

    s.z0 = p0->z;
    s.dz1 = p1->z - p0->z;
    s.dz2 = p2->z - p0->z;
    if (shade == Shade::Flat)
    {
        //! Most of a canvas or UI frame: nothing per pixel but depth and one color.
        s.flat = glm::clamp(p0->color, 0.0f, 1.0f) * s.factor;
        s.flatPacked = mode == RasterMode::Scene ? packLinear(s.flat) : 0;
    }
    else
    {
        s.iw0 = p0->invW;
        s.diw1 = p1->invW - p0->invW;
        s.diw2 = p2->invW - p0->invW;
        const glm::vec4 c0 = p0->color * p0->invW, c1 = p1->color * p1->invW, c2 = p2->color * p2->invW;
        s.c0 = c0;
        s.dc1 = c1 - c0;
        s.dc2 = c2 - c0;
        const glm::vec2 uv0 = p0->uv * p0->invW, uv1 = p1->uv * p1->invW, uv2 = p2->uv * p2->invW;
        s.uv0 = uv0;
        s.duv1 = uv1 - uv0;
        s.duv2 = uv2 - uv0;
    }

    //! Screen UI and canvas content: nothing in the band to test against, so no depth plane traffic.
    const bool depthPasses = depthCleared && mode == RasterMode::Batch && v0.z == v1.z && v1.z == v2.z && v0.z <= 1.0f;
    const Planes planes{_color.data(), _depth.data(), _stride, settings.useDepthBuffer && !depthPasses};
    if (mode == RasterMode::Batch)
        rasterizeShaded<true>(s, planes, shade, perspective);
    else
        rasterizeShaded<false>(s, planes, shade, perspective);
}

} // namespace aura3d::cpu
