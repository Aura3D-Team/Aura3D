#include "aura/UI/Widgets/Controls.h"

#include <algorithm>
#include <cmath>

#include "aura/UI/Text/Utf8.h"
#include "aura/UI/UIRoot.h"

namespace aura3d::ui
{

namespace
{

//! Command key of the platform: Ctrl, and on Apple's the Command key too.
[[nodiscard]] bool commandKey(const Modifiers &mods) noexcept
{
#if defined(__APPLE__)
    return mods.ctrl || mods.super;
#else
    return mods.ctrl;
#endif
}

} // namespace

// =============================================================================
// TextField
// =============================================================================

TextField::TextField(std::string value) : text(std::move(value))
{
    _part = Part::TextField;

    text.changed().connect(
        [this](const std::string &updated)
        {
            //! An assignment from outside can land anywhere relative to the caret,
            //! so both ends are pulled back into the new string and onto a
            //! character boundary.
            _caret = utf8::clampToBoundary(updated, std::min(_caret, updated.size()));
            _selection = _caret;

            //! A step is a copy of what the text was, and an assignment from outside has made them all stale.
            if (!INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::Editing))
            {
                _undo.clear();
                _redo.clear();
                _lastEdit = EditKind::None;
            }
            invalidateLayout();
        });
}

void TextField::setPlaceholder(std::string placeholder)
{
    _placeholder = std::move(placeholder);
    _placeholderShaped.clear();
    invalidatePaint();
}

void TextField::setReadOnly(bool readOnly)
{
    if (this->readOnly() == readOnly)
        return;

    readOnly ? INK_FLAG_SET(_fieldFlags, TextFieldFlags::ReadOnly)
             : INK_FLAG_CLEAR(_fieldFlags, TextFieldFlags::ReadOnly);
    invalidatePaint();
}

TextStyle TextField::_style() const
{
    TextStyle style{};
    style.pixelSize = theme().metrics.fontSize;
    style.wrap = TextWrap::None;
    style.align = Align::Left;
    return style;
}

void TextField::_reshape()
{
    ITextShaper *shaper = this->shaper();
    if (!shaper)
        return;

    const TextStyle style = _style();

    if (_shapedSource == text.get() && _shapedFontSize == style.pixelSize && _shapedObscured == obscured() &&
        _shaped.scale == shaper->scale())
        return;

    //! One asterisk per byte, not per character: every caret and selection
    //! offset is a byte offset into the real text, and this keeps them valid
    //! in the shaped stand-in without a second mapping.
    if (INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::Obscured))
        shaper->shape(std::string(text.get().size(), '*'), style, kUnbounded, _shaped);
    else
        shaper->shape(text.get(), style, kUnbounded, _shaped);

    _shapedSource = text.get();
    _shapedFontSize = style.pixelSize;
    _shapedObscured = obscured();
}

void TextField::setObscured(bool obscured)
{
    if (this->obscured() == obscured)
        return;
    obscured ? INK_FLAG_SET(_fieldFlags, TextFieldFlags::Obscured)
             : INK_FLAG_CLEAR(_fieldFlags, TextFieldFlags::Obscured);
    if (obscured)
    {
        _undo.clear();
        _redo.clear();
        _lastEdit = EditKind::None;
    }
    invalidateLayout();
    invalidatePaint();
}

wma::SystemCursor TextField::cursor() const noexcept
{
    return wma::SystemCursor::Text;
}

Thickness TextField::contentInsets() const noexcept
{
    return textInsets();
}

glm::vec2 TextField::measureContent(const Constraints &)
{
    _reshape();

    const WidgetStyle style = resolvedStyle();
    const f32 row = rowHeight(style);

    //! A width, unlike a Label's, that does not follow the content: a field
    //! that grew as you typed would reflow the form under the cursor.
    return {theme().metrics.fontSize * 8.0f, std::max(row, _shaped.lineHeight)};
}

