#ifndef AURA_UI_WIDGETS_LAYOUTS_H
#define AURA_UI_WIDGETS_LAYOUTS_H

#pragma once

#include <vector>

#include "aura/UI/Core/Animation.h"
#include "aura/UI/Widget.h"

/**
 * @file Layouts.h
 * @brief The containers: flex, grid, overlay and scrolling.
 *
 * None of them draws anything by default -- they read @ref Part::Container,
 * which every palette leaves transparent. Give one a surface by styling it:
 *
 * @code
 * auto& card = page.add<Column>();
 * card.style().fill(theme.palette().surface).rounded(8.0f);
 * card.layout().padding = Thickness::all(16.0f);
 * @endcode
 */

namespace aura3d::ui {

/**
 * @class Box
 * @brief Stacks children along one axis, sharing what is left over.
 *
 * A child whose main-axis @ref Length is Fill takes a weighted share of the
 * space the fixed children did not use. On the cross axis, Alignment::Stretch
 * (the default) makes every child as wide as the box.
 *
 * @code
 * auto& row = panel.add<Row>();
 * row.setSpacing(8.0f);
 * row.add<Button>("OK").layout().width = Length::fill();
 * row.add<Button>("Cancel").layout().width = Length::fill();  // an even split
 * @endcode
 */
class Box : public Widget {
public:
    explicit Box(Axis axis);

    [[nodiscard]] Axis axis() const noexcept { return _axis; }
    [[nodiscard]] Axis mainAxis() const noexcept override { return _axis; }

    /// Gap between adjacent children, in logical pixels.
    void setSpacing(f32 pixels);
    [[nodiscard]] f32 spacing() const noexcept { return _spacing; }

    /// Where the children sit as a group when they do not fill the main axis
    /// and nothing in them is a Fill. Start by default.
    void setMainAlignment(Alignment alignment);
    [[nodiscard]] Alignment mainAlignment() const noexcept { return _mainAlignment; }

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void arrangeContent(const Rect& content) override;

private:
    /// This child's sizing rule along the box's own axis.
    [[nodiscard]] const Length& _mainLength(const Widget& child) const noexcept;

    Axis _axis = Axis::Vertical;
    f32 _spacing = 0.0f;
    Alignment _mainAlignment = Alignment::Start;

    /// Main-axis extent each child asked for, kept from measure so arrange
    /// does not have to re-derive it. Reused across frames.
    std::vector<f32> _base;
};

/// A vertical @ref Box.
class Column final : public Box {
public:
    Column() : Box(Axis::Vertical) {}
};

/// A horizontal @ref Box.
class Row final : public Box {
public:
    Row() : Box(Axis::Horizontal) {}
};

/**
 * @class Grid
 * @brief Rows and columns of @ref Length, with children placed by cell.
 *
 * Track sizes follow the same vocabulary as everything else: @c px is fixed,
 * @c automatic sizes to the widest child in the track, @c fill shares the
 * leftover.
 *
 * @code
 * auto& form = page.add<Grid>();
 * form.setColumns({Length::automatic(), Length::fill()});
 * form.setSpacing(8.0f);
 *
 * form.addAt<Label>(0, 0, "Name");
 * form.addAt<TextField>(0, 1);
 * @endcode
 */
class Grid final : public Widget {
public:
    Grid();

    /// Track definitions. A child placed past the last track extends the grid
    /// with Auto tracks, so a form can be built without counting rows first.
    void setColumns(std::vector<Length> columns);
    void setRows(std::vector<Length> rows);

    void setSpacing(f32 pixels);
    void setColumnSpacing(f32 pixels);
    void setRowSpacing(f32 pixels);

    /// Constructs a child in the given cell and returns it.
    template <class W, class... Args>
    W& addAt(u16 row, u16 column, Args&&... args)
    {
        W& child = add<W>(std::forward<Args>(args)...);
        child.layout().cell = GridCell{.row = row, .column = column};
        return child;
    }

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void arrangeContent(const Rect& content) override;

private:
    /// Resolves both track axes against @p available, measuring every child
    /// once. Auto tracks come out of the children's desired sizes, so this is
    /// the only place a child is measured.
    void _resolveTracks(const Constraints& available);

    /// Hands the leftover extent to the Fill tracks and turns the track sizes
    /// into offsets.
    void _distribute(std::vector<f32>& sizes, const std::vector<Length>& tracks, f32 available,
                     f32 spacing) const;

    [[nodiscard]] static usize _trackCount(const std::vector<Length>& tracks, usize used) noexcept;

    std::vector<Length> _columns;
    std::vector<Length> _rows;

