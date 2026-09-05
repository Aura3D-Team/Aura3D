#include "aura/UI/Widget.h"

#include <algorithm>

#include "aura/UI/UIRoot.h"

namespace aura3d::ui {

namespace {

/// The theme every widget falls back to before it is attached to a root, so
/// resolvedStyle() and theme() are safe to call from a constructor.
[[nodiscard]] const Theme& detachedTheme()
{
    static const Theme fallback{};
    return fallback;
}

} // namespace

Widget::Widget() = default;

WidgetRef::WidgetRef(Widget* widget) : _widget(widget)
{
    if (widget)
    {
        if (!widget->_alive)
            widget->_alive = std::make_shared<u8>(0);
        _alive = widget->_alive;
    }
}

Widget::~Widget()
{
    _alive.reset();
    /*
     * Children are destroyed by _children's own destructor, but the root has
     * to be told first: it may be holding this subtree as the focused, hovered
     * or capturing widget, and those pointers must not outlive it.
     */
    if (_root)
        _setRoot(nullptr);
}

// -----------------------------------------------------------------------------
// Tree
// -----------------------------------------------------------------------------

void Widget::_insert(usize index, std::unique_ptr<Widget> child)
{
    if (!child)
        return;

    index = std::min(index, _children.size());

    Widget& reference = *child;
    child->_parent = this;

    _children.insert(_children.begin() + static_cast<ptrdiff_t>(index), std::move(child));

    if (_root)
        reference._setRoot(_root);

    onChildAdded(reference, index);
    invalidateLayout();
}

Widget& Widget::adopt(std::unique_ptr<Widget> child)
{
    Widget& reference = *child;
    _insert(_children.size(), std::move(child));
    return reference;
}

std::unique_ptr<Widget> Widget::detach(Widget& child)
{
    const auto it = std::ranges::find_if(
        _children, [&child](const std::unique_ptr<Widget>& held) { return held.get() == &child; });

    if (it == _children.end())
        return nullptr;

    const auto index = static_cast<usize>(it - _children.begin());

    std::unique_ptr<Widget> owned = std::move(*it);
    _children.erase(it);

    owned->_setRoot(nullptr);
    owned->_parent = nullptr;

    onChildRemoved(index);
    invalidateLayout();

    return owned;
}

void Widget::remove(Widget& child)
{
    //! Destroyed here, at the end of the statement, rather than inside the
    //! erase: ~Widget needs a live parent to unregister itself from the root.
    const std::unique_ptr<Widget> owned = detach(child);
}

void Widget::clearChildren()
{
    while (!_children.empty())
    {
        const std::unique_ptr<Widget> owned = detach(*_children.back());
    }
}

Widget* Widget::find(std::string_view name) noexcept
{
    if (_name == name)
        return this;

    for (const std::unique_ptr<Widget>& child : _children)
    {
        if (Widget* found = child->find(name))
            return found;
    }

    return nullptr;
}

void Widget::_setRoot(UIRoot* root)
{
    if (_root == root)
        return;

    if (_root)
    {
        _root->_forget(*this);
        onDetach();
    }

    _root = root;

    for (const std::unique_ptr<Widget>& child : _children)
        child->_setRoot(root);

    if (_root)
    {
        if (_animating)
            _root->_setAnimating(*this, true);
        onAttach();
    }
}

// -----------------------------------------------------------------------------
// Invalidation
// -----------------------------------------------------------------------------

void Widget::invalidateLayout() noexcept
{
    /*
     * Walks to the root rather than notifying it directly, because every
     * ancestor's desired size may have changed too -- a label whose text grew
     * can widen the panel three levels up. The walk stops early at a node
     * already marked, so a burst of changes in one subtree costs one walk.
     */
    for (Widget* node = this; node != nullptr; node = node->_parent)
    {
        if (node->_layoutDirty)
            break;

        node->_layoutDirty = true;
    }

    if (_root)
        _root->_requestLayout();
}

void Widget::invalidatePaint() noexcept
{
    if (_root)
        _root->_requestPaint();
}

// -----------------------------------------------------------------------------
// Layout
// -----------------------------------------------------------------------------

glm::vec2 Widget::measure(const Constraints& space)
{
    if (_visibility == Visibility::Collapsed)
    {
        _measured = {0.0f, 0.0f};
        _desired = {0.0f, 0.0f};
        _layoutDirty = false;
        return _desired;
    }

    const Constraints box = space.deflate(_layout.margin);

    const std::optional<f32> pinnedWidth = resolveLength(_layout.width, box.max.x);
    const std::optional<f32> pinnedHeight = resolveLength(_layout.height, box.max.y);

    _fixedWidth = pinnedWidth.has_value() && !_layout.width.isFill();
    _fixedHeight = pinnedHeight.has_value() && !_layout.height.isFill();

    //! Content is measured inside whatever the box will be, minus padding: a
    //! wrapping label inside a 200px panel must be told about the 200, not
    //! about the window.
    const glm::vec2 outer{pinnedWidth.value_or(box.max.x), pinnedHeight.value_or(box.max.y)};

    const Thickness insets = contentInsets();

    const Constraints inner = Constraints::loose(
        {std::max(0.0f, std::min(outer.x, _layout.maxWidth) - insets.horizontal()),
         std::max(0.0f, std::min(outer.y, _layout.maxHeight) - insets.vertical())});

    const glm::vec2 content = measureContent(inner);

    glm::vec2 size{pinnedWidth.value_or(content.x + insets.horizontal()),
                   pinnedHeight.value_or(content.y + insets.vertical())};

    size = glm::max(size, _layout.minSize());
    size = glm::min(size, _layout.maxSize());
    size = box.clamp(size);

    _measured = size;
    _desired = size + _layout.margin.collapsed();
    _layoutDirty = false;

    return _desired;
}

void Widget::arrange(const Rect& slot)
{
    if (_visibility == Visibility::Collapsed)
    {
        _bounds = Rect{};
        return;
    }

    const Rect available = deflate(slot, _layout.margin);

    glm::vec2 size = _measured;

    //! Stretch yields to an explicit Length: a 120px button centred in a wide
    //! row stays 120px wide, whatever its alignment says.
    if (_layout.hAlign == Alignment::Stretch && !_fixedWidth)
        size.x = available.width();
    if (_layout.vAlign == Alignment::Stretch && !_fixedHeight)
        size.y = available.height();

    size = glm::max(size, _layout.minSize());
    size = glm::min(size, _layout.maxSize());

    const glm::vec2 origin{
        available.min.x + alignOffset(_layout.hAlign, available.width(), size.x),
        available.min.y + alignOffset(_layout.vAlign, available.height(), size.y)};

    _bounds = Rect::fromSize(origin, size);

    arrangeContent(contentRect());
}

glm::vec2 Widget::measureContent(const Constraints& available)
{
    //! A plain Widget is a group: as big as the largest thing in it. Overlaid
    //! rather than stacked, which is what makes it the right base for a panel.
    glm::vec2 content{0.0f};

    for (const std::unique_ptr<Widget>& child : _children)
        content = glm::max(content, child->measure(available));

    return content;
}

void Widget::arrangeContent(const Rect& content)
{
    for (const std::unique_ptr<Widget>& child : _children)
        child->arrange(content);
}

// -----------------------------------------------------------------------------
// Visibility
// -----------------------------------------------------------------------------

void Widget::setVisibility(Visibility visibility)
{
    if (_visibility == visibility)
        return;

    const bool reflows = _visibility == Visibility::Collapsed ||
                         visibility == Visibility::Collapsed;

    _visibility = visibility;

    if (visibility != Visibility::Visible && _root)
        _root->_forget(*this);

    if (reflows)
        invalidateLayout();
    else
        invalidatePaint();
}

void Widget::setEnabled(bool enabled)
{
    if (_enabled == enabled)
        return;

    _enabled = enabled;

    //! A control disabled while focused or hovered must not keep either, or it
    //! would still look live and still eat the next keystroke.
    if (!_enabled && _root)
        _root->_forget(*this);

    invalidatePaint();
}

bool Widget::effectivelyEnabled() const noexcept
{
    for (const Widget* node = this; node != nullptr; node = node->_parent)
    {
        if (!node->_enabled)
            return false;
    }

    return true;
}

bool Widget::effectivelyVisible() const noexcept
{
    for (const Widget* node = this; node; node = node->_parent)
        if (node->_visibility != Visibility::Visible)
            return false;
    return true;
}

// -----------------------------------------------------------------------------
// Painting
// -----------------------------------------------------------------------------

void Widget::paintTree(DrawList& out)
{
    if (_visibility != Visibility::Visible)
        return;

    //! Nothing in this subtree can reach a pixel, so neither the widget nor
    //! any descendant is worth visiting. This is what keeps a long scrolled
    //! list cheap.
    const bool visible = !intersect(_bounds, out.clip()).empty();
    if (visible)
        paint(out);
    else if (clipsChildren())
        return;

    if (_children.empty())
        return;

    if (clipsChildren())
    {
        const ClipScope clipped(out, contentRect());
        paintChildren(out);
        return;
    }

    paintChildren(out);
}

void Widget::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    out.drawRect(_bounds, withAlpha(style.surface.normal, alpha),
                 withAlpha(style.border.normal, alpha), style.borderWidth,
                 Corners::all(style.rounding));
}