void TextField::_revealCaret(f32 viewportWidth)
{
    if (viewportWidth <= 0.0f)
        return;

    const f32 caretX = _shaped.caretPosition(_caret).x;

    //! One pixel of slack at each edge, so a caret sitting exactly on the
    //! boundary is drawn rather than clipped in half.
    if (caretX - _scrollX > viewportWidth - 1.0f)
        _scrollX = caretX - viewportWidth + 1.0f;
    else if (caretX - _scrollX < 1.0f)
        _scrollX = caretX - 1.0f;

    _scrollX = std::clamp(_scrollX, 0.0f, std::max(0.0f, _shaped.size.x - viewportWidth));
}

void TextField::paint(DrawList &out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = paintAlpha();
    const bool focused = hasFocus();

    paintSurface(out, bounds(), style, focused, alpha);

    const Rect content = contentRect();
    _revealCaret(content.width());

    const ClipScope clipped(out, content);

    const glm::vec2 origin{content.min.x - _scrollX,
                           content.min.y + alignOffset(Alignment::Center, content.height(), _shaped.size.y)};

    if (text.get().empty() && !_placeholder.empty())
    {
        if (ITextShaper *shaper = this->shaper();
            shaper && (_placeholderShaped.empty() || _placeholderShaped.scale != shaper->scale()))
            shaper->shape(_placeholder, _style(), kUnbounded, _placeholderShaped);

        out.drawText(_placeholderShaped, {content.min.x, origin.y}, withAlpha(style.text, alpha * 0.45f));
    }

    if (_hasSelection())
    {
        _selectionRects.clear();
        _shaped.selectionRects(_selectionBegin(), _selectionEnd(), _selectionRects);

        for (const Rect &rect : _selectionRects)
            out.fillRect(rect.translated(origin), withAlpha(style.accent, alpha * 0.35f));
    }

    out.drawText(_shaped, origin, withAlpha(style.text, alpha));

    //! Blink is a square wave on the tick clock, so the caret is solid while
    //! the field is being typed into -- _blink is reset on every edit.
    if (focused && !INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly) && _caretShown())
        out.fillRect(_shaped.caretRect(_caret, 1.0f).translated(origin), withAlpha(style.accent, alpha));

    paintFocusRing(out, style, bounds());
}

void TextField::setCaret(usize byte, bool extendSelection)
{
    _lastEdit = EditKind::None;
    _caret = utf8::clampToBoundary(text.get(), std::min(byte, text.get().size()));

    if (!extendSelection)
        _selection = _caret;

    _blink = 0.0f;
    invalidatePaint();
}

void TextField::selectAll()
{
    _lastEdit = EditKind::None;
    _selection = 0;
    _caret = text.get().size();
    invalidatePaint();
}

std::string TextField::selectedText() const
{
    return _hasSelection() ? text.get().substr(_selectionBegin(), _selectionEnd() - _selectionBegin()) : std::string{};
}

TextField::Snapshot TextField::_snapshot() const
{
    return {text.get(), _caret, _selection};
}

void TextField::_remember(EditKind kind)
{
    //! A passphrase must not outlive its edit in a stack of copies.
    if (INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::Obscured))
        return;

    //! A run's first step stands for all of it, as long as the caret has not been moved or a selection made between.
    const bool continuesRun =
        kind == _lastEdit && (kind == EditKind::Typing || kind == EditKind::Deleting) && !_hasSelection();
    _lastEdit = kind;
    if (continuesRun)
        return;

    _redo.clear();
    _undo.push_back(_snapshot());
    _trimHistory();
}

void TextField::_trimHistory() noexcept
{
    usize bytes = 0;
    for (const Snapshot &step : _undo)
        bytes += step.text.size();
    for (const Snapshot &step : _redo)
        bytes += step.text.size();

    //! The oldest go first; the newest step is always kept, whatever it weighs.
    while (_undo.size() > 1 && (_undo.size() > _maxUndoSteps || bytes > _maxHistoryBytes))
    {
        bytes -= _undo.front().text.size();
        _undo.pop_front();
    }
}

