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

/// @p event, from surface pixels into the space @p node's bounds() are in.
[[nodiscard]] PointerEvent inWidget(PointerEvent event, const Widget &node) noexcept
{
    const glm::vec2 offset = node.visualOffset();
    event.position -= offset;
    event.pressPosition -= offset;
    event.local = event.position - node.bounds().min;
    return event;
}

[[nodiscard]] Widget *pick(Widget &node, glm::vec2 point)
{
    if (node.visibility() != Visibility::Visible)
        return nullptr;

    //! Into the space the node's bounds are in: a translation moves the picture, and the target with it.
    point -= node.translation();

    /*
     * A clipping widget hides its children outside its content rectangle, so
     * the point cannot belong to one of them -- but the widget itself (its
     * frame, its scrollbar) may still own it.
     */
    if (node.clipsChildren() && !node.contentRect().contains(point))
        return node.hitTest(point) ? &node : nullptr;

    //! Reverse order: later children paint on top, so they are hit first.
    for (usize i = node.childCount(); i-- > 0;)
    {
        if (Widget *hit = pick(node.childAt(i), point))
            return hit;
    }

    return node.hitTest(point) ? &node : nullptr;
}

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

    _layoutDirty = true;
    _paintDirty = true;
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

    _layoutDirty = true;
    _paintDirty = true;
}

void UIRoot::resize(glm::vec2 logicalSize)
{
    logicalSize = glm::max(logicalSize, glm::vec2{0.0f});

    if (logicalSize == _size)
        return;

    _size = logicalSize;
    _layoutDirty = true;
    _paintDirty = true;
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
    _layoutDirty = true;
    _paintDirty = true;
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

    const bool layoutChanged = _layoutDirty;
    if (layoutChanged)
        _layout();
    _overlay->syncFocus();
    if ((layoutChanged || _hoverDirty) && _pointerInside && !_capture)
        _updateHover(_pointer);
    _hoverDirty = false;
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
    _hoverDirty = true;
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

    _layoutDirty = false;
    _paintDirty = true;

    //! After the flags: a hook that invalidates layout asks for the next pass, and is not overwritten by this one.
    _overlay->runEnterTransitions();
}

bool UIRoot::paint(DrawList &out)
{
    if (_layoutDirty)
        _layout();

    if (!_paintDirty)
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

    _paintDirty = false;
    return true;
}

// -----------------------------------------------------------------------------
// Dispatch
// -----------------------------------------------------------------------------

template <class Event, class Handler> bool UIRoot::_bubble(Widget *target, Event &event, Handler &&handler)
{
    if (!target)
        return false;

    /*
     * A disabled subtree is inert *and* opaque: the event stops here rather
     * than bubbling to an enabled ancestor, so a click on a greyed-out button
     * does not fall through to the panel behind it.
     */
    if (target->root() != this || !target->effectivelyVisible())
        return false;
    if (!target->effectivelyEnabled())
        return true;

    //! Keyboard events have no position to re-base; only pointer ones do. Each node hears them in its own space,
    //! where its bounds() are: the surface position less every translation above and on it.
    constexpr bool kPositioned = requires { event.local = event.position; };
    glm::vec2 surface{0.0f};
    glm::vec2 surfacePress{0.0f};
    glm::vec2 offset{0.0f};
    if constexpr (kPositioned)
    {
        surface = event.position;
        if constexpr (requires { event.pressPosition; })
            surfacePress = event.pressPosition;
        offset = target->visualOffset();
    }

    for (Widget *node = target; node != nullptr;)
    {
        const WidgetRef current(node);
        const WidgetRef parent(node->parent());
        const glm::vec2 own = node->translation();
        if constexpr (kPositioned)
        {
            event.position = surface - offset;
            if constexpr (requires { event.pressPosition; })
                event.pressPosition = surfacePress - offset;
            event.local = event.position - node->bounds().min;
        }

        if (handler(*node, event))
            return true;
        if (!current.get() || node->root() != this || !node->effectivelyVisible())
            return true;
        offset -= own;
        node = parent.get();
    }

    return false;
}