void Widget::paintChildren(DrawList& out)
{
    for (const std::unique_ptr<Widget>& child : _children)
        child->paintTree(out);
}

// -----------------------------------------------------------------------------
// Input
// -----------------------------------------------------------------------------

bool Widget::hitTest(glm::vec2 point) const
{
    return _hitTestVisible && _bounds.contains(point);
}

bool Widget::onPointerDown(const PointerEvent&) { return false; }
bool Widget::onPointerUp(const PointerEvent&) { return false; }
bool Widget::onPointerMove(const PointerEvent&) { return false; }
bool Widget::onWheel(const WheelEvent&) { return false; }
bool Widget::onKeyDown(const KeyEvent&) { return false; }
bool Widget::onKeyUp(const KeyEvent&) { return false; }
bool Widget::onTextInput(const TextEvent&) { return false; }

void Widget::onPointerEnter() {}
void Widget::onPointerLeave() {}
void Widget::onPointerCancel() {}
void Widget::onFocusIn(FocusReason) {}
void Widget::onFocusOut() {}

void Widget::onChildAdded(Widget&, usize) {}
void Widget::onChildRemoved(usize) {}
void Widget::onAttach() {}
void Widget::onDetach() {}

bool Widget::onTick(f32) { return false; }

void Widget::setAnimating(bool animating)
{
    if (_animating == animating)
        return;

    _animating = animating;

    if (_root)
        _root->_setAnimating(*this, animating);
}

