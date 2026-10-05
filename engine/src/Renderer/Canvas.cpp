#include "aura/Renderer/Canvas.h"

#include <array>
#include <cmath>

namespace aura3d
{
namespace gfx
{
namespace
{

//! x - x is 0 for finite x and NaN for NaN or infinity, so one comparison tests a whole vector.
template <glm::length_t N> [[nodiscard]] bool finite(const glm::vec<N, f32> &value) noexcept
{
    return value - value == glm::vec<N, f32>(0.0f);
}

//! Keeps the part of [a, b] on the non-negative side of a plane, given each end's signed distance.
[[nodiscard]] bool clipSegment(glm::vec4 &a, glm::vec4 &b, f32 da, f32 db) noexcept
{
    if (da < 0.0f && db < 0.0f)
        return false;
    if (da < 0.0f)
        a = glm::mix(a, b, da / (da - db));
    else if (db < 0.0f)
        b = glm::mix(b, a, db / (db - da));
    return true;
}

} // namespace

void Canvas::line(glm::vec2 from, glm::vec2 to, const glm::vec4 &color, f32 width)
{
    segment({from, 0.0f}, {to, 0.0f}, color, width);
}

void Canvas::line(const CanvasView &view, glm::vec3 from, glm::vec3 to, const glm::vec4 &color, f32 width)
{
    if (!(view.target.x > 0.0f) || !(view.target.y > 0.0f))
        return;

    glm::vec4 a = view.clip * glm::vec4(from, 1.0f);
    glm::vec4 b = view.clip * glm::vec4(to, 1.0f);

    //! Clipped to the near plane, and to w > 0 so the divide is defined; the rasterizer clips
    //! the rest. Widening the projected segment in pixels keeps the width at any depth.
    constexpr f32 kMinW = 1e-6f;
    const bool zeroToOne = view.depthZeroToOne;
    if (!clipSegment(a, b, zeroToOne ? a.z : a.z + a.w, zeroToOne ? b.z : b.z + b.w) ||
        !clipSegment(a, b, a.w - kMinW, b.w - kMinW))
        return;

    //! Screen batches take depth in [0, 1] on every backend.
    const auto toScreen = [&](const glm::vec4 &clip)
    {
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec3{(ndc.x + 1.0f) * 0.5f * view.target.x, (1.0f - ndc.y) * 0.5f * view.target.y,
                         zeroToOne ? ndc.z : (ndc.z + 1.0f) * 0.5f};
    };
    segment(toScreen(a), toScreen(b), color, width);
}

void Canvas::rect(glm::vec2 origin, glm::vec2 size, const glm::vec4 &color)
{
    const glm::vec2 end = origin + size;
    if (!(size.x > 0.0f) || !(size.y > 0.0f) || !(color.a > 0.0f) || !finite(glm::vec4{origin, end}) || !finite(color))
        return;
    quad({{origin, 0.0f}, {}, color}, {{end.x, origin.y, 0.0f}, {}, color}, {{end, 0.0f}, {}, color},
         {{origin.x, end.y, 0.0f}, {}, color});
}

void Canvas::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4 &color)
{
    triangle(glm::vec3{a, 0.0f}, glm::vec3{b, 0.0f}, glm::vec3{c, 0.0f}, color);
}

void Canvas::triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec4 &color)
{
    //! In double, so rounding cannot call a thin valid triangle degenerate.
    if (!(color.a > 0.0f) || !finite(a) || !finite(b) || !finite(c) || !finite(color) ||
        glm::cross(glm::dvec3(b) - glm::dvec3(a), glm::dvec3(c) - glm::dvec3(a)) == glm::dvec3(0.0))
        return;
    const auto base = static_cast<u32>(_vertices.size());
    _vertices.push_back({a, {}, color});
    _vertices.push_back({b, {}, color});
    _vertices.push_back({c, {}, color});
    const std::array<u32, 3> corners{base, base + 1, base + 2};
    _indices.insert(_indices.end(), corners.begin(), corners.end());
}

void Canvas::append(std::span<const BatchVertex> vertices, std::span<const u32> indices)
{
    const auto base = static_cast<u32>(_vertices.size());
    _vertices.insert(_vertices.end(), vertices.begin(), vertices.end());
    for (const u32 index : indices)
        _indices.push_back(base + index);
}

void Canvas::clear() noexcept
{
    _vertices.clear();
    _indices.clear();
}

bool Canvas::empty() const noexcept
{
    return _indices.empty();
}

std::span<const BatchVertex> Canvas::vertices() const noexcept
{
    return _vertices;
}

std::span<const u32> Canvas::indices() const noexcept
{
    return _indices;
}

void Canvas::segment(glm::vec3 from, glm::vec3 to, const glm::vec4 &color, f32 width)
{
    const glm::vec2 d{to.x - from.x, to.y - from.y};
    const f32 lengthSq = d.x * d.x + d.y * d.y;
    if (!(width > 0.0f) || !(color.a > 0.0f) || !(lengthSq > 0.0f) || !finite(glm::vec4{from, width}) ||
        !finite(glm::vec4{to, lengthSq}) || !finite(color))
        return;
    const glm::vec3 normal{glm::vec2{-d.y, d.x} * (width * 0.5f / std::sqrt(lengthSq)), 0.0f};
    quad({from - normal, {}, color}, {to - normal, {}, color}, {to + normal, {}, color}, {from + normal, {}, color});
}

void Canvas::quad(const BatchVertex &a, const BatchVertex &b, const BatchVertex &c, const BatchVertex &d)
{
    const auto base = static_cast<u32>(_vertices.size());
    _vertices.push_back(a);
    _vertices.push_back(b);
    _vertices.push_back(c);
    _vertices.push_back(d);
    const std::array<u32, 6> corners{base, base + 1, base + 2, base + 2, base + 3, base};
    _indices.insert(_indices.end(), corners.begin(), corners.end());
}

} // namespace gfx
} // namespace aura3d
