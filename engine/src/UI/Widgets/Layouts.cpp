#include "aura/UI/Widgets/Layouts.h"

#include <algorithm>

namespace aura3d::ui {

// =============================================================================
// Box
// =============================================================================

Box::Box(Axis axis) : _axis(axis) {}

void Box::setSpacing(f32 pixels)
{
    if (_spacing == pixels)
        return;

    _spacing = std::max(0.0f, pixels);
    invalidateLayout();
}

void Box::setMainAlignment(Alignment alignment)
{
    if (_mainAlignment == alignment)
        return;

    _mainAlignment = alignment;
    invalidateLayout();
}

const Length& Box::_mainLength(const Widget& child) const noexcept
{
    return _axis == Axis::Horizontal ? child.layout().width : child.layout().height;
}

glm::vec2 Box::measureContent(const Constraints& available)
{
    _base.assign(childCount(), 0.0f);

    /*
     * Children are measured with the main axis unbounded, so a Fill child
     * reports the size its *content* needs rather than swallowing the whole
     * box. That intrinsic size is what an auto-sized ancestor is sized from;
     * the share a Fill child actually receives is decided in arrangeContent(),
     * where the leftover is known.
     */
    const Constraints childSpace = available.unbound(_axis);

    f32 main = 0.0f;
    f32 cross = 0.0f;
    usize laidOut = 0;

    for (usize i = 0; i < childCount(); ++i)
    {
        Widget& child = childAt(i);
        if (child.visibility() == Visibility::Collapsed)
            continue;

        const glm::vec2 desired = child.measure(childSpace);

        _base[i] = along(_axis, desired);
        main += _base[i];
        cross = std::max(cross, across(_axis, desired));
        ++laidOut;
    }

    if (laidOut > 1)
        main += _spacing * static_cast<f32>(laidOut - 1);

    return fromAxes(_axis, main, cross);
}

void Box::arrangeContent(const Rect& content)
{
    //! measureContent() fills _base and always runs first in a normal pass;
    //! the guard is for a container arranged without one, where a stale cache
    //! would be indexed out of bounds rather than merely be wrong.
    if (_base.size() != childCount())
        _base.assign(childCount(), 0.0f);

    const f32 extent = along(_axis, content.size());

    f32 fixed = 0.0f;
    f32 weight = 0.0f;
    usize laidOut = 0;

    for (usize i = 0; i < childCount(); ++i)
    {
        const Widget& child = childAt(i);
        if (child.visibility() == Visibility::Collapsed)
            continue;

        ++laidOut;

        const Length& length = _mainLength(child);
        if (length.isFill())
            weight += length.value;
        else
            fixed += _base[i];
    }

    const f32 gaps = laidOut > 1 ? _spacing * static_cast<f32>(laidOut - 1) : 0.0f;
    const f32 leftover = std::max(0.0f, extent - fixed - gaps);

    //! With nothing filling, the group is placed as a whole -- what makes
    //! Alignment::Center centre a row of buttons rather than stretch them.
    f32 cursor = along(_axis, content.min);
    if (weight <= 0.0f)
        cursor += alignOffset(_mainAlignment, extent, fixed + gaps);

    for (usize i = 0; i < childCount(); ++i)
    {
        Widget& child = childAt(i);
        if (child.visibility() == Visibility::Collapsed)
        {
            child.arrange(Rect{});
            continue;
        }

        const Length& length = _mainLength(child);
        const f32 size = length.isFill() ? leftover * (length.value / weight) : _base[i];

        const glm::vec2 origin = fromAxes(_axis, cursor, across(_axis, content.min));
        const glm::vec2 slot = fromAxes(_axis, size, across(_axis, content.size()));

        child.arrange(Rect::fromSize(origin, slot));

        cursor += size + _spacing;
    }
}

// =============================================================================
// Grid
// =============================================================================

Grid::Grid() = default;

void Grid::setColumns(std::vector<Length> columns)
{
    _columns = std::move(columns);
    invalidateLayout();
}

void Grid::setRows(std::vector<Length> rows)
{
    _rows = std::move(rows);
    invalidateLayout();
}

void Grid::setSpacing(f32 pixels)
{
    setColumnSpacing(pixels);
    setRowSpacing(pixels);
}

void Grid::setColumnSpacing(f32 pixels)
{
    if (_columnSpacing == pixels)
        return;

    _columnSpacing = std::max(0.0f, pixels);
    invalidateLayout();
}

void Grid::setRowSpacing(f32 pixels)
{
    if (_rowSpacing == pixels)
        return;

    _rowSpacing = std::max(0.0f, pixels);
    invalidateLayout();
}

usize Grid::_trackCount(const std::vector<Length>& tracks, usize used) noexcept
{
    //! A child placed past the declared tracks extends the grid with Auto
    //! ones, so a form does not have to declare its row count up front.
    return std::max(tracks.size(), used);
}

void Grid::_resolveTracks(const Constraints& available)
{
    usize columns = 0;
    usize rows = 0;

    for (usize i = 0; i < childCount(); ++i)
    {
        const GridCell& cell = childAt(i).layout().cell;
        columns = std::max<usize>(columns, static_cast<usize>(cell.column) + cell.columnSpan);
        rows = std::max<usize>(rows, static_cast<usize>(cell.row) + cell.rowSpan);
    }

    columns = _trackCount(_columns, columns);
    rows = _trackCount(_rows, rows);

    _columnSizes.assign(columns, 0.0f);
    _rowSizes.assign(rows, 0.0f);

    const auto trackAt = [](const std::vector<Length>& tracks, usize index) {
        return index < tracks.size() ? tracks[index] : Length::automatic();
    };

    //! Fixed tracks first: an Auto track's budget is what the fixed ones left,
    //! so a wrapping label in an Auto column is told a truthful width.
    f32 fixedWidth = 0.0f;
    for (usize i = 0; i < columns; ++i)
    {
        if (const auto resolved = resolveLength(trackAt(_columns, i), available.max.x))
        {
            if (!trackAt(_columns, i).isFill())
                _columnSizes[i] = *resolved;
        }
        fixedWidth += _columnSizes[i];
    }

    f32 fixedHeight = 0.0f;
    for (usize i = 0; i < rows; ++i)
    {
        if (const auto resolved = resolveLength(trackAt(_rows, i), available.max.y))
        {
            if (!trackAt(_rows, i).isFill())
                _rowSizes[i] = *resolved;
        }
        fixedHeight += _rowSizes[i];
    }

    const f32 spareWidth = std::max(0.0f, available.max.x - fixedWidth);
    const f32 spareHeight = std::max(0.0f, available.max.y - fixedHeight);

    for (usize i = 0; i < childCount(); ++i)
    {
        Widget& child = childAt(i);
        if (child.visibility() == Visibility::Collapsed)
            continue;

        const GridCell& cell = child.layout().cell;
        if (cell.column >= columns || cell.row >= rows)
            continue;

        const bool autoColumn = trackAt(_columns, cell.column).isAuto();
        const bool autoRow = trackAt(_rows, cell.row).isAuto();

        const glm::vec2 budget{autoColumn ? spareWidth : _columnSizes[cell.column],
                               autoRow ? spareHeight : _rowSizes[cell.row]};

        const glm::vec2 desired = child.measure(Constraints::loose(budget));

        //! A spanning child is not allowed to drive an Auto track: which of
        //! the tracks it crosses should grow is genuinely ambiguous, and the
        //! answer people expect is "none of them".
        if (autoColumn && cell.columnSpan == 1)
            _columnSizes[cell.column] = std::max(_columnSizes[cell.column], desired.x);

        if (autoRow && cell.rowSpan == 1)
            _rowSizes[cell.row] = std::max(_rowSizes[cell.row], desired.y);
    }
}

void Grid::_distribute(std::vector<f32>& sizes, const std::vector<Length>& tracks, f32 available,
                       f32 spacing) const
{
    f32 used = 0.0f;
    f32 weight = 0.0f;

    for (usize i = 0; i < sizes.size(); ++i)
    {
        const Length track = i < tracks.size() ? tracks[i] : Length::automatic();

        if (track.isFill())
            weight += track.value;
        else
            used += sizes[i];
    }

    if (weight <= 0.0f)
        return;

    const f32 gaps = sizes.size() > 1 ? spacing * static_cast<f32>(sizes.size() - 1) : 0.0f;
    const f32 leftover = std::max(0.0f, available - used - gaps);

    for (usize i = 0; i < sizes.size(); ++i)
    {
        const Length track = i < tracks.size() ? tracks[i] : Length::automatic();
        if (track.isFill())
            sizes[i] = leftover * (track.value / weight);
    }
}

glm::vec2 Grid::measureContent(const Constraints& available)
{
    _resolveTracks(available);

    const auto total = [](const std::vector<f32>& sizes, f32 spacing) {
        f32 sum = 0.0f;
        for (const f32 size : sizes)
            sum += size;

        return sizes.size() > 1 ? sum + spacing * static_cast<f32>(sizes.size() - 1) : sum;
    };

    return {total(_columnSizes, _columnSpacing), total(_rowSizes, _rowSpacing)};
}

void Grid::arrangeContent(const Rect& content)
{
    _distribute(_columnSizes, _columns, content.width(), _columnSpacing);
    _distribute(_rowSizes, _rows, content.height(), _rowSpacing);

    const auto offsetOf = [](const std::vector<f32>& sizes, usize index, f32 spacing) {
        f32 offset = 0.0f;
        for (usize i = 0; i < index && i < sizes.size(); ++i)
            offset += sizes[i] + spacing;

        return offset;
    };

    const auto extentOf = [](const std::vector<f32>& sizes, usize first, usize span, f32 spacing) {
        f32 extent = 0.0f;
        for (usize i = first; i < first + span && i < sizes.size(); ++i)
            extent += sizes[i] + spacing;

        return std::max(0.0f, extent - spacing);
    };

    for (usize i = 0; i < childCount(); ++i)
    {
        Widget& child = childAt(i);
        if (child.visibility() == Visibility::Collapsed)
        {
            child.arrange(Rect{});
            continue;
        }

        const GridCell& cell = child.layout().cell;

        const glm::vec2 origin{
            content.min.x + offsetOf(_columnSizes, cell.column, _columnSpacing),
            content.min.y + offsetOf(_rowSizes, cell.row, _rowSpacing)};

        const glm::vec2 size{
            extentOf(_columnSizes, cell.column, std::max<u16>(1, cell.columnSpan), _columnSpacing),
            extentOf(_rowSizes, cell.row, std::max<u16>(1, cell.rowSpan), _rowSpacing)};

        child.arrange(Rect::fromSize(origin, size));
    }
}

// =============================================================================
// Spacer
// =============================================================================

Spacer::Spacer(f32 pixels) : _pixels(std::max(0.0f, pixels))
{
    setHitTestVisible(false);

    if (_pixels > 0.0f)
        return;

    layout().width = Length::fill();
    layout().height = Length::fill();
}

glm::vec2 Spacer::measureContent(const Constraints&)
{
    if (_pixels <= 0.0f)
        return {0.0f, 0.0f};

    //! Thick along the parent's axis and nothing across it: a 24px gap in a
    //! Column must not also force the column 24px wide.
    const Axis axis = parent() ? parent()->mainAxis() : Axis::Vertical;
    return fromAxes(axis, _pixels, 0.0f);
}

// =============================================================================
// ScrollView
// =============================================================================

ScrollView::ScrollView()
{
    _part = Part::ScrollView;
}

Widget& ScrollView::setContent(std::unique_ptr<Widget> content)
{
    clearChildren();
    return adopt(std::move(content));
}

void ScrollView::setScrollable(bool horizontal, bool vertical)
{
    if (_horizontal == horizontal && _vertical == vertical)
        return;

    _horizontal = horizontal;
    _vertical = vertical;
    invalidateLayout();
}

void ScrollView::setSmooth(bool smooth)
{
    _smooth = smooth;
}

f32 ScrollView::_gutter() const noexcept
{
    return theme().metrics.scrollbarWidth;
}

Thickness ScrollView::contentInsets() const noexcept
{
    Thickness insets = layout().padding;

    /*
     * Reserved whenever the axis scrolls at all, rather than only when the
     * content currently overflows: a gutter that appears and disappears
     * changes the viewport width, which re-wraps the content, which can change
     * whether it overflows. Reserving unconditionally is the fixed point.
     */
    if (_vertical)
        insets.right += _gutter();

    if (_horizontal)
        insets.bottom += _gutter();

    return insets;
}

glm::vec2 ScrollView::maxOffset() const noexcept
{
    const auto limit = glm::max(glm::vec2{0.0f}, _contentSize - _viewportSize);
    return {_horizontal ? limit.x : 0.0f, _vertical ? limit.y : 0.0f};
}

glm::vec2 ScrollView::measureContent(const Constraints& available)
{
    Widget* content = _content();
    if (!content)
    {
        _contentSize = {0.0f, 0.0f};
        return {0.0f, 0.0f};
    }

    //! Unbounded on the scrolling axes: the child must report the size it
    //! actually wants, which is the whole point of a scroll view.
    Constraints childSpace = available;
    if (_horizontal)
        childSpace.max.x = kUnbounded;
    if (_vertical)
        childSpace.max.y = kUnbounded;

    _contentSize = content->measure(childSpace);

    //! The view itself asks for the viewport it was offered, not for the
    //! content's size -- otherwise it would grow to fit and never scroll.
    return {_horizontal ? 0.0f : _contentSize.x, _vertical ? 0.0f : _contentSize.y};
}

void ScrollView::arrangeContent(const Rect& content)
{
    _viewportSize = content.size();

    Widget* child = _content();
    if (!child)
    {
        _offset.reset({0.0f, 0.0f});
        return;
    }

    const glm::vec2 limit = maxOffset();
    const glm::vec2 clamped = glm::clamp(_offset.value(), glm::vec2{0.0f}, limit);

    if (clamped != _offset.value())
        _offset.reset(clamped);
    else if (const auto target = glm::clamp(_offset.target(), glm::vec2{0.0f}, limit);
             target != _offset.target())
        _offset.to(target, 0.12f);

    const glm::vec2 size{_horizontal ? _contentSize.x : content.width(),
                         _vertical ? _contentSize.y : content.height()};

    child->arrange(Rect::fromSize(content.min - _offset.value(), size));
}

void ScrollView::_setOffset(glm::vec2 offset, bool animate)
{
    offset = glm::clamp(offset, glm::vec2{0.0f}, maxOffset());

    if (offset == _offset.target() && (animate || offset == _offset.value()))
        return;

    if (animate && _smooth)
    {
        _offset.to(offset, 0.12f);
        setAnimating(true);
    }
    else
    {
        _offset.reset(offset);
    }

    invalidateLayout();
}

void ScrollView::scrollTo(glm::vec2 offset) { _setOffset(offset, false); }

void ScrollView::scrollBy(glm::vec2 delta) { _setOffset(_offset.target() + delta, true); }

void ScrollView::scrollIntoView(const Rect& rect)
{
    const Rect viewport = contentRect();

    glm::vec2 delta{0.0f};

    if (rect.min.x < viewport.min.x)
        delta.x = rect.min.x - viewport.min.x;
    else if (rect.max.x > viewport.max.x)
        delta.x = rect.max.x - viewport.max.x;

    if (rect.min.y < viewport.min.y)
        delta.y = rect.min.y - viewport.min.y;
    else if (rect.max.y > viewport.max.y)
        delta.y = rect.max.y - viewport.max.y;

    if (delta != glm::vec2{0.0f})
        _setOffset(_offset.target() + delta, true);
}

bool ScrollView::onTick(f32 deltaSeconds)
{
    const bool running = _offset.tick(deltaSeconds);
    invalidateLayout();
    return running;
}

bool ScrollView::onWheel(const WheelEvent& event)
{
    const f32 step = theme().metrics.rowHeight * 3.0f;

    //! Shift turns a vertical wheel horizontal, which is the only way to
    //! scroll sideways on a mouse that has one wheel.
    const bool sideways = event.mods.shift || (!_vertical && _horizontal);

    const glm::vec2 delta = sideways ? glm::vec2{-(event.delta.x + event.delta.y) * step, 0.0f}
                                     : glm::vec2{-event.delta.x * step, -event.delta.y * step};

    const glm::vec2 limit = maxOffset();
    const glm::vec2 target = _offset.target();

    //! Declined at the end of travel so the parent scroll view takes over,
    //! rather than the inner one swallowing every notch.
    const bool canScroll = (delta.x < 0.0f && target.x > 0.0f) ||
                           (delta.x > 0.0f && target.x < limit.x) ||
                           (delta.y < 0.0f && target.y > 0.0f) ||
                           (delta.y > 0.0f && target.y < limit.y);

    if (!canScroll)
        return false;

    _setOffset(target + delta, true);
    return true;
}

ScrollView::Bar ScrollView::_bar(Axis axis) const
{
    Bar bar{};

    const bool enabled = axis == Axis::Vertical ? _vertical : _horizontal;
    if (!enabled)
        return bar;

    const f32 gutter = _gutter();
    const f32 viewport = along(axis, _viewportSize);
    const f32 content = along(axis, _contentSize);

    if (content <= viewport || viewport <= 0.0f)
        return bar;

    const Rect inner = deflate(bounds(), layout().padding);

    bar.track = axis == Axis::Vertical
                    ? Rect{{inner.max.x - gutter, inner.min.y},
                           {inner.max.x, inner.max.y - (_horizontal ? gutter : 0.0f)}}
                    : Rect{{inner.min.x, inner.max.y - gutter},
                           {inner.max.x - (_vertical ? gutter : 0.0f), inner.max.y}};

    const f32 trackExtent = along(axis, bar.track.size());
    const f32 thumbExtent = std::min(trackExtent, std::max(gutter * 2.0f, trackExtent * (viewport / content)));

    const f32 travel = std::max(0.0f, trackExtent - thumbExtent);
    const f32 progress = along(axis, _offset.value()) / std::max(1.0f, content - viewport);

    const f32 start = along(axis, bar.track.min) + travel * std::clamp(progress, 0.0f, 1.0f);

    bar.thumb = axis == Axis::Vertical
                    ? Rect{{bar.track.min.x, start}, {bar.track.max.x, start + thumbExtent}}
                    : Rect{{start, bar.track.min.y}, {start + thumbExtent, bar.track.max.y}};

    bar.active = true;
    return bar;
}

void ScrollView::paintChildren(DrawList& out)
{
    Widget::paintChildren(out);

    const auto paintBar = [this, &out](Axis axis) {
        const Bar bar = _bar(axis);
        if (!bar.active)
            return;

        const WidgetStyle track = theme()[Part::ScrollTrack];
        const WidgetStyle thumb = theme()[Part::ScrollThumb];

        const bool dragging = _dragActive && _dragging == axis;

        out.fillRect(bar.track, track.surface.normal, Corners::all(track.rounding));
        out.fillRect(bar.thumb, thumb.surface.pick(dragging, isHovered()),
                     Corners::all(thumb.rounding));
    };

    /*
     * Outside the clip the children were drawn under: the bars live in the
     * gutters, which contentRect() already excludes, and paintChildren() is
     * called with that clip in force.
     */
    out.popClip();
    paintBar(Axis::Vertical);
    paintBar(Axis::Horizontal);
    out.pushClip(contentRect());
}

bool ScrollView::onPointerDown(const PointerEvent& event)
{
    if (event.button != PointerButton::Left)
        return false;
    for (const Axis axis : {Axis::Vertical, Axis::Horizontal})
    {
        const Bar bar = _bar(axis);
        if (!bar.active || !bar.track.contains(event.position))
            continue;

        _dragging = axis;
        _dragActive = true;
        capturePointer();

        if (bar.thumb.contains(event.position))
        {
            _grab = along(axis, event.position) - along(axis, bar.thumb.min);
        }
        else
        {
            //! Clicking the track jumps the thumb to the cursor and then drags
            //! it, so a click and a click-drag do the same thing.
            _grab = along(axis, bar.thumb.size()) * 0.5f;
            onPointerMove(event);
        }

        capturePointer();
        invalidatePaint();
        return true;
    }

    return false;
}

bool ScrollView::onPointerMove(const PointerEvent& event)
{
    if (!_dragActive || !hasPointerCapture())
        return false;

    const Bar bar = _bar(_dragging);
    if (!bar.active)
        return true;

    const f32 trackExtent = along(_dragging, bar.track.size());
    const f32 thumbExtent = along(_dragging, bar.thumb.size());
    const f32 travel = trackExtent - thumbExtent;

    if (travel <= 0.0f)
        return true;

    const f32 local = along(_dragging, event.position) - along(_dragging, bar.track.min) - _grab;
    const f32 progress = std::clamp(local / travel, 0.0f, 1.0f);

    glm::vec2 offset = _offset.target();
    const f32 range = along(_dragging, maxOffset());

    if (_dragging == Axis::Vertical)
        offset.y = range * progress;
    else
        offset.x = range * progress;

    _setOffset(offset, false);
    return true;
}

bool ScrollView::onPointerUp(const PointerEvent& event)
{
    if (!_dragActive || event.button != PointerButton::Left)
        return false;

    _dragActive = false;
    releasePointer();
    invalidatePaint();
    return true;
}

void ScrollView::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);
    out.role = Role::ScrollView;
}

void ScrollView::onPointerCancel()
{
    _dragActive = false;
    invalidatePaint();
}

} // namespace aura3d::ui