    //! Resolved extents, retained across frames so a steady grid re-lays out
    //! without allocating.
    std::vector<f32> _columnSizes;
    std::vector<f32> _rowSizes;

    f32 _columnSpacing = 0.0f;
    f32 _rowSpacing = 0.0f;
};

/// Children overlaid in the same rectangle, each aligned within it. The base
/// for a badge over an icon, or a dialog over a page.
class Stack final : public Widget {
public:
    Stack() = default;
};

/**
 * @class Spacer
 * @brief Empty space: a fixed gap, or a claim on whatever is left over.
 *
 * @code
 * row.add<Spacer>();          // pushes what follows to the far end
 * column.add<Spacer>(24.0f);  // a 24px gap
 * @endcode
 */
class Spacer final : public Widget {
public:
    explicit Spacer(f32 pixels = 0.0f);

protected:
    glm::vec2 measureContent(const Constraints& available) override;

private:
    //! Zero means "take the leftover"; anything else is a gap along whichever
    //! axis the parent stacks on.
    f32 _pixels = 0.0f;
};

/**
 * @class ScrollView
 * @brief A viewport onto a taller (or wider) child.
 *
 * Measures its content unbounded on the scrolling axes, so the child reports
 * the size it actually wants rather than being squeezed into the viewport.
 * The wheel scrolls; the bars can be dragged.
 *
 * @code
 * auto& list = page.add<ScrollView>();
 * auto& items = list.setContent<Column>();
 * for (const auto& entry : entries) items.add<Label>(entry);
 * @endcode
 */
class ScrollView final : public Widget {
public:
    ScrollView();

    /// Replaces the scrolled child and returns it.
    template <class W, class... Args>
    W& setContent(Args&&... args)
    {
        clearChildren();
        return add<W>(std::forward<Args>(args)...);
    }

    Widget& setContent(std::unique_ptr<Widget> content);

    /// @{
    /// Which axes scroll. A disabled axis reserves no gutter and passes the
    /// wheel to the parent.
    void setScrollable(bool horizontal, bool vertical);
    [[nodiscard]] bool scrollsHorizontally() const noexcept { return _horizontal; }
    [[nodiscard]] bool scrollsVertically() const noexcept { return _vertical; }
    /// @}

    /// Wheel and bar drags ease to their target instead of jumping. On by
    /// default; turn it off for a list being driven programmatically.
    void setSmooth(bool smooth);

    void scrollTo(glm::vec2 offset);
    void scrollBy(glm::vec2 delta);

    /// Scrolls the smallest distance that brings @p rect (in surface
    /// coordinates) into view. What focus traversal needs.
    void scrollIntoView(const Rect& rect);

    [[nodiscard]] glm::vec2 offset() const noexcept { return _offset.value(); }
    [[nodiscard]] glm::vec2 contentSize() const noexcept { return _contentSize; }

    /// Largest offset the content allows; zero on an axis that fits.
    [[nodiscard]] glm::vec2 maxOffset() const noexcept;

    [[nodiscard]] Thickness contentInsets() const noexcept override;

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void arrangeContent(const Rect& content) override;

    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }

    void paintChildren(DrawList& out) override;

    bool onWheel(const WheelEvent& event) override;
    bool onPointerDown(const PointerEvent& event) override;
    bool onPointerMove(const PointerEvent& event) override;
    bool onPointerUp(const PointerEvent& event) override;
    void onPointerCancel() override;

    bool onTick(f32 deltaSeconds) override;

private:
    /// Track and thumb rectangles for one axis, or an empty pair when that
    /// axis does not overflow.
    struct Bar {
        Rect track{};
        Rect thumb{};
        bool active = false;
    };

    [[nodiscard]] Bar _bar(Axis axis) const;

    void _setOffset(glm::vec2 offset, bool animate);

    /// Gutter width, from the theme. Zero on an axis that does not scroll.
    [[nodiscard]] f32 _gutter() const noexcept;

    [[nodiscard]] Widget* _content() const noexcept
    {
        return childCount() > 0 ? &childAt(0) : nullptr;
    }

    Transition<glm::vec2> _offset{glm::vec2{0.0f}};
    glm::vec2 _contentSize{0.0f};
    glm::vec2 _viewportSize{0.0f};

    bool _horizontal = false;
    bool _vertical = true;
    bool _smooth = true;

    //! Which bar is being dragged, and where inside its thumb the drag
    //! started -- without the grab offset the thumb would jump to the cursor.
    Axis _dragging = Axis::Vertical;
    bool _dragActive = false;
    f32 _grab = 0.0f;
};

} // namespace aura3d::ui

#endif // AURA_UI_WIDGETS_LAYOUTS_H