void TextField::_restore(Snapshot snapshot)
{
    _lastEdit = EditKind::None;
    _blink = 0.0f;

    {
        INK_FLAG_SET(_fieldFlags, TextFieldFlags::Editing);
        INK_DEFER
        {
            INK_FLAG_CLEAR(_fieldFlags, TextFieldFlags::Editing);
        };
        text.set(std::move(snapshot.text), false);
        text.changed().emit(text.get());
    }

    //! The observers above reset the selection to the caret; the step being returned to had its own.
    _caret = utf8::clampToBoundary(text.get(), std::min(snapshot.caret, text.get().size()));
    _selection = utf8::clampToBoundary(text.get(), std::min(snapshot.selection, text.get().size()));
    invalidatePaint();
}

void TextField::setHistoryLimit(usize steps, usize bytes)
{
    _maxUndoSteps = steps;
    _maxHistoryBytes = bytes;
    _trimHistory();
}

bool TextField::canUndo() const noexcept
{
    return !INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly) && !_undo.empty();
}

bool TextField::canRedo() const noexcept
{
    return !INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly) && !_redo.empty();
}

bool TextField::undo()
{
    if (!canUndo())
        return false;

    _redo.push_back(_snapshot());
    Snapshot previous = std::move(_undo.back());
    _undo.pop_back();
    _restore(std::move(previous));
    _trimHistory();
    return true;
}

bool TextField::redo()
{
    if (!canRedo())
        return false;

    _undo.push_back(_snapshot());
    Snapshot next = std::move(_redo.back());
    _redo.pop_back();
    _restore(std::move(next));
    _trimHistory();
    return true;
}

void TextField::_deleteSelection(EditKind kind)
{
    _deleteRange(_selectionBegin(), _selectionEnd(), kind);
}

void TextField::_deleteRange(usize begin, usize end, EditKind kind)
{
    if (begin >= end)
        return;

    //! Before anything changes: what is kept is the caret and selection as the user left them, not as this edit
    //! needs them.
    _remember(kind);

    std::string updated = text.get();
    updated.erase(begin, end - begin);

    _caret = begin;
    _selection = begin;

    //! Written without notifying so the caret is already correct when
    //! observers run; the property fires once, below.
    text.set(std::move(updated), false);

    INK_FLAG_SET(_fieldFlags, TextFieldFlags::Editing);
    INK_DEFER
    {
        INK_FLAG_CLEAR(_fieldFlags, TextFieldFlags::Editing);
    };
    text.changed().emit(text.get());
}

void TextField::_replaceSelection(std::string_view insertion, EditKind kind)
{
    if (INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly))
        return;

    std::string updated = text.get();

    const usize begin = _selectionBegin();
    const usize end = _selectionEnd();

    updated.erase(begin, end - begin);

    //! Dropped whole rather than truncated: half a multi-byte character would
    //! leave the string invalid and every offset after it meaningless. A
    //! rejected insertion that deleted nothing is not a change at all.
    const bool fits = updated.size() + insertion.size() <= _maxBytes;

    if (!fits && begin == end)
        return;

    _remember(kind);

    if (fits)
        updated.insert(begin, insertion);

    text.set(std::move(updated), false);

    _caret = std::min(begin + (fits ? insertion.size() : 0), text.get().size());
    _selection = _caret;
    _blink = 0.0f;

    INK_FLAG_SET(_fieldFlags, TextFieldFlags::Editing);
    INK_DEFER
    {
        INK_FLAG_CLEAR(_fieldFlags, TextFieldFlags::Editing);
    };
    text.changed().emit(text.get());
}

bool TextField::copy()
{
    UIRoot *ui = root();

    //! A passphrase is not for the clipboard: other applications read it.
    if (ui == nullptr || INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::Obscured) || !_hasSelection())
        return false;

    return ui->clipboard().setText(selectedText());
}

