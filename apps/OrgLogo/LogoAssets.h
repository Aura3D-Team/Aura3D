#ifndef ORGLOGO_LOGO_ASSETS_H
#define ORGLOGO_LOGO_ASSETS_H

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "aura/Core/JobSystem/JobSystem.h"

/**
 * @file LogoAssets.h
 * @brief CPU-side generation of every pixel the logo is made of.
 *
 * Nothing in this header knows the renderer exists. It produces tightly packed
 * RGBA8 images and nothing else, which keeps the expensive, interesting half of
 * the logo -- the procedural content -- independent of the backend, portable to
 * every target the engine builds for, and testable without a window.
 */
namespace orglogo {

/// A tightly packed RGBA8 image, ready for createTextureFromPixels().
struct Image {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool empty() const noexcept { return pixels.empty(); }
};

/**
 * @struct BackdropDesc
 * @brief Everything the static deep field needs to know about where it will
 *        be shown.
 */
struct BackdropDesc {
    int width = 0;   //! Baked at the framebuffer's own resolution, so the
    int height = 0;  //! rasteriser's nearest-neighbour sampling lands 1:1.

    //! Reference length for resolution-independent sizes; min(width, height).
    float unit = 0.0f;

    //! Falloff length of the static core glow, in pixels. The flame quad only
    //! covers the middle of the screen, so the far half of the orb's halo lives
    //! here instead -- baked once rather than re-integrated 60 times a second.
    float haloRadius = 0.0f;

    std::uint32_t seed = 0;
};

/**
 * @brief Bakes the static deep field: nebula shine, dust stars, core halo and
 *        vignette.
 *
 * Drawn as opaque geometry underneath everything else, so this is the only
 * layer that does not use the additive glow encoding.
 */
[[nodiscard]] Image bakeBackdrop(const BackdropDesc& desc, const aura3d::JobSystem& jobs);

/// Sub-rectangle of a texture, in normalised coordinates.
struct UvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

/**
 * @struct StarAtlas
 * @brief One texture holding every sprite the star field draws with.
 *
 * A single atlas is what lets the whole field -- hundreds of stars plus any
 * meteors -- go out as one drawBatch2D() call: the overlay path takes one
 * texture per batch, so a second sprite sheet would mean a second draw.
 */
struct StarAtlas {
    Image image;
    UvRect soft{};    //! Round star: core plus halo, no spikes.
    UvRect spiked{};  //! Bright star: core, diffraction spikes, halo.
    UvRect comet{};   //! Meteor: head at the -U edge, tail trailing to +U.
};

/// Bakes the sprite atlas. Sizes are fixed and small; this is startup work.
[[nodiscard]] StarAtlas bakeStarAtlas();

/**
 * @class FlameField
 * @brief Regenerates the blue flame orb's texture from scratch, every frame.
 *
 * The orb is domain-warped 3D turbulence evaluated in a radially advected
 * frame: the noise is sampled at a point that creeps inward as time advances,
 * so the pattern appears to climb outward, and the third noise axis is time
 * itself, so tongues are born and die in place instead of sliding past. On top
 * of that sit the solid disc, its white-hot centre, and shine rays that rotate
 * around it.
 *
 * @par Cost
 * The expensive part -- two fractal sums and the ray pattern -- is evaluated on
 * a half-resolution grid and reconstructed bilinearly, with one full-resolution
 * noise octave added back per texel to restore the fine filaments. That trades
 * an invisible loss of detail for a 4x cut in the dominant cost, and what is
 * left is split across cores by the engine's JobSystem.
 *
 * @par Threading
 * bake() is the only mutating entry point and is not itself thread-safe;
 * internally it fans out over disjoint bands of the output image.
 */
class FlameField {
public:
    //! Disc radius in quad-UV units, at rest. The disc breathes around it.
    static constexpr float kDiscRadiusUv = 0.185f;

    //! Radius at which the orb's own contribution has faded to nothing. Past
    //! this the backdrop's baked halo carries the glow, so the two meet
    //! without a seam at the quad's edge.
    static constexpr float kGlowLimitUv = 0.50f;

    /**
     * @param resolution Edge length of the square texture, in texels. Rounded
     *        up to a multiple of the internal coarse-grid step.
     * @param jobs Parallel-for used by bake(); must outlive this object.
     */
    FlameField(int resolution, const aura3d::JobSystem& jobs);

    FlameField(const FlameField&) = delete;
    FlameField& operator=(const FlameField&) = delete;
    FlameField(FlameField&&) = delete;
    FlameField& operator=(FlameField&&) = delete;

    /// Regenerates the texture for @p time, in seconds since the logo appeared.
    void bake(float time);

    /// The most recent bake's RGBA8 texels, row-major, resolution() wide.
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept { return _pixels; }

    [[nodiscard]] int resolution() const noexcept { return _resolution; }

    /// The core's breath at the last bake, in [-1,1]. Exposed so other layers
    /// can move with the orb rather than beside it.
    [[nodiscard]] float pulse() const noexcept { return _pulse; }

private:
    //! Per-frame scalars, resolved once per bake and read by every band.
    struct Frame {
        float time = 0.0f;
        float discRadius = 0.0f;   //! Breathing disc radius, quad-UV units.
        float hotGain = 0.0f;      //! Centre flicker.
        float haloGain = 0.0f;
        float rayGain = 0.0f;
        float rise = 0.0f;         //! Radial advection distance, noise units.
    };

    void _buildRayTable(const Frame& frame) noexcept;
    void _buildRadialTables(const Frame& frame) noexcept;
    void _bakeCoarseBand(const Frame& frame, int rowBegin, int rowEnd) noexcept;
    void _shadeBand(const Frame& frame, int rowBegin, int rowEnd) noexcept;

    int _resolution = 0;
    int _coarseDim = 0;  //! Coarse samples per axis, including the far edge.

    const aura3d::JobSystem& _jobs;

    std::vector<std::uint8_t> _pixels;  //! RGBA8 output, _resolution^2 texels.
    std::vector<float> _coarse;         //! (turbulence, ray) pairs, interleaved.
    std::vector<float> _rayTable;       //! Ray intensity by angle.
    std::vector<float> _haloTable;      //! Core halo by radius.
    std::vector<float> _rayFalloff;     //! Ray radial profile, incl. edge fade.

    float _pulse = 0.0f;
};

} // namespace orglogo

#endif // ORGLOGO_LOGO_ASSETS_H
