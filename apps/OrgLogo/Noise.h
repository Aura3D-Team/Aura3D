#ifndef ORGLOGO_NOISE_H
#define ORGLOGO_NOISE_H

#pragma once

#include <cstdint>

/**
 * @file Noise.h
 * @brief Deterministic, allocation-free noise primitives shared by every layer
 *        of the logo.
 *
 * Everything here is a pure function of its arguments: the same coordinates
 * always yield the same value, on every platform and from any thread. That is
 * what lets the backdrop be baked once, the flame be re-baked every frame from
 * nothing but a timestamp, and both of them be split across worker threads
 * without a single shared byte of state.
 */
namespace orglogo {

[[nodiscard]] constexpr float clamp01(float x) noexcept
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/// Cubic Hermite fade, the classic value-noise interpolant.
[[nodiscard]] constexpr float fade3(float t) noexcept
{
    return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief Quintic fade.
 *
 * Zero first *and* second derivative at the lattice points, unlike fade3().
 * Stacked octaves of value noise expose the second derivative as faint creases
 * along the integer grid, so the fractal sums below use this one and the cheap
 * single-octave lookups use fade3().
 */
[[nodiscard]] constexpr float fade5(float t) noexcept
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

[[nodiscard]] constexpr float smoothstep(float edge0, float edge1, float x) noexcept
{
    const float span = edge1 - edge0;
    if (span == 0.0f)
        return x < edge0 ? 0.0f : 1.0f;

    return fade3(clamp01((x - edge0) / span));
}

/**
 * @brief std::floor() for the noise lattice, as an integer.
 *
 * Truncation rounds toward zero, which is the wrong direction for negative
 * coordinates -- and the flame is sampled in centre-relative space, so half of
 * every field it evaluates is negative. Getting this wrong mirrors the lattice
 * about the origin and puts a visible seam through the middle of the logo.
 */
[[nodiscard]] constexpr std::int32_t floorToInt(float x) noexcept
{
    const auto truncated = static_cast<std::int32_t>(x);
    return (x < static_cast<float>(truncated)) ? truncated - 1 : truncated;
}

/// Hashes a 2D integer lattice point to [0,1). Unsigned wrap-around is
/// well-defined, so the mixing below carries no overflow UB.
[[nodiscard]] constexpr float hash2(std::int32_t x, std::int32_t y) noexcept
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                    + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h) * (1.0f / 4294967296.0f);
}

/// Hashes a 3D integer lattice point to [0,1). One extra mixing round over
/// hash2(): the third axis is time, and a weak avalanche there shows up as the
/// flame pulsing in lockstep across the whole field.
[[nodiscard]] constexpr float hash3(std::int32_t x, std::int32_t y, std::int32_t z) noexcept
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                    + static_cast<std::uint32_t>(y) * 668265263u
                    + static_cast<std::uint32_t>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = (h ^ (h >> 16)) * 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h) * (1.0f / 4294967296.0f);
}

/// Smoothly interpolated 2D value noise, mean 0.5, range [0,1).
[[nodiscard]] constexpr float valueNoise2(float x, float y) noexcept
{
    const std::int32_t xi = floorToInt(x);
    const std::int32_t yi = floorToInt(y);
    const float u = fade3(x - static_cast<float>(xi));
    const float v = fade3(y - static_cast<float>(yi));

    const float c00 = hash2(xi,     yi);
    const float c10 = hash2(xi + 1, yi);
    const float c01 = hash2(xi,     yi + 1);
    const float c11 = hash2(xi + 1, yi + 1);

    return lerp(lerp(c00, c10, u), lerp(c01, c11, u), v);
}

/**
 * @brief Smoothly interpolated 3D value noise, mean 0.5, range [0,1).
 *
 * The third axis is what separates an animated flame from a scrolling texture:
 * advancing @p z evolves the field in place -- tongues are born, stretch and
 * die -- where translating a 2D field only slides the same shapes past.
 */
[[nodiscard]] constexpr float valueNoise3(float x, float y, float z) noexcept
{
    const std::int32_t xi = floorToInt(x);
    const std::int32_t yi = floorToInt(y);
    const std::int32_t zi = floorToInt(z);
    const float u = fade5(x - static_cast<float>(xi));
    const float v = fade5(y - static_cast<float>(yi));
    const float w = fade5(z - static_cast<float>(zi));

    const float c000 = hash3(xi,     yi,     zi);
    const float c100 = hash3(xi + 1, yi,     zi);
    const float c010 = hash3(xi,     yi + 1, zi);
    const float c110 = hash3(xi + 1, yi + 1, zi);
    const float c001 = hash3(xi,     yi,     zi + 1);
    const float c101 = hash3(xi + 1, yi,     zi + 1);
    const float c011 = hash3(xi,     yi + 1, zi + 1);
    const float c111 = hash3(xi + 1, yi + 1, zi + 1);

    const float x00 = lerp(c000, c100, u);
    const float x10 = lerp(c010, c110, u);
    const float x01 = lerp(c001, c101, u);
    const float x11 = lerp(c011, c111, u);

    return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

//! Frequency step between octaves. Deliberately not exactly 2: an integral
//! ratio re-aligns every octave on the same lattice points and prints a
//! rectangular grid onto the sum.
inline constexpr float kLacunarity = 2.02f;
inline constexpr float kGain = 0.5f;

/// Fractal Brownian motion over valueNoise2(). Roughly [0,1), mean 0.5.
[[nodiscard]] constexpr float fbm2(float x, float y, int octaves) noexcept
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; ++i)
    {
        sum += amplitude * valueNoise2(x * frequency, y * frequency);
        frequency *= kLacunarity;
        amplitude *= kGain;
    }

    return sum;
}

/// Fractal Brownian motion over valueNoise3(). Roughly [0,1), mean 0.5.
[[nodiscard]] constexpr float fbm3(float x, float y, float z, int octaves) noexcept
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; ++i)
    {
        sum += amplitude * valueNoise3(x * frequency, y * frequency, z * frequency);
        frequency *= kLacunarity;
        amplitude *= kGain;
    }

    return sum;
}

} // namespace orglogo

#endif // ORGLOGO_NOISE_H
