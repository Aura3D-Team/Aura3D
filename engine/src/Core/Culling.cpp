#include "aura/Core/Culling.h"

#include <algorithm>
#include <utility>

namespace aura3d
{

bool Aabb::empty() const noexcept
{
    return min.x > max.x || min.y > max.y || min.z > max.z;
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