Widget *UIRoot::widgetAt(glm::vec2 position) const
{
    if (!surface().contains(position))
        return nullptr;
    //! Overlays are drawn last, so they are hit first.
    if (Widget *hit = _overlay->widgetAt(position))
        return hit;

    /*
     * A modal overlay swallows everything underneath it. Returning the layer
     * itself rather than nullptr is what makes the click land *somewhere*:
     * it is consumed, so it neither reaches the content nor falls through to
     * the application as a click on the scene.
     */
    if (_overlay->hasModal())
        return _overlay.get();

    const bool inContent = _contentBounds.contains(position);
    if (_decoration && !inContent)
        return pick(*_decoration, position);

    return _content && inContent ? pick(*_content, position) : nullptr;
}

// -----------------------------------------------------------------------------
// Pointer
// -----------------------------------------------------------------------------

void UIRoot::_updateHover(glm::vec2 position)
{
    _hoverScratch.clear();

    for (Widget *node = widgetAt(position); node != nullptr; node = node->parent())
        _hoverScratch.push_back(node);

    //! Built deepest-first above; stored root-first so a diff against the
    //! previous chain compares common ancestors at matching indices.
    std::ranges::reverse(_hoverScratch);

    if (_hoverScratch == _hoverChain)
        return;

    usize shared = 0;
    while (shared < _hoverChain.size() && shared < _hoverScratch.size() && _hoverChain[shared] == _hoverScratch[shared])
        ++shared;

    InlineScratch<WidgetRef> leavingStorage;
    auto &leaving = leavingStorage.values;
    InlineScratch<WidgetRef> enteringStorage;
    auto &entering = enteringStorage.values;
    for (usize i = _hoverChain.size(); i-- > shared;)
        leaving.emplace_back(_hoverChain[i]);
    for (usize i = shared; i < _hoverScratch.size(); ++i)
        entering.emplace_back(_hoverScratch[i]);
    _hoverChain = _hoverScratch;
    for (const auto &reference : leaving)
        if (Widget *node = reference.get(); node && node->root() == this)
            node->onPointerLeave();
    for (const auto &reference : entering)
        if (Widget *node = reference.get(); node && node->root() == this && isHovered(node))
            node->onPointerEnter();

    _paintDirty = true;
}

void UIRoot::_dropHover()
{
    InlineScratch<WidgetRef> previousStorage;
    auto &previous = previousStorage.values;
    for (Widget *node : _hoverChain)
        previous.emplace_back(node);
    if (!_hoverChain.empty())
        _paintDirty = true;
    _hoverChain.clear();
    for (usize i = previous.size(); i-- > 0;)
        if (Widget *node = previous[i].get(); node && node->root() == this)
            node->onPointerLeave();
}

Widget *UIRoot::hovered() const noexcept
{
    return _hoverChain.empty() ? nullptr : _hoverChain.back();
}

wma::SystemCursor UIRoot::cursor() const noexcept
{
    for (const Widget *node = _capture != nullptr ? _capture : hovered(); node != nullptr; node = node->parent())
    {
        const wma::SystemCursor shape = node->cursor();
        if (shape != wma::SystemCursor::Default)
            return node->effectivelyEnabled() ? shape : wma::SystemCursor::Default;
    }
    return wma::SystemCursor::Default;
}

bool UIRoot::isHovered(const Widget *widget) const noexcept
{
    return widget != nullptr && std::ranges::find(_hoverChain, widget) != _hoverChain.end();
}

bool UIRoot::_offerIntercept(const PointerEvent &event)
{
    //! Deepest first, so the scroll view nearest the finger gets the first claim.
    for (usize i = _pressChain.size(); i-- > 0;)
    {
        //! A handler further down may end the press, and that empties the chain under this loop.
        if (i >= _pressChain.size())
            continue;

        Widget *node = _pressChain[i].get();
        if (node == nullptr || node == _capture || node->root() != this || !node->effectivelyEnabled() ||
            !node->effectivelyVisible())
            continue;

        if (node->onPointerIntercept(inWidget(event, *node)))
        {
            _pressChain.clear();
            return true;
        }
    }
    return false;
}

void UIRoot::_endPress() noexcept
{
    _pressChain.clear();
    _pressKind = PointerKind::Mouse;
}

