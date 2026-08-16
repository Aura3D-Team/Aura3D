#ifndef ORGLOGO_PALETTE_H
#define ORGLOGO_PALETTE_H

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Noise.h"

/**
 * @file Palette.h
 * @brief The logo's colour vocabulary and the RGBA8 encodings every layer
 *        writes through.
 *
 * Two encodings live here and the distinction matters:
 *
 *  - encodeOpaque() is for the backdrop, which is drawn as solid geometry and
 *    simply *is* the colour it stores.
 *  - encodeGlow() is for everything that composites *over* the backdrop -- the
 *    stars and the flame. The engine's 2D overlay blends source-over
 *    (@c out = src * a + dst * (1 - a)) on every backend, which darkens what is
 *    already on screen; light does not do that. encodeGlow() folds the
 *    intensity into the alpha channel and normalises the colour by it, so
 *    source-over evaluates to @c colour + dst * (1 - a) instead: additive over
 *    the dark field, and saturating rather than blowing out where the layers
 *    pile up. That single trick is what lets a glow look like a glow without
 *    an additive blend mode the interface does not expose.
 */
namespace orglogo {

/// Linear-ish HDR colour. Values above 1 are expected and are tone-mapped down
/// at encode time rather than being clamped mid-computation.
struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

[[nodiscard]] constexpr Rgb operator+(const Rgb& a, const Rgb& b) noexcept
{
    return {a.r + b.r, a.g + b.g, a.b + b.b};
}

[[nodiscard]] constexpr Rgb operator*(const Rgb& c, float s) noexcept
{
    return {c.r * s, c.g * s, c.b * s};
}

[[nodiscard]] constexpr Rgb operator*(const Rgb& a, const Rgb& b) noexcept
{
    return {a.r * b.r, a.g * b.g, a.b * b.b};
}

[[nodiscard]] constexpr Rgb mix(const Rgb& a, const Rgb& b, float t) noexcept
{
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
}

[[nodiscard]] constexpr float peakChannel(const Rgb& c) noexcept
{
    return std::max(c.r, std::max(c.g, c.b));
}

/// Tightly packed output texel, in the RGBA order every backend's
/// createTextureFromPixels()/updateTextureRegion() expects.
struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

inline constexpr Rgb kStarWhite{1.00f, 1.00f, 1.00f};
inline constexpr Rgb kStarBlue {0.75f, 0.85f, 1.00f};
inline constexpr Rgb kStarWarm {1.00f, 0.92f, 0.80f};

/**
 * @brief Blue-flame colour ramp: heat in [0,1] -> flame colour.
 *
 * Red stays below green below blue at every stop, so the flames read as blue
 * throughout -- and the low end starts at a lit blue rather than near-black, so
 * even the coolest tips glow instead of guttering out. A temperature ramp would
 * wash out to white at its hot end; this one only reaches pale cyan.
 */
[[nodiscard]] inline Rgb blueFlame(float heat) noexcept
{
    heat = clamp01(heat);

    constexpr std::array<float, 5> stops{0.00f, 0.35f, 0.65f, 0.90f, 1.00f};
    constexpr std::array<Rgb, 5> colors{
        Rgb{0.02f, 0.12f, 0.34f},
        Rgb{0.10f, 0.40f, 0.88f},
        Rgb{0.32f, 0.70f, 1.00f},
        Rgb{0.58f, 0.88f, 1.00f},
        Rgb{0.84f, 0.97f, 1.00f},
    };

    for (std::size_t i = 0; i + 1 < stops.size(); ++i)
    {
        if (heat <= stops[i + 1])
        {
            const float t = (heat - stops[i]) / (stops[i + 1] - stops[i]);
            return mix(colors[i], colors[i + 1], t);
        }
    }

    return colors.back();
}

namespace detail {

//! Resolution of the two runtime lookup tables. 1024 entries put the tone
//! curve's quantisation an order of magnitude below an 8-bit output step, so
//! the table is indistinguishable from calling std::pow() per channel.
inline constexpr int kTableSize = 1024;

/**
 * @brief Display tone curve, @c pow(c, 0.85), tabulated.
 *
 * Called three times per texel across roughly a third of a million texels
 * every frame; std::pow() at that rate costs more than the entire flame
 * simulation feeding it. A namespace-scope inline table -- rather than a
 * function-local static -- also keeps the guard variable check out of the
 * inner loop.
 */
inline const std::array<float, kTableSize> kToneCurve = [] {
    std::array<float, kTableSize> table{};
    for (int i = 0; i < kTableSize; ++i)
    {
        const float x = static_cast<float>(i) / static_cast<float>(kTableSize - 1);
        table[static_cast<std::size_t>(i)] = std::pow(x, 0.85f);
    }
    return table;
}();

//! blueFlame() tabulated, for the same reason: the ramp is a branchy search
//! that would otherwise run once per flame texel.
inline const std::array<Rgb, kTableSize> kFlameRamp = [] {
    std::array<Rgb, kTableSize> table{};
    for (int i = 0; i < kTableSize; ++i)
    {
        const float x = static_cast<float>(i) / static_cast<float>(kTableSize - 1);
        table[static_cast<std::size_t>(i)] = blueFlame(x);
    }
    return table;
}();

[[nodiscard]] inline std::size_t tableIndex(float x) noexcept
{
    return static_cast<std::size_t>(clamp01(x) * static_cast<float>(kTableSize - 1) + 0.5f);
}

[[nodiscard]] inline float tone(float x) noexcept
{
    return kToneCurve[tableIndex(x)];
}

} // namespace detail

/// Tabulated blueFlame(): identical ramp, one indexed load.
[[nodiscard]] inline Rgb flameRamp(float heat) noexcept
{
    return detail::kFlameRamp[detail::tableIndex(heat)];
}

/// Encodes a fully covering surface: tone-mapped colour, alpha 255.
[[nodiscard]] inline Rgba8 encodeOpaque(const Rgb& c) noexcept
{
    const auto quantise = [](float v) noexcept {
        return static_cast<std::uint8_t>(detail::tone(v) * 255.0f + 0.5f);
    };

    return {quantise(c.r), quantise(c.g), quantise(c.b), 255};
}

/**
 * @brief Encodes emitted light for the source-over 2D overlay.
 *
 * Stores the tone-mapped colour normalised by its peak channel, with that peak
 * in alpha. Substituting into the blend the engine performs,
 * @c out = (colour / peak) * peak + dst * (1 - peak) = @c colour + dst * (1 - peak):
 * the layer adds its light rather than replacing what is under it, while a
 * fully saturated texel (peak 1) still covers opaquely -- which is exactly what
 * the flame's solid core needs.
 *
 * @param c Emitted colour; may exceed 1 per channel.
 */
[[nodiscard]] inline Rgba8 encodeGlow(const Rgb& c) noexcept
{
    const Rgb toned{detail::tone(c.r), detail::tone(c.g), detail::tone(c.b)};
    const float peak = peakChannel(toned);

    //! Below half a quantisation step there is no light to carry, and the
    //! normalisation below would divide by ~0.
    if (peak <= (0.5f / 255.0f))
        return {};

    const float normalise = 255.0f / peak;
    return {static_cast<std::uint8_t>(toned.r * normalise + 0.5f),
            static_cast<std::uint8_t>(toned.g * normalise + 0.5f),
            static_cast<std::uint8_t>(toned.b * normalise + 0.5f),
            static_cast<std::uint8_t>(peak * 255.0f + 0.5f)};
}

/**
 * @brief Encodes a white coverage mask: the shape in alpha, white in RGB.
 *
 * The star sprites are stored this way so a single atlas serves every star:
 * the per-star tint and brightness arrive as the quad's vertex colour, which
 * the overlay shader multiplies in (@c texel * @c vertexColor). No tone curve
 * here -- @p coverage is a geometric falloff, not a radiance.
 */
[[nodiscard]] inline Rgba8 encodeWhiteMask(float coverage) noexcept
{
    return {255, 255, 255,
            static_cast<std::uint8_t>(clamp01(coverage) * 255.0f + 0.5f)};
}

} // namespace orglogo

#endif // ORGLOGO_PALETTE_H
