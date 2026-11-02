#ifndef AURA_CORE_CULLING_H
#define AURA_CORE_CULLING_H

#pragma once

#include <array>
#include <limits>
#include <span>

#include <glm/glm.hpp>

#include "aura/Core/AuraCore.h"

/// @file Culling.h
/// Bounds and the frustum test the renderer culls draws with; meshBounds() and transformed() give a game the same
/// boxes for collision and picking of its own.

namespace aura3d
{

/// Axis-aligned box. Default-constructed it is empty, and expand() grows it from nothing.
struct Aabb
{
    glm::vec3 min{std::numeric_limits<f32>::max()};
    glm::vec3 max{std::numeric_limits<f32>::lowest()};

    [[nodiscard]] bool empty() const noexcept;
    void expand(glm::vec3 point) noexcept;
};

/// Points p with dot(normal, p) + distance == 0; the normal side is positive.
struct Plane
{
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    f32 distance = 0.0f;

    [[nodiscard]] f32 signedDistance(glm::vec3 point) const noexcept;
};

/// The six planes of a view volume, normals pointing inwards.
struct Frustum
{
    std::array<Plane, 6> planes{};

    /// From world-to-clip @p clip (projection * view); @p depthZeroToOne as in gfx::CanvasView.
    [[nodiscard]] static Frustum fromMatrix(const glm::mat4 &clip, bool depthZeroToOne) noexcept;
};

/// False only when @p box is wholly outside one plane: a box near a corner may pass and still be off screen.
[[nodiscard]] bool visible(const Frustum &frustum, const Aabb &box) noexcept;

[[nodiscard]] Aabb boundsOf(std::span<const gfx::Vertex3D> vertices) noexcept;
/// The box around @p box after @p transform: tight for rotations, never smaller than the true bounds.
[[nodiscard]] Aabb transformed(const Aabb &box, const glm::mat4 &transform) noexcept;

} // namespace aura3d

#endif // AURA_CORE_CULLING_H