bool TextField::cut()
{
    //! The text goes only once the clipboard has it.
    if (INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly) || !copy())
        return false;

    _deleteSelection(EditKind::Other);
    return true;
}

bool TextField::paste()
{
    UIRoot *ui = root();
    if (INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly) || ui == nullptr)
        return false;

    std::string pasted = utf8::sanitize(ui->clipboard().text(), utf8::Controls::Space);

    //! What still fits once the selection it replaces is gone, cut at a character rather than through one.
    const usize kept = text.get().size() - (_selectionEnd() - _selectionBegin());
    const usize room = _maxBytes > kept ? _maxBytes - kept : 0;
    if (pasted.size() > room)
        pasted.resize(utf8::clampToBoundary(pasted, room));
    if (pasted.empty())
        return false;

    _replaceSelection(pasted, EditKind::Other);
    return true;
}

bool TextField::onTextInput(const TextEvent &event)
{
    if (INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly) || event.text.empty())
        return false;

    //! Typed text is the platform's, but a control character in it (the Tab key arrives as one on some backends)
    //! must not become part of a single line.
    const std::string typed = utf8::sanitize(event.text, utf8::Controls::Drop);
    if (typed.empty())
        return false;

    _replaceSelection(typed, EditKind::Typing);
    return true;
}

bool TextField::onKeyDown(const KeyEvent &event)
{
    const std::string &value = text.get();
    const bool shift = event.mods.shift;
    const bool command = commandKey(event.mods) && !event.mods.alt;

    //! Consumed whether or not they did anything: a Ctrl+C with nothing selected is still not for the parent.
    if (command)
    {
        switch (event.key)
        {
        case wma::KEY_C:
        case wma::KEY_INSERT:
            copy();
            return true;
        case wma::KEY_X:
            cut();
            return true;
        case wma::KEY_V:
            paste();
            return true;
        case wma::KEY_Z:
            if (shift)
                redo();
            else
                undo();
            return true;
        case wma::KEY_Y:
            redo();
            return true;
        default:
            break;
        }
    }
    else if (shift && !event.mods.alt && !event.mods.ctrl)
    {
        //! The older pair of bindings, still what many keyboards and habits reach for.
        if (event.key == wma::KEY_DELETE)
        {
            cut();
            return true;
        }
        if (event.key == wma::KEY_INSERT)
        {
            paste();
            return true;
        }
    }

    switch (event.key)
    {
    case wma::KEY_LEFT:
        setCaret(event.mods.ctrl ? utf8::previousWord(value, _caret) : utf8::previousBoundary(value, _caret), shift);
        return true;

    case wma::KEY_RIGHT:
        setCaret(event.mods.ctrl ? utf8::nextWord(value, _caret) : utf8::nextBoundary(value, _caret), shift);
        return true;

    case wma::KEY_HOME:
        setCaret(0, shift);
        return true;

    case wma::KEY_END:
        setCaret(value.size(), shift);
        return true;

    case wma::KEY_BACKSPACE:
        if (INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly))
            return true;

        if (_hasSelection())
            _deleteSelection(EditKind::Other);
        else
            _deleteRange(utf8::previousBoundary(value, _caret), _caret, EditKind::Deleting);
        return true;

    case wma::KEY_DELETE:
        if (INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::ReadOnly))
            return true;

        if (_hasSelection())
            _deleteSelection(EditKind::Other);
        else
            _deleteRange(_caret, utf8::nextBoundary(value, _caret), EditKind::Deleting);
        return true;

    case wma::KEY_A:
        if (!command)
            return false;

        selectAll();
        return true;

    case wma::KEY_ENTER:
        submitted.emit();
        return true;

    case wma::KEY_ESCAPE:
        if (UIRoot *root = this->root())
            root->clearFocus();
        return true;

    default:
        break;
    }

    //! Everything else -- Tab above all -- is declined, so the root's focus
    //! traversal still works while a field has focus.
    return false;
}

