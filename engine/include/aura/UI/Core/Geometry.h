#ifndef AURA_UI_GEOMETRY_H
#define AURA_UI_GEOMETRY_H

#pragma once

#include <algorithm>
#include <limits>

#include <glm/glm.hpp>
#include <ink/ink_base.hpp>

/**
 * @file Geometry.h
 * @brief The value types every other AuraUI header speaks in.
 *
 * Pixels throughout, y growing downwards, origin at the surface's top-left --
 * the same convention IRenderer::drawBatch2D() takes, so nothing between a
 * widget and the GPU has to flip an axis.
 */

namespace aura3d::ui {

/// Stand-in for "no constraint". Finite so arithmetic on it stays defined:
/// `kUnbounded - padding` is still huge, where `inf - padding` would be inf
/// and `inf * 0` a NaN in the middle of a layout pass.
inline constexpr f32 kUnbounded = 1.0e7f;

[[nodiscard]] constexpr bool isUnbounded(f32 extent) noexcept
{
    return extent >= kUnbounded;
}

/// Horizontal placement of text within the rectangle that holds it.
enum class Align : u8 { Left, Center, Right };

/// An axis-aligned rectangle, held as opposite corners.
struct Rect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    [[nodiscard]] static constexpr Rect fromSize(glm::vec2 origin, glm::vec2 size) noexcept
    {
        return {origin, origin + size};
    }

    [[nodiscard]] constexpr f32 width() const noexcept { return max.x - min.x; }
    [[nodiscard]] constexpr f32 height() const noexcept { return max.y - min.y; }
    [[nodiscard]] constexpr glm::vec2 size() const noexcept { return max - min; }
    [[nodiscard]] constexpr glm::vec2 center() const noexcept { return (min + max) * 0.5f; }

    /// True when the rectangle encloses no pixels, so a caller can drop it
    /// before doing any work on it.
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return max.x <= min.x || max.y <= min.y;
    }

    [[nodiscard]] constexpr bool contains(glm::vec2 point) const noexcept
    {
        return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
    }

    [[nodiscard]] constexpr Rect translated(glm::vec2 by) const noexcept
    {
        return {min + by, max + by};
    }

    /// Pulled inwards on all four sides. A rectangle inset past its own size
    /// collapses to its centre rather than inverting.
    [[nodiscard]] constexpr Rect inset(f32 by) const noexcept
    {
        const glm::vec2 half = glm::vec2{std::min(by, width() * 0.5f), std::min(by, height() * 0.5f)};
        return {min + half, max - half};
    }

    [[nodiscard]] constexpr Rect grown(f32 by) const noexcept { return inset(-by); }
};

[[nodiscard]] constexpr Rect intersect(const Rect& a, const Rect& b) noexcept
{
    return Rect{{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)},
                {std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y)}};
}

/// The smallest rectangle covering both. An empty operand is ignored, so
/// folding this over a child list needs no "first child" special case.
[[nodiscard]] constexpr Rect unite(const Rect& a, const Rect& b) noexcept
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;

    return Rect{{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y)},
                {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y)}};
}

/// Per-side inset, for margin and padding.
struct Thickness {
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 right = 0.0f;
    f32 bottom = 0.0f;

    [[nodiscard]] static constexpr Thickness all(f32 value) noexcept
    {
        return {value, value, value, value};
    }

    [[nodiscard]] static constexpr Thickness symmetric(f32 horizontal, f32 vertical) noexcept
    {
        return {horizontal, vertical, horizontal, vertical};
    }

    [[nodiscard]] constexpr f32 horizontal() const noexcept { return left + right; }
    [[nodiscard]] constexpr f32 vertical() const noexcept { return top + bottom; }
    [[nodiscard]] constexpr glm::vec2 collapsed() const noexcept
    {
        return {horizontal(), vertical()};
    }

    [[nodiscard]] constexpr glm::vec2 topLeft() const noexcept { return {left, top}; }

    [[nodiscard]] constexpr bool operator==(const Thickness&) const noexcept = default;
};

/// @p rect with @p by removed from each side. Never inverts: a padding wider
/// than the rectangle yields an empty one, which every consumer already drops.
[[nodiscard]] constexpr Rect deflate(const Rect& rect, const Thickness& by) noexcept
{
    const glm::vec2 lo = rect.min + by.topLeft();
    return {lo, {std::max(lo.x, rect.max.x - by.right), std::max(lo.y, rect.max.y - by.bottom)}};
}

[[nodiscard]] constexpr Rect inflate(const Rect& rect, const Thickness& by) noexcept
{
    return {rect.min - by.topLeft(), rect.max + glm::vec2{by.right, by.bottom}};
}

/// Per-corner radius, clockwise from the top left.
struct Corners {
    f32 topLeft = 0.0f;
    f32 topRight = 0.0f;
    f32 bottomRight = 0.0f;
    f32 bottomLeft = 0.0f;

    [[nodiscard]] static constexpr Corners all(f32 radius) noexcept
    {
        return {radius, radius, radius, radius};
    }

    /// Rounded top only -- a tab, a panel header, a card that meets a footer.
    [[nodiscard]] static constexpr Corners top(f32 radius) noexcept
    {
        return {radius, radius, 0.0f, 0.0f};
    }

    [[nodiscard]] static constexpr Corners bottom(f32 radius) noexcept
    {
        return {0.0f, 0.0f, radius, radius};
    }

    [[nodiscard]] constexpr bool any() const noexcept
    {
        return topLeft > 0.0f || topRight > 0.0f || bottomRight > 0.0f || bottomLeft > 0.0f;
    }

    [[nodiscard]] constexpr f32 largest() const noexcept
    {
        return std::max({topLeft, topRight, bottomRight, bottomLeft});
    }

    [[nodiscard]] constexpr bool operator==(const Corners&) const noexcept = default;
};

[[nodiscard]] constexpr f32 lerp(f32 a, f32 b, f32 t) noexcept
{
    return a + (b - a) * t;
}

[[nodiscard]] inline glm::vec4 withAlpha(glm::vec4 color, f32 alpha) noexcept
{
    color.a *= alpha;
    return color;
}

} // namespace aura3d::ui

#endif // AURA_UI_GEOMETRY_H
