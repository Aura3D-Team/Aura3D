#include "aura/UI/UIRoot.h"

#include "aura/Utils/InlineScratch.h"

#include <algorithm>

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

} // namespace

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
        return _decoration->pick(position);

    return _content && inContent ? _content->pick(position) : nullptr;
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

    INK_FLAG_SET(_flags, UIRootFlags::PaintDirty);
}

void UIRoot::_dropHover()
{
    InlineScratch<WidgetRef> previousStorage;
    auto &previous = previousStorage.values;
    for (Widget *node : _hoverChain)
        previous.emplace_back(node);
    if (!_hoverChain.empty())
        INK_FLAG_SET(_flags, UIRootFlags::PaintDirty);
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
    if (INK_FLAG_CHECK(_flags, UIRootFlags::LayoutDirty))
        _layout();
    const glm::vec2 delta = position - _pointer;
    _pointer = position;
    INK_FLAG_SET(_flags, UIRootFlags::PointerInside);

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
    if (INK_FLAG_CHECK(_flags, UIRootFlags::LayoutDirty))
        _layout();
    _pointer = position;
    INK_FLAG_SET(_flags, UIRootFlags::PointerInside);
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
    if (INK_FLAG_CHECK(_flags, UIRootFlags::LayoutDirty))
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
    if (INK_FLAG_CHECK(_flags, UIRootFlags::LayoutDirty))
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
    INK_FLAG_CLEAR(_flags, UIRootFlags::PointerInside);

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
    if (!_capture && INK_FLAG_CHECK(_flags, UIRootFlags::PointerInside))
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
    INK_FLAG_CLEAR(_flags, UIRootFlags::PointerInside);
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

    INK_FLAG_SET(_flags, UIRootFlags::PaintDirty);
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

} // namespace aura3d::ui
