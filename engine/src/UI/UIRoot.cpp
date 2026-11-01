#include "aura/UI/UIRoot.h"

#include "aura/Utils/InlineScratch.h"

#include <algorithm>
#include <iterator>

#include "aura/UI/Text/TextEngine.h"
#include "aura/UI/Widgets/Basic.h"
#include "aura/UI/Widgets/Layouts.h"

namespace aura3d::ui
{

namespace
{

void collectAccessibility(const Widget &node, AccessibilityNode &out)
{
    if (node.visibility() == Visibility::Collapsed)
        return;

    node.accessibility(out.info);
    out.bounds = node.visualBounds();

    for (usize i = 0; i < node.childCount(); ++i)
    {
        AccessibilityNode child{};
        collectAccessibility(node.childAt(i), child);

        //! A purely visual node contributes its children but not itself, so a
        //! screen reader walks a tree of meaning rather than one of rectangles.
        if (child.info.role == Role::None && child.info.name.empty())
            out.children.insert(out.children.end(), std::make_move_iterator(child.children.begin()),
                                std::make_move_iterator(child.children.end()));
        else
            out.children.push_back(std::move(child));
    }
}

} // namespace

UIRoot::UIRoot(ITextShaper &shaper, Theme theme)
    : _shaper(&shaper), _theme(std::move(theme)), _overlay(std::make_unique<OverlayLayer>())
{
    _overlay->_setRoot(this);
}

UIRoot::~UIRoot()
{
    /*
     * The trees call back into _forget() as they go, which reads the members
     * below. The tooltip is dropped by hand first because _forget() would
     * otherwise try to close it through a layer that is already being torn
     * down. Content and decoration detach before the overlay they may reference.
     */
    _tooltipTarget = nullptr;
    _tooltipOverlay = OverlayLayer::kNone;

    while (_content || _decoration)
    {
        _adopt(nullptr);
        setDecoration(nullptr);
    }
    _overlay.reset();
}

// -----------------------------------------------------------------------------
// Content and surface
// -----------------------------------------------------------------------------

void UIRoot::_adopt(std::unique_ptr<Widget> content)
{
    // Keep the outgoing tree alive while blur/cancel/detach callbacks run.
    // A callback can install another tree; detach that one as well before
    // committing this caller's replacement, so setContent returns its own tree.
    while (_content)
    {
        auto previous = std::move(_content);
        previous->_setRoot(nullptr);
    }

    _content = std::move(content);

    if (_content)
    {
        _content->_parent = nullptr;
        _content->_setRoot(this);
    }

    INK_FLAG_SET(_flags, UIRootFlags::LayoutDirty | UIRootFlags::PaintDirty);
}

Widget &UIRoot::setContent(std::unique_ptr<Widget> content)
{
    Widget &reference = *content;
    _adopt(std::move(content));
    return reference;
}

void UIRoot::setDecoration(std::unique_ptr<Widget> decoration)
{
    // Detach callbacks may install another decoration; finish those replacements
    // before adopting the caller's widget, just as setContent does.
    while (_decoration)
    {
        auto previous = std::move(_decoration);
        previous->_setRoot(nullptr);
    }

    _decoration = std::move(decoration);
    if (_decoration)
    {
        _decoration->_parent = nullptr;
        _decoration->_setRoot(this);
    }
    ++_decorationGeneration;

    INK_FLAG_SET(_flags, UIRootFlags::LayoutDirty | UIRootFlags::PaintDirty);
}

void UIRoot::resize(glm::vec2 logicalSize)
{
    logicalSize = glm::max(logicalSize, glm::vec2{0.0f});

    if (logicalSize == _size)
        return;

    _size = logicalSize;
    INK_FLAG_SET(_flags, UIRootFlags::LayoutDirty | UIRootFlags::PaintDirty);
}

void UIRoot::setScale(f32 scale)
{
    scale = std::clamp(scale, 0.25f, 8.0f);
    if (scale == _scale)
        return;

    _scale = scale;
    _shaper->setScale(scale);

    //! Glyph metrics come back at a new resolution, so every measured label is
    //! stale by a fraction of a pixel.
    INK_FLAG_SET(_flags, UIRootFlags::LayoutDirty | UIRootFlags::PaintDirty);
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------

void UIRoot::update(f32 deltaSeconds)
{
    _time += deltaSeconds;

    //! First, so an exit that finishes here is collected in this same update.
    _tickTweens(deltaSeconds);

    //! Between frames, where destroying a widget cannot pull the ground out
    //! from under a dispatch that is still running.
    _overlay->collectClosed();

    _updateTooltip(deltaSeconds);

    if (!_animating.empty())
    {
        //! Ticking off a snapshot: a widget may stop animating (or start
        //! another one) from inside its own onTick.
        _tickScratch.clear();
        for (Widget *widget : _animating)
            _tickScratch.emplace_back(widget);

        for (const WidgetRef &reference : _tickScratch)
        {
            Widget *widget = reference.get();
            if (!widget || widget->root() != this || !widget->animating() || !widget->effectivelyVisible())
                continue;
            const bool running = widget->onTick(deltaSeconds);
            if (reference.get() && widget->root() == this && !running)
                widget->setAnimating(false);
        }
    }

    const bool layoutChanged = INK_FLAG_CHECK(_flags, UIRootFlags::LayoutDirty);
    if (layoutChanged)
        _layout();
    _overlay->syncFocus();
    if ((layoutChanged || INK_FLAG_CHECK(_flags, UIRootFlags::HoverDirty)) &&
        INK_FLAG_CHECK(_flags, UIRootFlags::PointerInside) && !_capture)
        _updateHover(_pointer);
    INK_FLAG_CLEAR(_flags, UIRootFlags::HoverDirty);
}

UIRoot::AnimationId UIRoot::animate(Widget *target, Tween tween)
{
    if (target != nullptr && target->root() != this)
        return kNoAnimation;

    const AnimationId id = _nextAnimationId++;
    _tweens.push_back(std::make_unique<TweenEntry>(
        TweenEntry{.id = id, .target = WidgetRef(target), .targeted = target != nullptr, .tween = std::move(tween)}));

    TweenEntry &entry = *_tweens.back();
    if (entry.tween.step)
        entry.tween.step(0.0f);
    return id;
}

void UIRoot::cancelAnimation(AnimationId id) noexcept
{
    if (id == kNoAnimation)
        return;
    for (const std::unique_ptr<TweenEntry> &entry : _tweens)
        if (entry->id == id)
            entry->id = kNoAnimation;
}

bool UIRoot::animating() const noexcept
{
    return !_animating.empty() || std::ranges::any_of(_tweens,
                                                      [](const std::unique_ptr<TweenEntry> &entry)
                                                      {
                                                          return entry->id != kNoAnimation;
                                                      });
}

void UIRoot::setDoubleClickSeconds(f32 seconds) noexcept
{
    _doubleClickSeconds = std::max(0.0f, seconds);
}

f32 UIRoot::doubleClickSeconds() const noexcept
{
    return _doubleClickSeconds;
}

void UIRoot::setDoubleClickSlop(f32 pixels) noexcept
{
    _doubleClickSlop = std::max(0.0f, pixels);
}

f32 UIRoot::doubleClickSlop() const noexcept
{
    return _doubleClickSlop;
}

void UIRoot::setCaretBlinkSeconds(f32 seconds) noexcept
{
    //! A zero interval would be a caret that is never on.
    _caretBlinkSeconds = std::max(0.05f, seconds);
}

f32 UIRoot::caretBlinkSeconds() const noexcept
{
    return _caretBlinkSeconds;
}

void UIRoot::setClipboard(IClipboard *clipboard) noexcept
{
    _clipboard = clipboard;
}

IClipboard &UIRoot::clipboard() noexcept
{
    return _clipboard != nullptr ? *_clipboard : _memoryClipboard;
}

void UIRoot::_requestHover() noexcept
{
    INK_FLAG_SET(_flags, UIRootFlags::HoverDirty);
}

void UIRoot::_tickTweens(f32 deltaSeconds)
{
    //! Only the tweens that were running when the frame began: one started from a step waits for the next update().
    for (usize i = 0, count = _tweens.size(); i < count; ++i)
    {
        TweenEntry &entry = *_tweens[i];
        if (entry.id == kNoAnimation)
            continue;

        if (entry.targeted)
        {
            const Widget *target = entry.target.get();
            if (target == nullptr || target->root() != this)
            {
                entry.id = kNoAnimation;
                continue;
            }
        }

        entry.elapsed += deltaSeconds;
        const f32 seconds = entry.tween.seconds;
        const f32 linear = seconds > 0.0f ? std::min(entry.elapsed / seconds, 1.0f) : 1.0f;
        const bool finished = linear >= 1.0f;
        if (entry.tween.step)
            entry.tween.step(finished || !entry.tween.curve ? linear : entry.tween.curve(linear));

        //! A step may cancel its own tween.
        if (!finished || entry.id == kNoAnimation)
            continue;
        entry.id = kNoAnimation;
        if (entry.tween.done)
            entry.tween.done();
    }

    std::erase_if(_tweens,
                  [](const std::unique_ptr<TweenEntry> &entry)
                  {
                      return entry->id == kNoAnimation;
                  });
}

void UIRoot::_layout()
{
    _contentBounds = surface();
    if (_decoration)
    {
        _decoration->measure(Constraints::tight(_size));
        _decoration->arrange(surface());

        //! Clamped so insets wider than a tiny surface leave an empty rectangle
        //! inside it rather than an inverted one.
        if (_decoration->visibility() != Visibility::Collapsed)
        {
            const Rect inner = _decoration->contentRect();
            _contentBounds.min = glm::clamp(inner.min, _contentBounds.min, _contentBounds.max);
            _contentBounds.max = glm::clamp(inner.max, _contentBounds.min, _contentBounds.max);
        }
    }

    if (_content)
    {
        _content->measure(Constraints::loose(_contentBounds.size()));
        _content->arrange(_contentBounds);
    }

    //! After the content, and against the same surface: an overlay is placed
    //! from anchors the content pass has just settled.
    _overlay->measure(Constraints::loose(_size));
    _overlay->arrange(surface());

    INK_FLAG_CLEAR(_flags, UIRootFlags::LayoutDirty);
    INK_FLAG_SET(_flags, UIRootFlags::PaintDirty);

    //! After the flags: a hook that invalidates layout asks for the next pass, and is not overwritten by this one.
    _overlay->runEnterTransitions();
}

bool UIRoot::paint(DrawList &out)
{
    if (INK_FLAG_CHECK(_flags, UIRootFlags::LayoutDirty))
        _layout();

    if (!INK_FLAG_CHECK(_flags, UIRootFlags::PaintDirty))
        return false;

    out.begin(surface());

    if (_content)
    {
        ClipScope clip(out, _contentBounds);
        _content->paintTree(out);
    }

    if (_decoration)
        _decoration->paintTree(out);

    //! Last, so it is on top of everything the content drew -- which is the
    //! entire reason the layer exists.
    _overlay->paintTree(out);

    INK_FLAG_CLEAR(_flags, UIRootFlags::PaintDirty);
    return true;
}

// -----------------------------------------------------------------------------
// Bookkeeping
// -----------------------------------------------------------------------------

void UIRoot::_forget(Widget &widget)
{
    // Finish bookkeeping before callbacks can remove another part of the tree.
    const bool losesFocus = widget.isAncestorOf(_focused);
    const WidgetRef focus(losesFocus ? std::exchange(_focused, nullptr) : nullptr);
    const bool losesCapture = widget.isAncestorOf(_capture);
    const WidgetRef capture(losesCapture ? std::exchange(_capture, nullptr) : nullptr);
    InlineScratch<WidgetRef> leavingStorage;
    auto &leaving = leavingStorage.values;
    for (Widget *node : _hoverChain)
        if (widget.isAncestorOf(node))
            leaving.emplace_back(node);
    std::erase_if(_hoverChain,
                  [&widget](Widget *node)
                  {
                      return widget.isAncestorOf(node);
                  });

    if (_tooltipTarget && widget.isAncestorOf(_tooltipTarget))
    {
        _tooltipTarget = nullptr;
        _closeTooltip();
    }
    if (_overlay)
        _overlay->forgetOwner(widget);

    if (Widget *node = focus.get())
        node->onFocusOut();
    if (losesFocus)
        focusChanged.emit(_focused);
    if (Widget *node = capture.get())
        node->onPointerCancel();
    for (usize i = leaving.size(); i-- > 0;)
        if (Widget *node = leaving[i].get())
            node->onPointerLeave();

    INK_FLAG_SET(_flags, UIRootFlags::LayoutDirty | UIRootFlags::PaintDirty);
}

void UIRoot::_setAnimating(Widget &widget, bool animating)
{
    if (animating)
    {
        if (std::ranges::find(_animating, &widget) == _animating.end())
            _animating.push_back(&widget);
    }
    else
    {
        std::erase(_animating, &widget);
    }
}

Widget *UIRoot::_tooltipOwner(Widget *node) noexcept
{
    for (; node != nullptr; node = node->parent())
    {
        if (!node->tooltip().empty())
            return node;
    }

    return nullptr;
}

void UIRoot::_closeTooltip()
{
    //! Null while the root is being destroyed, which is exactly when a widget
    //! carrying a tooltip is most likely to be unregistering itself.
    if (!_overlay || _tooltipOverlay == OverlayLayer::kNone)
        return;

    _overlay->close(_tooltipOverlay);
    _tooltipOverlay = OverlayLayer::kNone;
}

void UIRoot::_updateTooltip(f32 deltaSeconds)
{
    Widget *owner = INK_FLAG_CHECK(_flags, UIRootFlags::PointerInside) ? _tooltipOwner(hovered()) : nullptr;

    if (owner != _tooltipTarget)
    {
        _tooltipTarget = owner;
        _tooltipDwell = 0.0f;
        _closeTooltip();
    }

    if (!_tooltipTarget || _tooltipOverlay != OverlayLayer::kNone)
        return;

    //! The frame the pointer arrives on counts towards the dwell. Starting the
    //! clock a frame later would make the delay depend on the frame rate.
    _tooltipDwell += deltaSeconds;
    if (_tooltipDwell < _tooltipDelay)
        return;

    /*
     * Built from the theme rather than from a Tooltip widget: a tooltip is a
     * surface with a label on it, and every part of that is already something
     * the toolkit has. Anchored to the cursor, not the widget, because a
     * tooltip for a wide row should appear where the eye is.
     */
    const WidgetStyle &style = _theme[Part::Tooltip];

    auto &box = _overlay->open<Column>(OverlayDesc{
        .anchor = Rect{_pointer, _pointer},
        .placement = Placement::Cursor,
        .flags = OverlayFlags::None,
    });

    _tooltipOverlay = _overlay->lastId();

    box.setHitTestVisible(false);
    box.style().fill(style.surface.normal).rounded(style.rounding);
    box.layout().padding = Thickness::symmetric(style.padding * 1.5f, style.padding);

    if (style.borderWidth > 0.0f)
        box.style().outline(style.border.normal, style.borderWidth);

    auto &label = box.add<Label>(owner->tooltip());
    label.style().textColor(style.text);
}

AccessibilityNode UIRoot::accessibilityTree() const
{
    AccessibilityNode tree{};
    tree.info.role = Role::Window;
    tree.bounds = surface();

    if (_content)
    {
        AccessibilityNode child{};
        collectAccessibility(*_content, child);
        tree.children.push_back(std::move(child));
    }

    if (_decoration && _decoration->visibility() != Visibility::Collapsed)
    {
        AccessibilityNode child{};
        collectAccessibility(*_decoration, child);
        tree.children.push_back(std::move(child));
    }

    //! Last, as they are drawn: what floats is on top of everything else, and a modal one is all there is to reach.
    for (const OverlayLayer::OpenOverlay &overlay : _overlay->openOverlays())
    {
        AccessibilityNode child{};
        collectAccessibility(*overlay.widget, child);

        if (overlay.modal)
        {
            child.info.set(AccessibilityState::Modal, true);
            child.info.role = Role::Window;
        }

        //! The same fold as inside the tree: a root that means nothing hands its children up.
        if (!overlay.modal && child.info.role == Role::None && child.info.name.empty())
            tree.children.insert(tree.children.end(), std::make_move_iterator(child.children.begin()),
                                 std::make_move_iterator(child.children.end()));
        else
            tree.children.push_back(std::move(child));
    }

    return tree;
}

} // namespace aura3d::ui
