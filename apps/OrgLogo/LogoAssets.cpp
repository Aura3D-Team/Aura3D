#include "LogoAssets.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Palette.h"

namespace orglogo {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;

/// Stores one RGBA8 texel. Explicit rather than reinterpret_cast'ing a
/// std::uint32_t over the buffer: the byte order is part of the interface
/// contract with createTextureFromPixels(), not the host's endianness.
inline void store(std::uint8_t* dst, const Rgba8& texel) noexcept
{
    dst[0] = texel.r;
    dst[1] = texel.g;
    dst[2] = texel.b;
    dst[3] = texel.a;
}

/**
 * @brief atan2() to within ~0.005 rad, for about a tenth of the cost.
 *
 * The flame's shine rays need an angle at every coarse sample, tens of
 * thousands of times per frame. The error is three orders of magnitude below a
 * ray's angular width, so it is invisible, and unlike std::atan2 this compiles
 * to straight-line arithmetic.
 */
[[nodiscard]] float fastAtan2(float y, float x) noexcept
{
    const float ax = std::fabs(x);
    const float ay = std::fabs(y);
    const float small = std::min(ax, ay);
    const float large = std::max(ax, ay);

    //! The bias keeps the ratio finite at the origin, where the angle is
    //! undefined anyway and the caller's radial terms are zero.
    const float ratio = small / (large + 1e-20f);
    const float sq = ratio * ratio;

    float angle = ((-0.0464964749f * sq + 0.15931422f) * sq - 0.327622764f) * sq * ratio + ratio;

    if (ay > ax)     angle = kPi * 0.5f - angle;
    if (x < 0.0f)    angle = kPi - angle;
    if (y < 0.0f)    angle = -angle;

    return angle;
}

////////////////////////////////////
//// Backdrop detail
////////////////////////////////////

//! Cells across the short axis of the dust grid. Every cell holds at most one
//! faint star, which is what makes the dust evaluable per pixel -- and
//! therefore per thread -- instead of splatted into a shared buffer.
constexpr float kDustCellsPerUnit = 42.0f;

//! Fraction of cells that actually hold a star.
constexpr float kDustDensity = 0.5f;

/**
 * @brief Faint, evenly spread background stars, sampled at one point.
 *
 * A jittered grid: each cell's hash decides whether it holds a star, where in
 * the cell it sits and how bright it is. Scanning the 3x3 neighbourhood covers
 * every star whose falloff can reach @p gx, @p gy.
 *
 * @param gx,gy Position in cell units.
 * @param seed Lattice offset, so a different seed is a different sky.
 * @return Accumulated intensity, pre-tint.
 */
[[nodiscard]] float dustAt(float gx, float gy, std::int32_t seed) noexcept
{
    const std::int32_t cx = floorToInt(gx);
    const std::int32_t cy = floorToInt(gy);

    float total = 0.0f;

    for (std::int32_t oy = -1; oy <= 1; ++oy)
    {
        for (std::int32_t ox = -1; ox <= 1; ++ox)
        {
            const std::int32_t ix = cx + ox;
            const std::int32_t iy = cy + oy;

            const float presence = hash2(ix + seed, iy - seed);
            if (presence > kDustDensity)
                continue;

            const float jitterX = hash2(ix * 7919 + seed, iy);
            const float jitterY = hash2(ix, iy * 7919 - seed);

            const float dx = gx - (static_cast<float>(ix) + jitterX);
            const float dy = gy - (static_cast<float>(iy) + jitterY);
            const float distSq = dx * dx + dy * dy;

            //! Sigma is a fraction of a cell, so a dust star is a pixel or two
            //! wide whatever the resolution.
            const float sigma = 0.018f + presence * 0.055f;
            const float reachSq = 9.0f * sigma * sigma;
            if (distSq > reachSq)
                continue;

            const float brightness = 0.10f + presence * 0.60f;
            total += brightness * std::exp(-distSq / (2.0f * sigma * sigma));
        }
    }

    return total;
}

} // namespace

