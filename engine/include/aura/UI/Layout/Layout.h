#ifndef AURA_UI_LAYOUT_H
#define AURA_UI_LAYOUT_H

#pragma once

#include <algorithm>
#include <optional>

#include <glm/glm.hpp>
#include <ink/ink_base.hpp>

#include "aura/UI/Core/Geometry.h"

/**
 * @file Layout.h
 * @brief The sizing vocabulary, and the two-pass contract widgets implement.
 *
 * Layout runs twice over the tree, and the split is what makes intrinsic
 * sizing work at all:
 *
 * @code
 * measure(Constraints)  ->  desiredSize()   // "how big would you like to be?"
 * arrange(Rect)         ->  bounds()        // "this is what you get"
 * @endcode
 *
 * A parent may measure a child several times (a Grid measures each column
 * candidate) but arranges it exactly once. Nothing in the measure pass may
 * depend on a final position, and nothing in the arrange pass may change a
 * desired size.
 */

namespace aura3d::ui {

enum class Axis : u8 { Horizontal, Vertical };

/// Where a widget sits inside the slot its parent gave it, on one axis.
/// Stretch fills the slot, unless an explicit @ref Length says otherwise.
enum class Alignment : u8 { Start, Center, End, Stretch };

/**
 * @brief One axis' sizing rule.
 *
 * @code
 * w.layout().width = Length::px(120.0f);      // exactly 120
 * w.layout().width = Length::percent(50.0f);  // half the parent's content
 * w.layout().width = Length::fill();          // a share of what is left over
 * w.layout().width = Length::automatic();     // as big as its content needs
 * @endcode
 */
struct Length {
    enum class Unit : u8 {
        Auto,    //! Sized by content.
        Pixels,  //! Exactly @c value logical pixels.
        Percent, //! @c value percent of the parent's content extent.
        Fill,    //! @c value is a weight: a share of the leftover extent.
    };

    Unit unit = Unit::Auto;
    f32 value = 0.0f;

    [[nodiscard]] static constexpr Length automatic() noexcept { return {}; }
    [[nodiscard]] static constexpr Length px(f32 pixels) noexcept
    {
        return {Unit::Pixels, pixels};
    }
    [[nodiscard]] static constexpr Length percent(f32 percent) noexcept
    {
        return {Unit::Percent, percent};
    }
    [[nodiscard]] static constexpr Length fill(f32 weight = 1.0f) noexcept
    {
        return {Unit::Fill, std::max(weight, 0.0f)};
    }

    [[nodiscard]] constexpr bool isAuto() const noexcept { return unit == Unit::Auto; }
    [[nodiscard]] constexpr bool isFill() const noexcept { return unit == Unit::Fill; }

    [[nodiscard]] constexpr bool operator==(const Length&) const noexcept = default;
};

/**
 * @brief Resolves @p length to a concrete extent.
 *
 * @return nullopt when the extent is content-driven -- Auto always, and Fill
 *         against an unbounded parent, where "a share of what is left" has no
 *         meaning and content size is the only sensible base.
 */
[[nodiscard]] constexpr std::optional<f32> resolveLength(const Length& length,
                                                         f32 available) noexcept
{
    switch (length.unit)
    {
    case Length::Unit::Pixels:
        return std::max(length.value, 0.0f);

    case Length::Unit::Percent:
        return isUnbounded(available) ? std::nullopt
                                      : std::optional{std::max(available, 0.0f) * length.value *
                                                      0.01f};

    case Length::Unit::Fill:
        return isUnbounded(available) ? std::nullopt : std::optional{std::max(available, 0.0f)};

    case Length::Unit::Auto:
        break;
    }

    return std::nullopt;
}

/// The size range a parent offers a child. @c max is @ref kUnbounded on an
/// axis the parent is itself sized by (a scroll view's content, an auto panel).
struct Constraints {
    glm::vec2 min{0.0f};
    glm::vec2 max{kUnbounded};

    [[nodiscard]] static constexpr Constraints loose(glm::vec2 maximum) noexcept
    {
        return {{0.0f, 0.0f}, maximum};
    }

