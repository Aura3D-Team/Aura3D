#ifndef AURA_CORE_COLLISION_H
#define AURA_CORE_COLLISION_H

#pragma once

#include <array>
#include <limits>
#include <optional>
#include <span>

#include <glm/glm.hpp>

#include "aura/Core/AuraCore.h"

/**
 * @file Collision.h
 * @brief Rays, bounds and the tests between them: picking, collision and culling.
 *
 * @code
 * const Ray ray = camera.rayFromScreen(mouse, viewport);
 * if (const auto t = intersect(ray, transformed(meshBounds, model)))
 *     selected = ray.at(*t);
 * @endcode
 *
 * Every intersect() returns the distance along the ray to the first hit at or past its origin, in units of
 * the direction's length; a ray starting inside a solid hits at 0.
 */

namespace aura3d
{

struct Ray
{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};

    [[nodiscard]] glm::vec3 at(f32 t) const noexcept;
};

/// Axis-aligned box. Default-constructed it is empty, and expand() grows it from nothing.
struct Aabb
{
    glm::vec3 min{std::numeric_limits<f32>::max()};
    glm::vec3 max{std::numeric_limits<f32>::lowest()};

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] glm::vec3 center() const noexcept;
    void expand(glm::vec3 point) noexcept;
};

struct Sphere
{
    glm::vec3 center{0.0f};
    f32 radius = 0.0f;
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

[[nodiscard]] std::optional<f32> intersect(const Ray &ray, const Aabb &box) noexcept;
[[nodiscard]] std::optional<f32> intersect(const Ray &ray, const Sphere &sphere) noexcept;
[[nodiscard]] std::optional<f32> intersect(const Ray &ray, const Plane &plane) noexcept;
/// Both faces; a ray in the triangle's plane misses.
[[nodiscard]] std::optional<f32> intersect(const Ray &ray, glm::vec3 a, glm::vec3 b, glm::vec3 c) noexcept;

[[nodiscard]] bool overlaps(const Aabb &a, const Aabb &b) noexcept;
[[nodiscard]] bool overlaps(const Sphere &a, const Sphere &b) noexcept;
[[nodiscard]] bool overlaps(const Sphere &sphere, const Aabb &box) noexcept;

/// False only when @p box is wholly outside one plane: a box near a corner may pass and still be off screen.
[[nodiscard]] bool visible(const Frustum &frustum, const Aabb &box) noexcept;
[[nodiscard]] bool visible(const Frustum &frustum, const Sphere &sphere) noexcept;

[[nodiscard]] Aabb boundsOf(std::span<const gfx::Vertex3D> vertices) noexcept;
/// The box around @p box after @p transform: tight for rotations, never smaller than the true bounds.
[[nodiscard]] Aabb transformed(const Aabb &box, const glm::mat4 &transform) noexcept;

} // namespace aura3d

#endif // AURA_CORE_COLLISION_H