////////////////////////////////////
//// Backdrop
////////////////////////////////////

Image bakeBackdrop(const BackdropDesc& desc, const aura3d::JobSystem& jobs)
{
    Image image;
    if (desc.width <= 0 || desc.height <= 0 || desc.unit <= 0.0f)
        return image;

    image.width = desc.width;
    image.height = desc.height;
    image.pixels.resize(static_cast<std::size_t>(desc.width) * desc.height * 4u);

    const float width = static_cast<float>(desc.width);
    const float height = static_cast<float>(desc.height);
    const float halfW = width * 0.5f;
    const float halfH = height * 0.5f;

    //! Nebula coordinates are normalised by the *short* axis on both axes, so
    //! the noise stays isotropic instead of being stretched with the window.
    const float invUnit = 1.0f / desc.unit;
    const float dustScale = kDustCellsPerUnit * invUnit;
    const float haloRadius = std::max(desc.haloRadius, 1.0f);
    //! Kept small on purpose: it is added to lattice coordinates, and a seed
    //! near INT32_MAX would overflow that addition.
    const auto seed = static_cast<std::int32_t>(desc.seed % 8192u);

    std::uint8_t* const pixels = image.pixels.data();

    jobs.dispatch(desc.height, [&](int rowBegin, int rowEnd) noexcept {
        for (int y = rowBegin; y < rowEnd; ++y)
        {
            const float py = static_cast<float>(y) + 0.5f;
            const float cy = (py - halfH) * invUnit;

            std::uint8_t* row = pixels + static_cast<std::size_t>(y) * desc.width * 4u;

            for (int x = 0; x < desc.width; ++x)
            {
                const float px = static_cast<float>(x) + 0.5f;
                const float cx = (px - halfW) * invUnit;
                const float dist = std::sqrt(cx * cx + cy * cy);

                /*
                 * Faint, unevenly distributed cool shine. Two fractal fields at
                 * different scales, thresholded and squared: the threshold is
                 * what keeps most of the sky genuinely black instead of a flat
                 * grey haze.
                 */
                const float base = fbm2(cx * 3.0f + 2.0f, cy * 3.0f - 1.0f, 4);
                const float broad = fbm2(cx * 1.3f + 5.0f, cy * 1.3f - 3.0f, 3);
                const float shine = clamp01(base * 0.6f + broad * 0.5f - 0.35f);
                const float glow = shine * shine;

                //! A second, larger structure with a colder tint gives the
                //! field some sense of depth behind the stars.
                const float wispField = fbm2(cx * 1.9f - 7.0f, cy * 1.9f + 4.0f, 5);
                const float wisp = clamp01(wispField * 1.18f - 0.46f);

                Rgb color{glow * 0.075f, glow * 0.140f, glow * 0.275f};
                color = color + Rgb{0.030f, 0.075f, 0.170f} * (wisp * wisp);

                //! Dust: baked, so it neither twinkles nor rotates. It reads as
                //! sky texture rather than as stars, which is the point -- the
                //! animated field above it is what the eye tracks.
                const float dust = dustAt(px * dustScale, py * dustScale, seed);
                color = color + Rgb{0.72f, 0.80f, 1.00f} * (dust * 0.55f);

                //! Vignette, applied before the halo so the corners darken
                //! without dimming the core glow.
                const float vignette = 1.0f - 0.45f * smoothstep(0.40f, 1.05f, dist);
                color = color * vignette;

                /*
                 * Outer half of the orb's halo. The flame quad only covers the
                 * middle of the screen; this carries its light the rest of the
                 * way out, and because the flame's own contribution fades to
                 * zero before that quad's edge, the two meet seamlessly.
                 */
                const float halo = std::exp(-dist * desc.unit / haloRadius);
                color = color + Rgb{0.12f, 0.38f, 0.86f} * (halo * 0.62f)
                              + Rgb{0.34f, 0.68f, 1.00f} * (halo * halo * 0.42f);

                store(row + static_cast<std::size_t>(x) * 4u, encodeOpaque(color));
            }
        }
    });

    return image;
}

