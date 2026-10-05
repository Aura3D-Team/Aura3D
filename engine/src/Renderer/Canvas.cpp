#include "aura/Renderer/Canvas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace aura3d
{
namespace gfx
{
namespace
{

constexpr std::array<u32, 6> kQuadIndices{0, 1, 2, 2, 3, 0};
constexpr std::array<u32, 3> kTriangleIndices{0, 1, 2};

template <glm::length_t N, glm::qualifier Q> [[nodiscard]] bool finite(const glm::vec<N, f32, Q> &value) noexcept
{
    for (glm::length_t i = 0; i < N; ++i)
        if (!std::isfinite(value[i]))
            return false;
    return true;
}

[[nodiscard]] bool visible(const glm::vec4 &color) noexcept
{
    return color.a > 0.0f && finite(color);
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

//! Quad @p width wide around [@p from, @p to] in x and y, flat-ended; nullopt draws nothing.
[[nodiscard]] std::optional<std::array<BatchVertex, 4>> lineQuad(glm::vec3 from, glm::vec3 to, const glm::vec4 &color,
                                                                 f32 width) noexcept
{
    const f32 length = std::hypot(to.x - from.x, to.y - from.y);
    if (!finite(from) || !finite(to) || !visible(color) || !(width > 0.0f) || !std::isfinite(width) ||
        !(length > 0.0f) || !std::isfinite(length))
        return std::nullopt;

    const glm::vec3 normal{glm::vec2{from.y - to.y, to.x - from.x} * (width * 0.5f / length), 0.0f};
    return std::array<BatchVertex, 4>{
        {{from - normal, {}, color}, {to - normal, {}, color}, {to + normal, {}, color}, {from + normal, {}, color}}};
}

} // namespace

void Canvas::line(glm::vec2 from, glm::vec2 to, const glm::vec4 &color, f32 width)
{
    if (const auto quad = lineQuad({from, 0.0f}, {to, 0.0f}, color, width))
        append(*quad, kQuadIndices);
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
    if (const auto quad = lineQuad(toScreen(a), toScreen(b), color, width))
        append(*quad, kQuadIndices);
}

void Canvas::rect(glm::vec2 origin, glm::vec2 size, const glm::vec4 &color)
{
    const glm::vec2 end = origin + size;
    if (!finite(origin) || !finite(end) || !(size.x > 0.0f) || !(size.y > 0.0f) || !visible(color))
        return;

    const std::array<BatchVertex, 4> quad{{{{origin, 0.0f}, {}, color},
                                           {{end.x, origin.y, 0.0f}, {}, color},
                                           {{end, 0.0f}, {}, color},
                                           {{origin.x, end.y, 0.0f}, {}, color}}};
    append(quad, kQuadIndices);
}

void Canvas::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4 &color)
{
    triangle(glm::vec3{a, 0.0f}, glm::vec3{b, 0.0f}, glm::vec3{c, 0.0f}, color);
}

void Canvas::triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec4 &color)
{
    //! In double, so rounding cannot call a thin valid triangle degenerate.
    if (!finite(a) || !finite(b) || !finite(c) || !visible(color) ||
        glm::cross(glm::dvec3(b) - glm::dvec3(a), glm::dvec3(c) - glm::dvec3(a)) == glm::dvec3(0.0))
        return;

    const std::array<BatchVertex, 3> corners{{{a, {}, color}, {b, {}, color}, {c, {}, color}}};
    append(corners, kTriangleIndices);
}

void Canvas::append(std::span<const BatchVertex> vertices, std::span<const u32> indices)
{
    const auto base = static_cast<u32>(_vertices.size());
    _vertices.insert(_vertices.end(), vertices.begin(), vertices.end());
    const usize first = _indices.size();
    _indices.resize(first + indices.size());
    std::ranges::transform(indices, _indices.begin() + static_cast<std::ptrdiff_t>(first),
                           [base](u32 index)
                           {
                               return index + base;
                           });
}

} // namespace gfx
} // namespace aura3d