    [[nodiscard]] static constexpr Constraints tight(glm::vec2 size) noexcept
    {
        return {size, size};
    }

    [[nodiscard]] static constexpr Constraints unbounded() noexcept { return {}; }

    [[nodiscard]] constexpr glm::vec2 clamp(glm::vec2 size) const noexcept
    {
        return {std::clamp(size.x, min.x, std::max(min.x, max.x)),
                std::clamp(size.y, min.y, std::max(min.y, max.y))};
    }

    /// The space left for a child once @p by is taken out of all four sides.
    [[nodiscard]] constexpr Constraints deflate(const Thickness& by) const noexcept
    {
        const glm::vec2 lost = by.collapsed();
        return {{std::max(0.0f, min.x - lost.x), std::max(0.0f, min.y - lost.y)},
                {std::max(0.0f, max.x - lost.x), std::max(0.0f, max.y - lost.y)}};
    }

    /// The same range with one axis' upper bound removed -- what a container
    /// measuring along an axis it does not itself bound hands its children.
    [[nodiscard]] constexpr Constraints unbound(Axis axis) const noexcept
    {
        Constraints result = *this;
        if (axis == Axis::Horizontal)
            result.max.x = kUnbounded;
        else
            result.max.y = kUnbounded;
        return result;
    }
};

/// Which grid cell a child occupies. Lives on the child (like WPF's
/// Grid.Row) so a Grid needs no side table to keep in sync with its children.
struct GridCell {
    u16 row = 0;
    u16 column = 0;
    u16 rowSpan = 1;
    u16 columnSpan = 1;

    [[nodiscard]] constexpr bool operator==(const GridCell&) const noexcept = default;
};

/**
 * @brief Everything about a widget's own placement.
 *
 * A plain struct, edited in place through Widget::layout(), which marks the
 * tree for re-layout on the way out:
 *
 * @code
 * button.layout() = {.width = Length::fill(), .margin = Thickness::all(4.0f)};
 * label.layout().hAlign = Alignment::Center;
 * @endcode
 */
struct LayoutSpec {
    Length width{};
    Length height{};

    Thickness margin{};  //! Outside the widget: space between it and its siblings.
    Thickness padding{}; //! Inside the widget: space between it and its content.

    Alignment hAlign = Alignment::Stretch;
    Alignment vAlign = Alignment::Stretch;

    f32 minWidth = 0.0f;
    f32 minHeight = 0.0f;
    f32 maxWidth = kUnbounded;
    f32 maxHeight = kUnbounded;

    GridCell cell{};

    [[nodiscard]] constexpr glm::vec2 minSize() const noexcept { return {minWidth, minHeight}; }
    [[nodiscard]] constexpr glm::vec2 maxSize() const noexcept { return {maxWidth, maxHeight}; }
};

/// Offset of an aligned box of @p extent within a slot of @p available.
[[nodiscard]] constexpr f32 alignOffset(Alignment alignment, f32 available, f32 extent) noexcept
{
    const f32 slack = std::max(0.0f, available - extent);

    switch (alignment)
    {
    case Alignment::Center:
        return slack * 0.5f;
    case Alignment::End:
        return slack;
    case Alignment::Start:
    case Alignment::Stretch:
        break;
    }

    return 0.0f;
}

/// @{
/// Axis-agnostic accessors, so a Column and a Row are one implementation.
[[nodiscard]] constexpr f32 along(Axis axis, glm::vec2 size) noexcept
{
    return axis == Axis::Horizontal ? size.x : size.y;
}

[[nodiscard]] constexpr f32 across(Axis axis, glm::vec2 size) noexcept
{
    return axis == Axis::Horizontal ? size.y : size.x;
}

[[nodiscard]] constexpr glm::vec2 fromAxes(Axis axis, f32 main, f32 cross) noexcept
{
    return axis == Axis::Horizontal ? glm::vec2{main, cross} : glm::vec2{cross, main};
}
/// @}

} // namespace aura3d::ui

#endif // AURA_UI_LAYOUT_H