////////////////////////////////////
//// Star sprites
////////////////////////////////////

namespace {

constexpr int kSpriteTile = 64;              //! Star tiles are square.
constexpr int kCometWidth = kSpriteTile * 2; //! Meteors are twice as long.
constexpr int kAtlasWidth = kSpriteTile * 2 + kCometWidth;
constexpr int kAtlasHeight = kSpriteTile;

/// Half-texel inset, so a quad's edge sample cannot bleed into the next tile.
[[nodiscard]] UvRect tileUv(int x0, int tileWidth) noexcept
{
    constexpr float inset = 0.5f;
    return {(static_cast<float>(x0) + inset) / static_cast<float>(kAtlasWidth),
            inset / static_cast<float>(kAtlasHeight),
            (static_cast<float>(x0 + tileWidth) - inset) / static_cast<float>(kAtlasWidth),
            (static_cast<float>(kAtlasHeight) - inset) / static_cast<float>(kAtlasHeight)};
}

/// Round star: a tight core inside two decreasingly tight halos.
[[nodiscard]] float softStarCoverage(float dx, float dy) noexcept
{
    const float distSq = dx * dx + dy * dy;
    const float dist = std::sqrt(distSq);

    const float core = std::exp(-distSq / (2.0f * 0.055f * 0.055f));
    const float inner = std::exp(-dist / 0.085f) * 0.42f;
    const float outer = std::exp(-dist / 0.200f) * 0.16f;

    return clamp01(core * 1.25f + inner + outer);
}

/**
 * @brief Bright star: core, four diffraction spikes and two faint diagonals.
 *
 * Spikes are deliberately a few texels wide rather than hairline. The software
 * rasteriser samples textures nearest-neighbour, so a one-texel spike drops out
 * of alternate rows once the sprite is minified and the star ends up looking
 * dashed.
 */
[[nodiscard]] float spikedStarCoverage(float dx, float dy) noexcept
{
    const float distSq = dx * dx + dy * dy;
    const float dist = std::sqrt(distSq);

    const float core = std::exp(-distSq / (2.0f * 0.048f * 0.048f));

    const float ax = std::fabs(dx);
    const float ay = std::fabs(dy);
    const float horizontal = std::exp(-ay / 0.022f) * std::exp(-ax / 0.150f);
    const float vertical = std::exp(-ax / 0.022f) * std::exp(-ay / 0.150f);

    //! 45-degree pair, at a fraction of the intensity: real optics put most of
    //! the energy in the primary cross.
    const float rx = (dx + dy) * 0.70710678f;
    const float ry = (dx - dy) * 0.70710678f;
    const float diagonalA = std::exp(-std::fabs(ry) / 0.020f) * std::exp(-std::fabs(rx) / 0.090f);
    const float diagonalB = std::exp(-std::fabs(rx) / 0.020f) * std::exp(-std::fabs(ry) / 0.090f);

    const float halo = std::exp(-dist / 0.075f) * 0.34f;

    return clamp01(core * 1.40f
                 + (horizontal + vertical) * 0.80f
                 + (diagonalA + diagonalB) * 0.34f
                 + halo);
}

/// Meteor: head near the +U edge, tail widening and fading toward -U.
[[nodiscard]] float cometCoverage(float u, float v) noexcept
{
    constexpr float headU = 0.88f;

    const float dy = v - 0.5f;
    const float behind = headU - u; //! Positive along the tail.

    const float head = std::exp(-((u - headU) * (u - headU) / (2.0f * 0.030f * 0.030f)
                                + dy * dy / (2.0f * 0.050f * 0.050f)));

    float tail = 0.0f;
    if (behind > 0.0f)
    {
        const float width = 0.026f + 0.070f * behind;
        const float along = std::exp(-behind / 0.260f) * (1.0f - smoothstep(0.55f, 0.92f, behind));
        tail = along * std::exp(-(dy * dy) / (2.0f * width * width));
    }

    return clamp01(head * 1.30f + tail * 0.90f);
}

} // namespace