// -----------------------------------------------------------------------------
// Focus and pointer state
// -----------------------------------------------------------------------------

bool Widget::focusable() const noexcept
{
    return _focusable && effectivelyVisible() && effectivelyEnabled();
}

bool Widget::hasFocus() const noexcept
{
    return _root != nullptr && _root->focused() == this;
}

void Widget::requestFocus(FocusReason reason)
{
    if (_root)
        _root->setFocus(this, reason);
}

bool Widget::isHovered() const noexcept
{
    return _root != nullptr && _root->isHovered(this);
}

void Widget::capturePointer()
{
    if (_root)
        _root->capturePointer(this);
}

void Widget::releasePointer()
{
    if (_root && _root->pointerCapture() == this)
        _root->capturePointer(nullptr);
}

bool Widget::hasPointerCapture() const noexcept
{
    return _root != nullptr && _root->pointerCapture() == this;
}

// -----------------------------------------------------------------------------
// Theming
// -----------------------------------------------------------------------------

const Theme& Widget::theme() const noexcept
{
    return _root ? std::as_const(*_root).theme() : detachedTheme();
}

WidgetStyle Widget::resolvedStyle() const
{
    const WidgetStyle& base = theme()[_part];
    return _style.empty() ? base : _style.over(base);
}

ITextShaper* Widget::shaper() const noexcept
{
    return _root ? &_root->shaper() : nullptr;
}

OverlayLayer* Widget::overlay() const noexcept
{
    return _root ? &_root->overlay() : nullptr;
}

// -----------------------------------------------------------------------------
// Accessibility
// -----------------------------------------------------------------------------

void Widget::accessibility(AccessibilityInfo& out) const
{
    out.role = _children.empty() ? Role::None : Role::Group;
    out.name = !_accessibleName.empty() ? _accessibleName : _name;
    out.description = _accessibleDescription;
    out.enabled = effectivelyEnabled();
    out.focusable = focusable();
    out.focused = hasFocus();
}

} // namespace aura3d::ui