bool TextField::onPointerDown(const PointerEvent &event)
{
    if (event.button != PointerButton::Left)
        return false;

    const WidgetRef alive(this);
    setPressed(true);
    capturePointer();
    if (!alive.get() || !hasPointerCapture())
        return true;

    if (event.kind == PointerKind::Touch)
    {
        _press = Press::Touch;
        _touchClicks = event.clickCount;
        return true;
    }

    _beginEdit(event, event.clickCount);
    return true;
}

void TextField::_beginEdit(const PointerEvent &event, u32 clickCount)
{
    const WidgetRef alive(this);
    requestFocus(FocusReason::Pointer);
    if (!alive.get() || !hasPointerCapture())
        return;

    const Rect content = contentRect();
    const usize byte = _shaped.byteAt({event.position.x - content.min.x + _scrollX, 0.0f});

    if (clickCount >= 3)
    {
        selectAll();
    }
    else if (clickCount == 2)
    {
        const std::string &value = text.get();
        _selection = utf8::previousWord(value, utf8::nextBoundary(value, byte));
        _caret = utf8::nextWord(value, byte);
        invalidatePaint();
    }
    else
    {
        setCaret(byte, event.mods.shift);
        _press = Press::Drag;
    }
}

bool TextField::onPointerMove(const PointerEvent &event)
{
    if (_press != Press::Drag || !hasPointerCapture())
        return false;

    const Rect content = contentRect();
    setCaret(_shaped.byteAt({event.position.x - content.min.x + _scrollX, 0.0f}), true);
    return true;
}

bool TextField::onPointerUp(const PointerEvent &event)
{
    if (event.button != PointerButton::Left)
        return false;
    const bool tap = _press == Press::Touch && hitTest(event.position);
    _press = Press::None;
    if (tap)
    {
        _beginEdit(event, _touchClicks);
        _press = Press::None; //! A finger that lifted has nothing left to drag a selection with.
    }

    return Control::onPointerUp(event);
}

void TextField::onPointerCancel()
{
    _press = Press::None;
    Control::onPointerCancel();
}

void TextField::onFocusIn(FocusReason reason)
{
    Control::onFocusIn(reason);

    //! Tabbed into: the whole value is selected, so typing replaces it. That
    //! is what makes keyboard-only form filling bearable.
    if (reason == FocusReason::Keyboard)
        selectAll();

    _blink = 0.0f;
    setAnimating(true);
}

void TextField::onFocusOut()
{
    Control::onFocusOut();
    _lastEdit = EditKind::None;

    _selection = _caret;
    if (_press == Press::Drag)
        _press = Press::None;
    setAnimating(false);
}

bool TextField::_caretShown() const noexcept
{
    if (root() == nullptr)
        return true;

    const f32 blink = root()->caretBlinkSeconds();
    return std::fmod(_blink, blink * 2.0f) < blink;
}

bool TextField::onTick(f32 deltaSeconds)
{
    //! Repainting only when the picture changes: a caret that is on for half a second does not need the whole UI
    //! recorded sixty times in it, and a focused field would otherwise keep every frame from being idle.
    const bool wasShown = _caretShown();
    _blink += deltaSeconds;
    const bool hovering = tickHover(deltaSeconds);
    if (hovering || _caretShown() != wasShown)
        invalidatePaint();

    //! Ticked for as long as it has focus, for the caret, and until a hover that began without it has finished.
    return hasFocus() || hovering;
}

void TextField::accessibility(AccessibilityInfo &out) const
{
    Widget::accessibility(out);

    out.role = Role::TextField;
    out.set(AccessibilityState::ReadOnly, readOnly());

    //! A passphrase is not read back to anyone.
    if (out.name.empty() && !INK_FLAG_CHECK(_fieldFlags, TextFieldFlags::Obscured))
        out.name = text.get();

    if (out.description.empty())
        out.description = _placeholder;
}

} // namespace aura3d::ui
