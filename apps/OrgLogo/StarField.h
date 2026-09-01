#ifndef ORGLOGO_STAR_FIELD_H
#define ORGLOGO_STAR_FIELD_H

#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "aura/Renderer/IRenderer.h"

#include "LogoAssets.h"
#include "Overlay.h"
#include "Palette.h"

namespace orglogo {

/**
 * @class StarField
 * @brief The animated sky: hundreds of twinkling stars plus the occasional
 *        meteor, drawn as one batch.
 *
 * Stars are stored in polar coordinates about the logo's centre and grouped
 * into three depth layers turning at different rates, so the field drifts with
 * a parallax rather than as a rigid picture. Each star carries its own twinkle
 * frequency and a much slower flare clock; the flare is what makes a star
 * occasionally swell, whiten and throw longer spikes before settling back.
 *
 * The whole field -- every star, its white-hot core and any meteor in flight --
 * leaves as a single drawBatch2D() call, because all of it samples one sprite
 * atlas. Cost therefore scales with the number of stars, not with the screen.
 */
class StarField {
public:
    /**
     * @param renderer Used once, to upload the sprite atlas.
     * @param seed Chooses the sky; the same seed always yields the same stars.
     */
    StarField(aura3d::IRenderer& renderer, std::uint32_t seed);

    StarField(const StarField&) = delete;
    StarField& operator=(const StarField&) = delete;

    /**
     * @brief Advances the simulation.
     *
     * @param time Seconds since the logo appeared; drives twinkle and drift.
     * @param deltaTime Seconds since the previous frame; drives the meteors.
     * @param orbPulse The flame core's breath, in [-1,1]. The field expands a
     *        hair with it, so the sky moves with the orb rather than beside it.
     */
    void update(float time, float deltaTime, float orbPulse);

    /// Rebuilds the batch for @p layout and submits it. Must be called between
    /// beginRenderPass() and endRenderPass().
    void submit(aura3d::IRenderer& renderer, const SceneLayout& layout);

private:
    struct Star {
        float radius = 0.0f;      //! Distance from centre, in layout units.
        float theta = 0.0f;       //! Base angle, radians.
        float spinScale = 1.0f;   //! Depth layer's share of the field drift.
        float size = 0.0f;        //! Sprite half-extent, in layout units.
        float brightness = 0.0f;
        Rgb tint{};
        float twinklePhase = 0.0f;
        float twinkleRate = 1.0f;
        float flarePhase = 0.0f;
        float flareRate = 0.0f;
        float roll = 0.0f;        //! Sprite rotation, so spikes are not aligned.
        float rollRate = 0.0f;
        bool spiked = false;
    };

    struct Meteor {
        glm::vec2 origin{0.0f, 0.0f};   //! Layout units, relative to centre.
        glm::vec2 velocity{0.0f, 0.0f}; //! Layout units per second.
        float age = 0.0f;
        float life = 0.0f;
        float length = 0.0f;
        float width = 0.0f;
        float brightness = 0.0f;
    };

    void _seedStars(std::uint32_t seed);
    void _spawnMeteor();
    void _buildBatch(const SceneLayout& layout);
    void _appendStar(const Star& star, const SceneLayout& layout);
    void _appendMeteor(const Meteor& meteor, const SceneLayout& layout);

    std::vector<Star> _stars;
    std::vector<Meteor> _meteors;

    std::vector<aura3d::gfx::Vertex2D> _vertices;
    std::vector<u32> _indices;

    aura3d::TextureHandle _atlas;
    UvRect _softUv{};
    UvRect _spikedUv{};
    UvRect _cometUv{};

    std::mt19937 _rng;
    float _time = 0.0f;
    float _orbPulse = 0.0f;
    float _meteorCountdown = 0.0f;
};

} // namespace orglogo

#endif // ORGLOGO_STAR_FIELD_H
