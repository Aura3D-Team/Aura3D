#include "aura/Core/Collision.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aura3d
{

glm::vec3 Ray::at(f32 t) const noexcept
{
    return origin + direction * t;
}

bool Aabb::empty() const noexcept
{
    return min.x > max.x || min.y > max.y || min.z > max.z;
}

glm::vec3 Aabb::center() const noexcept
{
    return (min + max) * 0.5f;
}

void Aabb::expand(glm::vec3 point) noexcept
{
    min = glm::min(min, point);
    max = glm::max(max, point);
}

f32 Plane::signedDistance(glm::vec3 point) const noexcept
{
    return glm::dot(normal, point) + distance;
}

Frustum Frustum::fromMatrix(const glm::mat4 &clip, bool depthZeroToOne) noexcept
{
    //! Gribb-Hartmann: each clip-space bound is a row combination; glm is column-major, so rows are gathered.
    const auto row = [&](int r)
    {
        return glm::vec4{clip[0][r], clip[1][r], clip[2][r], clip[3][r]};
    };
    const glm::vec4 x = row(0), y = row(1), z = row(2), w = row(3);
    const std::array<glm::vec4, 6> raw{w + x, w - x, w + y, w - y, depthZeroToOne ? z : w + z, w - z};

    Frustum frustum;
    for (usize i = 0; i < raw.size(); ++i)
    {
        const glm::vec3 normal{raw[i]};
        const f32 length = glm::length(normal);
        frustum.planes[i] = length > 0.0f ? Plane{normal / length, raw[i].w / length} : Plane{normal, raw[i].w};
    }
    return frustum;
}

std::optional<f32> intersect(const Ray &ray, const Aabb &box) noexcept
{
    if (box.empty())
        return std::nullopt;

    //! Slab test. An axis the ray does not move along is a range check, not a division: on a face it
    //! would be 0 * inf.
    f32 nearest = 0.0f;
    f32 farthest = std::numeric_limits<f32>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        if (ray.direction[axis] == 0.0f)
        {
            if (ray.origin[axis] < box.min[axis] || ray.origin[axis] > box.max[axis])
                return std::nullopt;
            continue;
        }
        const f32 inverse = 1.0f / ray.direction[axis];
        const f32 t0 = (box.min[axis] - ray.origin[axis]) * inverse;
        const f32 t1 = (box.max[axis] - ray.origin[axis]) * inverse;
        nearest = std::max(nearest, std::min(t0, t1));
        farthest = std::min(farthest, std::max(t0, t1));
    }
    return nearest <= farthest ? std::optional{nearest} : std::nullopt;
}

std::optional<f32> intersect(const Ray &ray, const Sphere &sphere) noexcept
{
    const glm::vec3 offset = ray.origin - sphere.center;
    const f32 a = glm::dot(ray.direction, ray.direction);
    const f32 halfB = glm::dot(offset, ray.direction);
    const f32 c = glm::dot(offset, offset) - sphere.radius * sphere.radius;
    if (c <= 0.0f)
        return 0.0f;
    const f32 discriminant = halfB * halfB - a * c;
    if (a == 0.0f || discriminant < 0.0f || halfB > 0.0f)
        return std::nullopt;
    return (-halfB - std::sqrt(discriminant)) / a;
}

std::optional<f32> intersect(const Ray &ray, const Plane &plane) noexcept
{
    const f32 facing = glm::dot(plane.normal, ray.direction);
    if (facing == 0.0f)
        return std::nullopt;
    const f32 t = -plane.signedDistance(ray.origin) / facing;
    return t >= 0.0f ? std::optional{t} : std::nullopt;
}

std::optional<f32> intersect(const Ray &ray, glm::vec3 a, glm::vec3 b, glm::vec3 c) noexcept
{
    //! Moller-Trumbore.
    constexpr f32 kParallel = 1e-12f;
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 p = glm::cross(ray.direction, ac);
    const f32 determinant = glm::dot(ab, p);
    if (std::abs(determinant) < kParallel)
        return std::nullopt;
    const f32 inverse = 1.0f / determinant;
    const glm::vec3 s = ray.origin - a;
    const f32 u = glm::dot(s, p) * inverse;
    if (u < 0.0f || u > 1.0f)
        return std::nullopt;
    const glm::vec3 q = glm::cross(s, ab);
    const f32 v = glm::dot(ray.direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f)
        return std::nullopt;
    const f32 t = glm::dot(ac, q) * inverse;
    return t >= 0.0f ? std::optional{t} : std::nullopt;
}

bool overlaps(const Aabb &a, const Aabb &b) noexcept
{
    return !a.empty() && !b.empty() && glm::all(glm::lessThanEqual(a.min, b.max)) &&
           glm::all(glm::lessThanEqual(b.min, a.max));
}

bool overlaps(const Sphere &a, const Sphere &b) noexcept
{
    const glm::vec3 offset = a.center - b.center;
    const f32 reach = a.radius + b.radius;
    return glm::dot(offset, offset) <= reach * reach;
}

bool overlaps(const Sphere &sphere, const Aabb &box) noexcept
{
    if (box.empty())
        return false;
    const glm::vec3 offset = sphere.center - glm::clamp(sphere.center, box.min, box.max);
    return glm::dot(offset, offset) <= sphere.radius * sphere.radius;
}

bool visible(const Frustum &frustum, const Aabb &box) noexcept
{
    if (box.empty())
        return false;
    //! The corner furthest along each normal: if even that is behind a plane, the whole box is.
    return std::ranges::all_of(frustum.planes,
                               [&](const Plane &plane)
                               {
                                   const glm::vec3 corner =
                                       glm::mix(box.min, box.max,
                                                glm::vec3(glm::greaterThanEqual(plane.normal, glm::vec3(0.0f))));
                                   return plane.signedDistance(corner) >= 0.0f;
                               });
}

bool visible(const Frustum &frustum, const Sphere &sphere) noexcept
{
    return std::ranges::all_of(frustum.planes,
                               [&](const Plane &plane)
                               {
                                   return plane.signedDistance(sphere.center) >= -sphere.radius;
                               });
}

Aabb boundsOf(std::span<const gfx::Vertex3D> vertices) noexcept
{
    Aabb box;
    for (const gfx::Vertex3D &vertex : vertices)
        box.expand(vertex.pos);
    return box;
}

Aabb transformed(const Aabb &box, const glm::mat4 &transform) noexcept
{
    if (box.empty())
        return box;
    //! Arvo: each output axis takes, per input axis, whichever end the matrix entry makes smaller.
    Aabb out{glm::vec3(transform[3]), glm::vec3(transform[3])};
    for (int column = 0; column < 3; ++column)
    {
        const glm::vec3 a = glm::vec3(transform[column]) * box.min[column];
        const glm::vec3 b = glm::vec3(transform[column]) * box.max[column];
        out.min += glm::min(a, b);
        out.max += glm::max(a, b);
    }
    return out;
}

} // namespace aura3d