bool UIRoot::pointerMoved(glm::vec2 position)
{
    if (_layoutDirty)
        _layout();
    const glm::vec2 delta = position - _pointer;
    _pointer = position;
    _pointerInside = true;

    PointerEvent event{.position = position, .delta = delta, .kind = _pressKind, .pressPosition = _pressPosition};
    if (!_pressChain.empty())
        _offerIntercept(event);

    if (_capture)
    {
        _capture->onPointerMove(inWidget(event, *_capture));
        return true;
    }

    _updateHover(position);

    const bool handled = _bubble(hovered(), event,
                                 [](Widget &node, PointerEvent &e)
                                 {
                                     return node.onPointerMove(e);
                                 });

    return handled || !_hoverChain.empty();
}

bool UIRoot::pointerDown(glm::vec2 position, PointerButton button, Modifiers mods, PointerKind kind)
{
    if (_layoutDirty)
        _layout();
    _pointer = position;
    _pointerInside = true;
    _endPress();

    _updateHover(position);

    const bool nearby = glm::length(position - _lastPressPosition) <= _doubleClickSlop;
    const bool soon = _doubleClickSeconds > 0.0f && _lastPressTime >= 0.0f &&
                      _time - _lastPressTime <= _doubleClickSeconds && button == _lastPressButton &&
                      _lastPressTarget.get() == hovered();

    _clickCount = (nearby && soon) ? _clickCount + 1 : 1;
    _lastPressTime = _time;
    _lastPressPosition = position;
    _lastPressButton = button;
    _lastPressTarget = WidgetRef(hovered());

    //! Before dispatch, so the press that dismisses a menu is not also
    //! delivered to whatever the menu was covering.
    if (_overlay->dismissOutside(position))
        return true;

    Widget *target = _capture ? _capture : hovered();

    PointerEvent event{.position = position,
                       .button = button,
                       .mods = mods,
                       .clickCount = _clickCount,
                       .kind = kind,
                       .pressPosition = position};

    //! Only the primary button starts a gesture. The chain is recorded before dispatch, because a widget
    //! that takes the press captures the pointer and the ancestors still need to hear about the moves.
    if (button == PointerButton::Left)
    {
        _pressPosition = position;
        _pressKind = kind;
        for (Widget *node : _hoverChain)
            if (node->interceptsPointer(kind))
                _pressChain.emplace_back(node);

        //! A widget that takes the press itself (a coasting scroll view caught by a finger) keeps it from
        //! the one under the finger, so that one is not clicked when the finger lifts.
        if (!_pressChain.empty() && _offerIntercept(event))
            return true;
    }

    const WidgetRef typing(_focused != nullptr && _focused->wantsTextInput() ? _focused : nullptr);
    const bool handled = _bubble(target, event,
                                 [](Widget &node, PointerEvent &e)
                                 {
                                     return node.onPointerDown(e);
                                 });

    //! A press elsewhere ends typing even when what it landed on takes the press (a scroll view always does), or the
    //! on-screen keyboard of a phone would stay up over the board.
    if (typing.get() != nullptr && typing.get() == _focused)
    {
        const auto inside = [&typing](const Widget *node)
        {
            for (; node != nullptr; node = node->parent())
                if (node == typing.get())
                    return true;
            return false;
        };
        if (!inside(target))
            clearFocus();
    }

    /*
     * A press that landed on the UI but that no widget wanted still counts as
     * consumed: it moves focus out of whatever had it, and it must not also be
     * a click in the world behind the panel.
     */
    if (!handled && target != nullptr)
        clearFocus();

    return handled || target != nullptr;
}

bool UIRoot::pointerUp(glm::vec2 position, PointerButton button, Modifiers mods, PointerKind kind)
{
    if (_layoutDirty)
        _layout();
    _pointer = position;
    if (!_capture)
        _updateHover(position);

    Widget *target = _capture ? _capture : hovered();

    PointerEvent event{
        .position = position, .button = button, .mods = mods, .kind = kind, .pressPosition = _pressPosition};
    if (button == PointerButton::Left)
        _endPress();

    const bool handled = _bubble(target, event,
                                 [](Widget &node, PointerEvent &e)
                                 {
                                     return node.onPointerUp(e);
                                 });

    if (!_capture)
        _updateHover(position);

    return handled || target != nullptr;
}

