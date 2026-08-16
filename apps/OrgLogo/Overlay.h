#ifndef ORGLOGO_OVERLAY_H
#define ORGLOGO_OVERLAY_H

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "aura/Core/AuraCore.h"

#include "LogoAssets.h"

/**
 * @file Overlay.h
 * @brief Screen-space layout and quad emission for the engine's 2D overlay.
 *
 * Every animated layer of the logo is drawn through IRenderer::drawBatch2D(),
 * which takes window-pixel coordinates with the origin at the top-left corner
 * and needs no camera. Sizes are expressed as fractions of SceneLayout::unit so
 * the composition is identical at any window size.
 */
namespace orglogo {

//! Side of the flame quad, as a fraction of the layout's reference length.
inline constexpr float kOrbQuadFraction = 0.66f;

/**
 * @struct SceneLayout
 * @brief Where the logo sits in the current framebuffer.
 */
struct SceneLayout {
    glm::vec2 windowSize{0.0f, 0.0f};
    glm::vec2 center{0.0f, 0.0f};

    //! Reference length: min(width, height). Every resolution-independent size
    //! in the scene is a multiple of this, so nothing is stretched when the
    //! window is not square.
    float unit = 0.0f;

    //! Half the side of the flame quad, in pixels.
    float orbHalfExtent = 0.0f;
};

[[nodiscard]] inline SceneLayout makeLayout(int width, int height) noexcept
{
    SceneLayout layout;
    layout.windowSize = {static_cast<float>(width), static_cast<float>(height)};
    layout.center = layout.windowSize * 0.5f;
    layout.unit = std::min(layout.windowSize.x, layout.windowSize.y);
    layout.orbHalfExtent = 0.5f * kOrbQuadFraction * layout.unit;
    return layout;
}

/// Falloff length of the backdrop's baked halo, in pixels. Deliberately much
/// longer than the flame quad's own halo so the glow keeps spreading past the
/// quad's edge instead of stopping at it.
[[nodiscard]] inline float haloRadiusPx(const SceneLayout& layout) noexcept
{
    return 0.26f * layout.unit;
}

/**
 * @brief Appends one textured, rotated quad to a 2D overlay batch.
 *
 * @param vertices Batch vertices, in window pixels.
 * @param indices Triangle list; two triangles are appended.
 * @param center Quad centre, in window pixels.
 * @param halfExtent Half width and half height before rotation, in pixels.
 * @param rotation Clockwise on screen, since the overlay's Y axis points down.
 * @param uv Sub-rectangle of the batch texture to sample.
 * @param color Multiplied into the texel; alpha carries the layer's intensity.
 */
inline void appendQuad(std::vector<aura3d::gfx::Vertex2D>& vertices,
                       std::vector<u32>& indices,
                       const glm::vec2& center,
                       const glm::vec2& halfExtent,
                       float rotation,
                       const UvRect& uv,
                       const glm::vec4& color)
{
    const auto base = static_cast<u32>(vertices.size());

    const float cosR = std::cos(rotation);
    const float sinR = std::sin(rotation);
    const glm::vec2 axisX{cosR * halfExtent.x, sinR * halfExtent.x};
    const glm::vec2 axisY{-sinR * halfExtent.y, cosR * halfExtent.y};

    const std::array<glm::vec2, 4> corners{
        center - axisX - axisY,
        center + axisX - axisY,
        center + axisX + axisY,
        center - axisX + axisY,
    };
    const std::array<glm::vec2, 4> texCoords{
        glm::vec2{uv.u0, uv.v0},
        glm::vec2{uv.u1, uv.v0},
        glm::vec2{uv.u1, uv.v1},
        glm::vec2{uv.u0, uv.v1},
    };

    for (std::size_t i = 0; i < corners.size(); ++i)
        vertices.push_back(aura3d::gfx::Vertex2D{corners[i], texCoords[i], color});

    //! Winding is irrelevant here: the overlay pipeline disables culling on
    //! every backend, exactly so UI quads need not agree on one.
    indices.insert(indices.end(), {base, base + 1u, base + 2u,
                                   base + 2u, base + 3u, base});
}

} // namespace orglogo

#endif // ORGLOGO_OVERLAY_H