StarAtlas bakeStarAtlas()
{
    StarAtlas atlas;
    atlas.image.width = kAtlasWidth;
    atlas.image.height = kAtlasHeight;
    atlas.image.pixels.resize(static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * 4u);

    atlas.soft = tileUv(0, kSpriteTile);
    atlas.spiked = tileUv(kSpriteTile, kSpriteTile);
    atlas.comet = tileUv(kSpriteTile * 2, kCometWidth);

    std::uint8_t* const pixels = atlas.image.pixels.data();

    for (int y = 0; y < kAtlasHeight; ++y)
    {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kAtlasHeight);
        std::uint8_t* row = pixels + static_cast<std::size_t>(y) * kAtlasWidth * 4u;

        for (int x = 0; x < kAtlasWidth; ++x)
        {
            float coverage = 0.0f;

            if (x < kSpriteTile)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kSpriteTile);
                coverage = softStarCoverage(u - 0.5f, v - 0.5f);
            }
            else if (x < kSpriteTile * 2)
            {
                const float u = (static_cast<float>(x - kSpriteTile) + 0.5f)
                              / static_cast<float>(kSpriteTile);
                coverage = spikedStarCoverage(u - 0.5f, v - 0.5f);
            }
            else
            {
                const float u = (static_cast<float>(x - kSpriteTile * 2) + 0.5f)
                              / static_cast<float>(kCometWidth);
                coverage = cometCoverage(u, v);
            }

            store(row + static_cast<std::size_t>(x) * 4u, encodeWhiteMask(coverage));
        }
    }

    return atlas;
}

////////////////////////////////////
//// Flame field
////////////////////////////////////