bool UIRoot::wheel(glm::vec2 delta, glm::vec2 position, Modifiers mods)
{
    if (_layoutDirty)
        _layout();
    _updateHover(position);

    WheelEvent event{.position = position, .delta = delta, .mods = mods};

    return _bubble(hovered(), event,
                   [](Widget &node, WheelEvent &e)
                   {
                       return node.onWheel(e);
                   });
}

void UIRoot::pointerLeft()
{
    _pointerInside = false;

    //! A drag keeps its capture: the pointer leaving the window mid-drag is
    //! normal, and the widget still wants the moves that follow.
    if (!_capture)
        _dropHover();
}

void UIRoot::capturePointer(Widget *widget)
{
    if (widget && (widget->root() != this || !widget->effectivelyEnabled() || !widget->effectivelyVisible()))
        return;
    if (_capture == widget)
        return;

    Widget *previous = std::exchange(_capture, widget);
    if (previous)
        previous->onPointerCancel();

    //! Releasing re-evaluates hover from where the pointer actually is, which
    //! is rarely still on the widget that was being dragged.
    if (!_capture && _pointerInside)
        _updateHover(_pointer);
    else if (!_capture)
        _dropHover();
}

void UIRoot::claimPointer(Widget *widget)
{
    capturePointer(widget);
    if (_capture == widget)
        _pressChain.clear();
}

void UIRoot::cancelInput()
{
    _pointerInside = false;
    _endPress();
    capturePointer(nullptr);
    clearFocus();
    _dropHover();
    _closeTooltip();
    _overlay->closeLightDismissible();
    _lastPressTime = -1.0f;
}

bool UIRoot::capturesPointer() const noexcept
{
    return _capture != nullptr || !_hoverChain.empty();
}

// -----------------------------------------------------------------------------
// Keyboard
// -----------------------------------------------------------------------------

bool UIRoot::_runShortcuts(const KeyEvent &event, bool beforeWidget)
{
    //! Modified combinations win over the focused widget; bare keys lose to
    //! it, so Delete still edits text but Ctrl+S always saves.
    const bool modified = event.mods.ctrl || event.mods.alt || event.mods.super;
    if (modified != beforeWidget)
        return false;

    for (const ShortcutEntry &entry : _shortcuts)
    {
        if (!entry.shortcut.matches(event))
            continue;

        const auto action = entry.action;
        if (action)
            action();

        return true;
    }

    return false;
}

bool UIRoot::keyDown(wma::Key key, Modifiers mods, bool repeat)
{
    _overlay->syncFocus();
    const KeyEvent event{.key = key, .mods = mods, .repeat = repeat};

    /*
     * Escape belongs to the topmost overlay before anything else: a drop-down
     * over a dialog closes the drop-down, and only a second Escape reaches the
     * dialog. A focused widget that wants Escape for itself (a text field
     * reverting an edit) still gets it whenever nothing is floating.
     */
    if (key == wma::KEY_ESCAPE && _overlay->closeTopmost())
        return true;

    if (_runShortcuts(event, true))
        return true;

    KeyEvent bubbled = event;
    if (_bubble(_focused, bubbled,
                [](Widget &node, KeyEvent &e)
                {
                    return node.onKeyDown(e);
                }))
        return true;

    if (_runShortcuts(event, false))
        return true;

    if (key == wma::KEY_TAB)
        return mods.shift ? focusPrevious() : focusNext();

    return false;
}

bool UIRoot::keyUp(wma::Key key, Modifiers mods)
{
    _overlay->syncFocus();
    KeyEvent event{.key = key, .mods = mods};
    return _bubble(_focused, event,
                   [](Widget &node, KeyEvent &e)
                   {
                       return node.onKeyUp(e);
                   });
}

bool UIRoot::textInput(std::string_view utf8)
{
    _overlay->syncFocus();
    if (utf8.empty())
        return false;

    TextEvent event{utf8};
    return _bubble(_focused, event,
                   [](Widget &node, TextEvent &e)
                   {
                       return node.onTextInput(e);
                   });
}

// -----------------------------------------------------------------------------
// Focus
// -----------------------------------------------------------------------------

void UIRoot::setFocus(Widget *widget, FocusReason reason)
{
    if (widget != nullptr && (widget->root() != this || !widget->focusable() || !_overlay->allowsFocus(widget)))
        return;

    if (_focused == widget)
        return;

    Widget *previous = _focused;
    _focused = widget;

    if (previous)
        previous->onFocusOut();

    if (_focused == widget && _focused)
        _focused->onFocusIn(reason);

    _paintDirty = true;
    focusChanged.emit(_focused);
}

