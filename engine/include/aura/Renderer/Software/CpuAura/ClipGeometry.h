#pragma once

#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include "aura/Renderer/Software/CpuAura/CpuFrameBufferManager.h"

namespace aura3d::cpu {

/// A vertex after the vertex stage and before perspective division: still in
/// clip space, so it can be interpolated linearly along an edge.
struct ClipVertex {
    /// Clip-space position (MVP applied, not yet divided by w).
    glm::vec4 position{};
    /// Texture coordinate, interpolated linearly by the clipper.
    glm::vec2 uv{};
    /// Lit vertex colour, interpolated linearly by the clipper.
    glm::vec4 color{};
};

/// Clips one triangle against the six frustum planes in homogeneous space and
/// emits the visible part as screen-space triangles.
///
/// Clipping happens before the divide by w because a vertex behind the camera
/// has a negative w: dividing first would flip it across the screen, and
/// dropping the whole triangle (the old behaviour) makes geometry pop out as
/// soon as one corner passes the near plane. A convex polygon comes out of the
/// clip and is fanned from its first vertex, so back-face culling is applied
/// per fan triangle after projection.
///
/// @param a, b, c  Triangle corners in clip space. A non-finite corner rejects the triangle.
/// @param width    Framebuffer width in pixels, for the viewport transform.
/// @param height   Framebuffer height in pixels.
/// @param emit     Called once per front-facing `ScreenTriangle` produced; zero to seven calls.
template<class Emit>
void clipTriangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c,
                  f32 width, f32 height, Emit&& emit)
{
    std::array<ClipVertex, 12> front{a, b, c}, back{};
    usize count = 3;
    unsigned outsideAny = 0, outsideAll = 63;
    for (usize i = 0; i < count; ++i)
    {
        unsigned outside = 0;
        for (int axis = 0; axis < 4; ++axis)
            if (!std::isfinite(front[i].position[axis])) return;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (front[i].position[axis] < -front[i].position.w) outside |= 1u << (axis * 2);
            if (front[i].position[axis] > front[i].position.w) outside |= 2u << (axis * 2);
        }
        outsideAny |= outside;
        outsideAll &= outside;
    }
    if (outsideAll) return;

    for (int plane = 0; plane < 6 && count; ++plane)
    {
        // Convex interpolation cannot leave a plane containing all input vertices.
        if (!(outsideAny & (1u << plane))) continue;
        const auto distance = [plane](const ClipVertex& v) {
            return v.position.w + ((plane & 1) ? -v.position[plane / 2] : v.position[plane / 2]);
        };
        usize next = 0;
        ClipVertex previous = front[count - 1];
        f32 previousDistance = distance(previous);
        for (usize i = 0; i < count; ++i)
        {
            const ClipVertex current = front[i];
            const f32 currentDistance = distance(current);
            if ((previousDistance >= 0) != (currentDistance >= 0))
            {
                const f32 t = previousDistance / (previousDistance - currentDistance);
                back[next++] = {glm::mix(previous.position, current.position, t),
                                glm::mix(previous.uv, current.uv, t),
                                glm::mix(previous.color, current.color, t)};
            }
            if (currentDistance >= 0) back[next++] = current;
            previous = current;
            previousDistance = currentDistance;
        }
        front.swap(back);
        count = next;
    }
    if (count < 3) return;
    std::array<ScreenVertex, 12> screen{};
    for (usize i = 0; i < count; ++i)
    {
        if (front[i].position.w <= 0) return;
        auto& v = screen[i];
        v.invW = 1.0f / front[i].position.w;
        const glm::vec3 ndc = glm::vec3(front[i].position) * v.invW;
        v.x = (ndc.x + 1) * .5f * width;
        v.y = (1 - ndc.y) * .5f * height;
        v.z = (ndc.z + 1) * .5f;
        v.uv = front[i].uv;
        v.color = front[i].color;
    }
    for (usize i = 1; i + 1 < count; ++i)
        if (isFrontFacing(screen[0], screen[i], screen[i + 1]))
            emit(ScreenTriangle{screen[0], screen[i], screen[i + 1]});
}

} // namespace aura3d::cpu