namespace {

//! Output texels per coarse sample, per axis. Two is the sweet spot: a 4x cut
//! in fractal-noise cost, and the detail octave added back at full resolution
//! restores everything the interpolation smooths away.
constexpr int kCoarseStep = 2;

constexpr int kRayTableSize = 2048;
constexpr int kRadialTableSize = 1024;

//! Distance from the quad's centre to its corner, in UV units.
constexpr float kMaxRadiusUv = 0.70711f;

/*
 * The flame field is not sampled in the quad's own coordinates. It is sampled
 * on a cylinder: the two lateral axes trace a circle of radius kRingRadius as
 * the angle sweeps, and the third axis carries distance from the centre. That
 * parameterisation is what makes the flow genuinely radial -- advecting along
 * the third axis moves the whole pattern outward, and new structure is born at
 * the rim -- while staying seamless across the +/-pi wrap that any polar
 * mapping of a Cartesian noise field would tear along.
 *
 * Advecting in the quad's own coordinates instead, by subtracting a radial
 * unit vector, puts the sample at radius (scale * r - rise). That expression
 * turns negative near the centre, folding the field through the origin and
 * printing smooth concentric arcs over the flames.
 */

//! Noise-space radius of the cylinder. The circumference, 2*pi times this, is
//! how many tongues fit around the rim.
constexpr float kRingRadius = 3.2f;

//! Noise units per UV unit along the radial axis. Deliberately far smaller
//! than the angular scale: that anisotropy stretches every feature radially,
//! which is the difference between tongues and a cloud wrapped round a ball.
constexpr float kRadialScale = 6.0f;

//! Displacement the warp field applies, in noise units. Near the feature size,
//! which is what curls the tongues instead of merely nudging them.
constexpr float kWarpStrength = 1.10f;

/**
 * @struct FlameCoords
 * @brief A quad texel's position in the flame's noise space.
 */
struct FlameCoords {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/// Maps a centre-relative texel onto the flame cylinder. One definition, used
/// by both the coarse field and the detail octave that refines it.
[[nodiscard]] inline FlameCoords flameCoords(float dx, float dy, float dist, float rise) noexcept
{
    const float invDist = dist > 1e-5f ? 1.0f / dist : 0.0f;
    return {dx * invDist * kRingRadius,
            dy * invDist * kRingRadius,
            dist * kRadialScale - rise};
}

//! How far past the disc the flames can reach, in UV units.
constexpr float kFlameReach = 0.30f;

//! Where the orb's own light fades out, short of the quad's edge.
constexpr float kEdgeFadeStart = 0.455f;

[[nodiscard]] inline std::size_t radialIndex(float dist) noexcept
{
    const float scaled = dist * (static_cast<float>(kRadialTableSize - 1) / kMaxRadiusUv);
    const int index = static_cast<int>(scaled);
    return static_cast<std::size_t>(std::clamp(index, 0, kRadialTableSize - 1));
}

[[nodiscard]] inline std::size_t angleIndex(float dy, float dx) noexcept
{
    const float angle = fastAtan2(dy, dx) + kPi;
    const int index = static_cast<int>(angle * (static_cast<float>(kRayTableSize) / kTwoPi));
    return static_cast<std::size_t>(std::clamp(index, 0, kRayTableSize - 1));
}

} // namespace

FlameField::FlameField(int resolution, const aura3d::JobSystem& jobs)
    : _resolution(std::max(kCoarseStep, (resolution + kCoarseStep - 1) / kCoarseStep * kCoarseStep)),
      _coarseDim(0),
      _jobs(jobs)
{
    _coarseDim = _resolution / kCoarseStep + 1;

    _pixels.assign(static_cast<std::size_t>(_resolution) * _resolution * 4u, 0u);
    _coarse.assign(static_cast<std::size_t>(_coarseDim) * _coarseDim * 2u, 0.0f);
    _rayTable.assign(kRayTableSize, 0.0f);
    _haloTable.assign(kRadialTableSize, 0.0f);
    _rayFalloff.assign(kRadialTableSize, 0.0f);
}

void FlameField::bake(float time)
{
    /*
     * The core's breath: two incommensurate sines, so the orb never settles
     * into a rhythm the eye can predict. Everything else that pulses is driven
     * from this one value, which keeps the disc, its halo and the star field
     * moving as one body instead of three.
     */
    _pulse = std::sin(time * 0.90f) * 0.60f + std::sin(time * 1.53f + 1.10f) * 0.40f;

    Frame frame;
    frame.time = time;
    frame.discRadius = kDiscRadiusUv * (1.0f + 0.035f * _pulse);
    frame.hotGain = 0.86f + 0.14f * _pulse + 0.05f * std::sin(time * 5.70f);
    frame.haloGain = 0.92f + 0.20f * _pulse;
    frame.rayGain = 0.85f + 0.28f * std::sin(time * 0.70f + 2.0f);
    frame.rise = time * 0.90f;

    _buildRayTable(frame);
    _buildRadialTables(frame);

    _jobs.dispatch(_coarseDim, [this, &frame](int begin, int end) noexcept {
        _bakeCoarseBand(frame, begin, end);
    });

    _jobs.dispatch(_resolution, [this, &frame](int begin, int end) noexcept {
        _shadeBand(frame, begin, end);
    });
}

void FlameField::_buildRayTable(const Frame& frame) noexcept
{
    //! Rays turn slowly, and the three harmonics drift against each other, so
    //! the starburst reshapes itself instead of spinning rigidly.
    const float spin = frame.time * 0.085f;

    for (int i = 0; i < kRayTableSize; ++i)
    {
        const float angle = -kPi + (static_cast<float>(i) + 0.5f) * (kTwoPi / kRayTableSize);

        const float pattern = 0.50f
                            + 0.44f * std::sin(angle * 13.0f + spin)
                            + 0.30f * std::sin(angle * 19.0f - spin * 1.7f + 1.3f)
                            + 0.22f * std::sin(angle * 5.0f + spin * 0.55f - 0.7f);

        //! Fourth power turns a smooth wave into narrow, well-separated rays.
        const float shaped = clamp01(pattern * 0.55f);
        const float squared = shaped * shaped;
        _rayTable[static_cast<std::size_t>(i)] = squared * squared;
    }
}

void FlameField::_buildRadialTables(const Frame& frame) noexcept
{
    const float step = kMaxRadiusUv / static_cast<float>(kRadialTableSize - 1);

    for (int i = 0; i < kRadialTableSize; ++i)
    {
        const float dist = static_cast<float>(i) * step;

        //! Guarantees the quad's border contributes nothing, so its edge never
        //! shows against the backdrop.
        const float edge = 1.0f - smoothstep(kEdgeFadeStart, FlameField::kGlowLimitUv - 0.002f, dist);

        _haloTable[static_cast<std::size_t>(i)] =
            std::exp(-dist / 0.150f) * frame.haloGain * edge;

        //! Rays start just outside the disc -- inside it they would only wash
        //! out the solid core -- and decay outward.
        const float radial = std::min(1.0f, std::exp(-(dist - frame.discRadius) / 0.100f));
        const float onset = smoothstep(frame.discRadius * 0.92f, frame.discRadius * 1.40f, dist);
        _rayFalloff[static_cast<std::size_t>(i)] = radial * onset * frame.rayGain * edge;
    }
}

void FlameField::_bakeCoarseBand(const Frame& frame, int rowBegin, int rowEnd) noexcept
{
    const float invResolution = 1.0f / static_cast<float>(_resolution);

    for (int j = rowBegin; j < rowEnd; ++j)
    {
        const float py = static_cast<float>(j * kCoarseStep) + 0.5f;
        const float dy = py * invResolution - 0.5f;

        float* row = _coarse.data() + static_cast<std::size_t>(j) * _coarseDim * 2u;

        for (int i = 0; i < _coarseDim; ++i)
        {
            const float px = static_cast<float>(i * kCoarseStep) + 0.5f;
            const float dx = px * invResolution - 0.5f;

            const float dist = std::sqrt(dx * dx + dy * dy);
            const FlameCoords q = flameCoords(dx, dy, dist, frame.rise);

            //! Domain warp: a low-frequency field displacing a higher one, the
            //! standard recipe for organic rather than blobby turbulence. Its
            //! own slow drift in time is what keeps the tongues reshaping
            //! themselves instead of merely streaming outward unchanged.
            const float warp = fbm3(q.x * 0.70f + 11.3f, q.y * 0.70f - 4.7f,
                                    q.z * 0.55f + frame.time * 0.30f, 3);
            const float offset = (warp - 0.5f) * kWarpStrength;
            const float turbulence = fbm3(q.x + offset, q.y + offset, q.z + offset * 0.7f, 4);

            row[static_cast<std::size_t>(i) * 2u + 0u] = turbulence;
            row[static_cast<std::size_t>(i) * 2u + 1u] = _rayTable[angleIndex(dy, dx)];
        }
    }
}

void FlameField::_shadeBand(const Frame& frame, int rowBegin, int rowEnd) noexcept
{
    const float invResolution = 1.0f / static_cast<float>(_resolution);
    constexpr float invStep = 1.0f / static_cast<float>(kCoarseStep);

    const float discRadius = frame.discRadius;
    const float hotRadius = discRadius * 0.30f;

    for (int y = rowBegin; y < rowEnd; ++y)
    {
        const float dy = (static_cast<float>(y) + 0.5f) * invResolution - 0.5f;

        //! Coarse row pair for this output row, hoisted out of the inner loop.
        const float gy = static_cast<float>(y) * invStep;
        const auto j0 = static_cast<int>(gy);
        const float fy = gy - static_cast<float>(j0);
        const float* coarseRow0 = _coarse.data() + static_cast<std::size_t>(j0) * _coarseDim * 2u;
        const float* coarseRow1 = coarseRow0 + static_cast<std::size_t>(_coarseDim) * 2u;

        std::uint8_t* row = _pixels.data() + static_cast<std::size_t>(y) * _resolution * 4u;

        for (int x = 0; x < _resolution; ++x)
        {
            std::uint8_t* texel = row + static_cast<std::size_t>(x) * 4u;

            const float dx = (static_cast<float>(x) + 0.5f) * invResolution - 0.5f;
            const float dist = std::sqrt(dx * dx + dy * dy);

            //! The corners of the quad lie outside the glow entirely.
            if (dist >= FlameField::kGlowLimitUv)
            {
                store(texel, Rgba8{});
                continue;
            }

            const float gx = static_cast<float>(x) * invStep;
            const auto i0 = static_cast<int>(gx);
            const float fx = gx - static_cast<float>(i0);
            const std::size_t lo = static_cast<std::size_t>(i0) * 2u;
            const std::size_t hi = lo + 2u;

            const float turbCoarse =
                lerp(lerp(coarseRow0[lo], coarseRow0[hi], fx),
                     lerp(coarseRow1[lo], coarseRow1[hi], fx), fy);
            const float ray =
                lerp(lerp(coarseRow0[lo + 1u], coarseRow0[hi + 1u], fx),
                     lerp(coarseRow1[lo + 1u], coarseRow1[hi + 1u], fx), fy);

            //! One full-resolution octave, restoring the filaments the
            //! half-resolution reconstruction smooths out.
            const FlameCoords q = flameCoords(dx, dy, dist, frame.rise);
            const float detail = valueNoise3(q.x * 2.60f + 31.0f, q.y * 2.60f - 7.0f,
                                             q.z * 2.60f + 5.0f);
            const float turbulence = clamp01(turbCoarse + (detail - 0.5f) * 0.18f);

            //! Solid blue disc -- the "circle" -- deepening toward its rim.
            const float discMask = clamp01((discRadius - dist) / 0.016f);
            const float inner = clamp01(1.0f - dist / discRadius);
            const float innerShaped = inner * std::sqrt(inner); //! pow(inner, 1.5)
            const Rgb discColor{lerp(0.06f, 0.32f, innerShaped),
                                lerp(0.34f, 0.72f, innerShaped),
                                lerp(0.88f, 1.00f, innerShaped)};

            //! Small white-blue hot spot at the very centre.
            const float hotFalloff = clamp01((hotRadius - dist) / hotRadius);
            const float hot = hotFalloff * hotFalloff * frame.hotGain;

            /*
             * Flame tongues. The turbulence is thresholded against a level that
             * rises with distance from the rim, so near the disc almost all of
             * the field burns and further out only its peaks do -- which is
             * what separates a continuous sheet at the base into tongues that
             * taper and break apart at their tips. Shading the band by
             * brightness alone, as a smooth falloff, only ever produces a ring.
             */
            const float band = clamp01((dist - discRadius) / kFlameReach);
            const float threshold = 0.10f + 0.66f * band;
            const float density = clamp01((turbulence - threshold) * 2.6f);
            const float rimBlend = smoothstep(discRadius * 0.88f, discRadius * 1.02f, dist);
            const float tongue = density * rimBlend * (1.0f - band);

            //! Hottest at the base and cooling outward, so the tips stay deep
            //! blue while the rim burns pale cyan.
            const float heat = clamp01(0.14f + (1.0f - band) * 0.40f + density * 0.22f);
            const Rgb flame = flameRamp(heat);
            const float corona = tongue * 0.95f;

            const std::size_t radial = radialIndex(dist);
            const float halo = _haloTable[radial];

            //! Rays are suppressed wherever a tongue already burns: a starburst
            //! reads as light escaping *between* the flames.
            const float shine = ray * _rayFalloff[radial] * (1.0f - 0.75f * tongue);

            Rgb color;
            color.r = discMask * discColor.r * 1.18f + hot * 0.70f
                    + corona * flame.r + halo * 0.20f + shine * 0.30f;
            color.g = discMask * discColor.g * 1.18f + hot * 0.85f
                    + corona * flame.g + halo * 0.48f + shine * 0.66f;
            color.b = discMask * discColor.b * 1.18f + hot * 0.95f
                    + corona * flame.b + halo * 0.92f + shine * 1.00f;

            store(texel, encodeGlow(color));
        }
    }
}

} // namespace orglogo