void UIRoot::clearFocus()
{
    setFocus(nullptr);
}

void UIRoot::_collectFocusable(Widget &node, std::vector<Widget *> &out) const
{
    if (node.visibility() != Visibility::Visible || !node.effectivelyEnabled())
        return;

    if (node.focusable())
        out.push_back(&node);

    for (usize i = 0; i < node.childCount(); ++i)
        _collectFocusable(node.childAt(i), out);
}

bool UIRoot::_moveFocus(int direction)
{
    _focusScratch.clear();

    /*
     * Tab is trapped inside the topmost overlay while one is open. Without
     * this, tabbing out of a dialog lands on the controls it is covering,
     * which is both wrong and unreachable with the pointer.
     */
    if (Widget *floating = _overlay->focusScope())
    {
        _collectFocusable(*floating, _focusScratch);
    }
    else
    {
        if (_content)
            _collectFocusable(*_content, _focusScratch);
        if (_decoration)
            _collectFocusable(*_decoration, _focusScratch);
    }

    if (_focusScratch.empty())
        return false;

    const auto current = std::ranges::find(_focusScratch, _focused);

    usize next = 0;
    if (current != _focusScratch.end())
    {
        const auto index = static_cast<isize>(current - _focusScratch.begin());
        const auto count = static_cast<isize>(_focusScratch.size());
        next = static_cast<usize>((index + direction + count) % count);
    }
    else if (direction < 0)
    {
        next = _focusScratch.size() - 1;
    }

    setFocus(_focusScratch[next], FocusReason::Keyboard);
    return true;
}

bool UIRoot::focusNext()
{
    return _moveFocus(1);
}
bool UIRoot::focusPrevious()
{
    return _moveFocus(-1);
}

bool UIRoot::capturesKeyboard() const noexcept
{
    return _focused != nullptr;
}

bool UIRoot::capturesTextInput() const noexcept
{
    return _focused != nullptr && _focused->wantsTextInput();
}

// -----------------------------------------------------------------------------
// Shortcuts
// -----------------------------------------------------------------------------

u64 UIRoot::addShortcut(Shortcut shortcut, std::function<void()> action)
{
    const u64 id = _nextShortcutId++;
    _shortcuts.push_back(ShortcutEntry{id, shortcut, std::move(action)});
    return id;
}

void UIRoot::removeShortcut(u64 id)
{
    std::erase_if(_shortcuts,
                  [id](const ShortcutEntry &entry)
                  {
                      return entry.id == id;
                  });
}

// -----------------------------------------------------------------------------
// Bookkeeping
// -----------------------------------------------------------------------------

bool UIRoot::_isAncestorOf(const Widget &ancestor, const Widget *node) noexcept
{
    for (; node != nullptr; node = node->parent())
    {
        if (node == &ancestor)
            return true;
    }

    return false;
}

void UIRoot::_forget(Widget &widget)
{
    // Finish bookkeeping before callbacks can remove another part of the tree.
    const bool losesFocus = _isAncestorOf(widget, _focused);
    const WidgetRef focus(losesFocus ? std::exchange(_focused, nullptr) : nullptr);
    const bool losesCapture = _isAncestorOf(widget, _capture);
    const WidgetRef capture(losesCapture ? std::exchange(_capture, nullptr) : nullptr);
    InlineScratch<WidgetRef> leavingStorage;
    auto &leaving = leavingStorage.values;
    for (Widget *node : _hoverChain)
        if (_isAncestorOf(widget, node))
            leaving.emplace_back(node);
    std::erase_if(_hoverChain,
                  [&widget](Widget *node)
                  {
                      return _isAncestorOf(widget, node);
                  });

    if (_tooltipTarget && _isAncestorOf(widget, _tooltipTarget))
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

    _paintDirty = true;
    _layoutDirty = true;
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
    Widget *owner = _pointerInside ? _tooltipOwner(hovered()) : nullptr;

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
        .dismissOnOutsideClick = false,
        .dismissOnEscape = false,
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
            child.info.modal = true;
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
