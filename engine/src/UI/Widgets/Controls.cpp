#include "aura/UI/Widgets/Controls.h"

#include <algorithm>
#include <cmath>

#include "aura/UI/Text/Utf8.h"
#include "aura/UI/UIRoot.h"

namespace aura3d::ui {

namespace {

/// How long hover and press take to settle. Short enough to feel immediate,
/// long enough that the eye reads it as motion rather than a jump.
constexpr f32 kHoverSeconds = 0.11f;

constexpr f32 kCaretBlinkSeconds = 0.53f;

[[nodiscard]] glm::vec4 mix(const glm::vec4& a, const glm::vec4& b, f32 t) noexcept
{
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

} // namespace

// =============================================================================
// Control
// =============================================================================

Control::Control()
{
    setFocusable(true);
}

void Control::onPointerEnter()
{
    _hover.to(1.0f, kHoverSeconds);
    setAnimating(true);
}

void Control::onPointerLeave()
{
    _hover.to(0.0f, kHoverSeconds);
    setAnimating(true);
}

void Control::onFocusIn(FocusReason reason)
{
    //! A control clicked into does not show a ring; one tabbed into does. That
    //! difference is the whole reason FocusReason is carried.
    _focusRingVisible = _showsFocusRing && reason != FocusReason::Pointer;
    invalidatePaint();
}

void Control::onFocusOut()
{
    _focusRingVisible = false;
    invalidatePaint();
}

bool Control::onTick(f32 deltaSeconds)
{
    const bool running = _hover.tick(deltaSeconds);
    invalidatePaint();
    return running;
}

void Control::setPressed(bool pressed)
{
    if (_pressed == pressed)
        return;

    _pressed = pressed;
    invalidatePaint();
}

glm::vec4 Control::surfaceColor(const WidgetStyle& style) const
{
    if (_pressed)
        return style.surface.active;

    return mix(style.surface.normal, style.surface.hovered, _hover.value());
}

void Control::paintFocusRing(DrawList& out, const WidgetStyle& style, const Rect& bounds) const
{
    if (!_focusRingVisible || !hasFocus())
        return;

    out.strokeRect(bounds.grown(1.0f), style.accent, 1.0f,
                   Corners::all(style.rounding + 1.0f));
}

bool Control::onPointerDown(const PointerEvent& event)
{
    if (event.button != PointerButton::Left)
        return false;

    setPressed(true);
    capturePointer();
    requestFocus(FocusReason::Pointer);
    return true;
}

bool Control::onPointerUp(const PointerEvent& event)
{
    if (!_pressed)
        return false;

    setPressed(false);
    releasePointer();

    //! A press that wandered off the control before release is a cancelled
    //! click, not a click somewhere else.
    if (bounds().contains(event.position))
        activate();

    return true;
}

bool Control::onKeyDown(const KeyEvent& event)
{
    if (event.key != wma::KEY_SPACE && event.key != wma::KEY_ENTER)
        return false;

    activate();
    return true;
}

void Control::activate() {}

// =============================================================================
// Button
// =============================================================================

Button::Button(std::string text)
{
    _part = Part::Button;

    _label = &add<Label>(std::move(text));
    _label->setAlign(Align::Center);
}

void Button::setText(std::string text) { _label->text = std::move(text); }

const std::string& Button::text() const noexcept { return _label->text.get(); }

glm::vec2 Button::measureContent(const Constraints& available)
{
    const glm::vec2 content = Widget::measureContent(available);
    const WidgetStyle style = resolvedStyle();

    //! A button is at least a row tall whatever its label says, so a column of
    //! them has one rhythm rather than one height per string.
    const f32 height = style.height.value_or(theme().metrics.rowHeight);

    return {content.x + style.padding * 2.0f, std::max(content.y, height)};
}

void Button::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    out.drawRect(bounds(), withAlpha(surfaceColor(style), alpha),
                 withAlpha(style.border.pick(pressed(), isHovered()), alpha), style.borderWidth,
                 Corners::all(style.rounding));

    paintFocusRing(out, style, bounds());
}

void Button::activate()
{
    if (!effectivelyEnabled())
        return;

    clicked.emit();
}

void Button::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::Button;
    if (out.name.empty())
        out.name = _label->text.get();
}

// =============================================================================
// CheckBox
// =============================================================================

CheckBox::CheckBox(std::string text, bool value) : checked(value)
{
    _part = Part::Checkbox;

    _label = &add<Label>(std::move(text));
    _check.reset(value ? 1.0f : 0.0f);

    checked.changed().connect([this](bool on) {
        _check.to(on ? 1.0f : 0.0f, kHoverSeconds);
        setAnimating(true);
    });
}

void CheckBox::setText(std::string text) { _label->text = std::move(text); }

Rect CheckBox::markBox(const Rect& content) const
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 side = row * style.markScale;

    return Rect::fromSize({content.min.x, content.min.y + (content.height() - side) * 0.5f},
                          {side, side});
}

glm::vec2 CheckBox::measureContent(const Constraints& available)
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 side = row * style.markScale;

    //! The label is measured against what is left after the box and the gap,
    //! so a wrapping label in a narrow column wraps at the right width.
    Constraints labelSpace = available;
    if (!isUnbounded(labelSpace.max.x))
        labelSpace.max.x = std::max(0.0f, labelSpace.max.x - side - style.padding);

    const glm::vec2 label = _label->measure(labelSpace);

    return {side + style.padding + label.x, std::max({row, side, label.y})};
}

void CheckBox::arrangeContent(const Rect& content)
{
    const WidgetStyle style = resolvedStyle();
    const Rect box = markBox(content);

    const Rect labelSlot{{box.max.x + style.padding, content.min.y}, content.max};
    _label->arrange(labelSlot);
}

void CheckBox::paintMark(DrawList& out, const Rect& box, const glm::vec4& color)
{
    const WidgetStyle style = resolvedStyle();

    //! The tick grows out of the middle of the box, so toggling reads as the
    //! box filling rather than as a mark appearing from nowhere.
    const f32 inset = box.width() * style.markInset;
    const f32 scale = _check.value();

    const Rect full = box.inset(inset);
    const glm::vec2 half = full.size() * 0.5f * scale;

    out.fillRect(Rect{full.center() - half, full.center() + half}, color,
                 Corners::all(std::max(0.0f, style.rounding - inset)));
}

void CheckBox::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    const Rect box = markBox(contentRect());

    out.drawRect(box, withAlpha(surfaceColor(style), alpha),
                 withAlpha(style.border.pick(checked.get(), isHovered()), alpha),
                 style.borderWidth, Corners::all(style.rounding));

    if (_check.value() > 0.0f)
        paintMark(out, box, withAlpha(style.accent, alpha * _check.value()));

    paintFocusRing(out, style, box);
}

void CheckBox::activate()
{
    if (!effectivelyEnabled())
        return;

    checked = !checked.get();
}

bool CheckBox::onTick(f32 deltaSeconds)
{
    const bool mark = _check.tick(deltaSeconds);
    const bool hover = Control::onTick(deltaSeconds);
    return mark || hover;
}

void CheckBox::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::CheckBox;
    out.checked = checked.get();
    if (out.name.empty())
        out.name = _label->text.get();
}

// =============================================================================
// RadioButton
// =============================================================================

RadioButton::RadioButton(std::string text)
{
    _part = Part::Radio;

    _label = &add<Label>(std::move(text));

    checked.changed().connect([this](bool) { invalidatePaint(); });
}

void RadioButton::setText(std::string text) { _label->text = std::move(text); }

glm::vec2 RadioButton::measureContent(const Constraints& available)
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 side = row * style.markScale;

    Constraints labelSpace = available;
    if (!isUnbounded(labelSpace.max.x))
        labelSpace.max.x = std::max(0.0f, labelSpace.max.x - side - style.padding);

    const glm::vec2 label = _label->measure(labelSpace);

    return {side + style.padding + label.x, std::max({row, side, label.y})};
}

void RadioButton::arrangeContent(const Rect& content)
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 side = row * style.markScale;

    _label->arrange(Rect{{content.min.x + side + style.padding, content.min.y}, content.max});
}

void RadioButton::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    const Rect content = contentRect();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 side = row * style.markScale;

    const Rect box = Rect::fromSize(
        {content.min.x, content.min.y + (content.height() - side) * 0.5f}, {side, side});

    out.drawRect(box, withAlpha(surfaceColor(style), alpha),
                 withAlpha(style.border.pick(checked.get(), isHovered()), alpha),
                 style.borderWidth, Corners::all(style.rounding));

    if (checked.get())
        out.fillRect(box.inset(side * style.markInset), withAlpha(style.accent, alpha),
                     Corners::all(style.rounding));

    paintFocusRing(out, style, box);
}

void RadioButton::activate()
{
    //! Unlike a check box, clicking a selected radio does not clear it: the
    //! group must always have a selection once it has one.
    if (effectivelyEnabled() && !checked.get())
        checked = true;
}

void RadioButton::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::RadioButton;
    out.checked = checked.get();
    if (out.name.empty())
        out.name = _label->text.get();
}

// =============================================================================
// RadioGroup
// =============================================================================

void RadioGroup::add(RadioButton& button)
{
    const auto index = static_cast<int>(_buttons.size());
    _buttons.push_back(&button);

    //! Observing the property rather than the click: selecting a button in
    //! code has to clear the others too, and a click sets the property anyway.
    _connections.emplace_back(button.checked.changed().connect([this, index](bool on) {
        if (on)
            select(index);
    }));

    if (button.checked.get())
        select(index);
}

void RadioGroup::select(int index)
{
    if (index < -1 || index >= static_cast<int>(_buttons.size()))
        return;

    const bool changed = _selected != index;
    _selected = index;

    for (usize i = 0; i < _buttons.size(); ++i)
        _buttons[i]->checked.set(static_cast<int>(i) == index);

    if (changed)
        selectionChanged.emit(index);
}

// =============================================================================
// Selectable
// =============================================================================

Selectable::Selectable(std::string text)
{
    _part = Part::Selectable;

    _label = &add<Label>(std::move(text));

    selected.changed().connect([this](bool) { invalidatePaint(); });
}

void Selectable::setText(std::string text) { _label->text = std::move(text); }

const std::string& Selectable::text() const noexcept { return _label->text.get(); }

void Selectable::setDetail(std::string text)
{
    if (!_detail)
    {
        _detail = &add<Label>(std::move(text));

        //! Dimmed, because a shortcut is a reminder rather than the point of
        //! the row it sits on.
        glm::vec4 muted = resolvedStyle().text;
        muted.a *= 0.55f;
        _detail->style().textColor(muted);
        return;
    }

    _detail->text = std::move(text);
}

glm::vec2 Selectable::measureContent(const Constraints& available)
{
    const WidgetStyle style = resolvedStyle();

    const glm::vec2 label = _label->measure(available);
    const glm::vec2 detail = _detail ? _detail->measure(available) : glm::vec2{0.0f};

    //! The gap only exists when there is something to separate.
    const f32 gap = _detail ? style.padding * 3.0f : 0.0f;

    return {label.x + gap + detail.x,
            std::max({label.y, detail.y, style.height.value_or(theme().metrics.rowHeight)})};
}

void Selectable::arrangeContent(const Rect& content)
{
    _label->arrange(content);

    if (!_detail)
        return;

    //! Right-aligned against the row's far edge, whatever the row's width
    //! turned out to be.
    const f32 width = _detail->desiredSize().x;
    _detail->arrange(Rect{{content.max.x - width, content.min.y}, content.max});
}

void Selectable::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    //! Selection outranks hover: a highlighted row that is also pointed at
    //! must still read as the selected one.
    const glm::vec4 fill =
        selected.get() ? style.surface.active : surfaceColor(style);

    out.drawRect(bounds(), withAlpha(fill, alpha),
                 withAlpha(style.border.pick(selected.get(), isHovered()), alpha),
                 style.borderWidth, Corners::all(style.rounding));

    paintFocusRing(out, style, bounds());
}

void Selectable::activate()
{
    if (effectivelyEnabled())
        activated.emit();
}

void Selectable::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::ListItem;
    out.selected = selected.get();

    if (out.name.empty())
        out.name = _label->text.get();
}

// =============================================================================
// Slider
// =============================================================================

Slider::Slider(f32 minimum, f32 maximum) : _minimum(minimum), _maximum(maximum)
{
    _part = Part::Slider;

    value.changed().connect([this](f32) { invalidatePaint(); });
}

void Slider::setRange(f32 minimum, f32 maximum)
{
    _minimum = minimum;
    _maximum = std::max(maximum, minimum + 1.0e-6f);

    value.set(std::clamp(value.get(), _minimum, _maximum));
    invalidatePaint();
}

void Slider::setStep(f32 step)
{
    _step = std::max(0.0f, step);
}

f32 Slider::_fraction() const noexcept
{
    return std::clamp((value.get() - _minimum) / (_maximum - _minimum), 0.0f, 1.0f);
}

f32 Slider::_knobRadius() const noexcept
{
    return resolvedStyle().height.value_or(theme().metrics.rowHeight) * 0.5f;
}

Rect Slider::_track(const Rect& content) const
{
    //! The track stops a knob's radius short of each end, so the knob's own
    //! edge -- not its centre -- reaches the extremes of the control.
    const f32 radius = _knobRadius();
    const f32 height = std::max(3.0f, radius * 0.5f);

    return Rect{{content.min.x + radius, content.center().y - height * 0.5f},
                {content.max.x - radius, content.center().y + height * 0.5f}};
}

f32 Slider::_valueAt(f32 x) const
{
    const Rect track = _track(contentRect());
    const f32 span = std::max(1.0f, track.width());

    return _minimum + (_maximum - _minimum) * std::clamp((x - track.min.x) / span, 0.0f, 1.0f);
}

void Slider::_commit(f32 raw)
{
    f32 snapped = std::clamp(raw, _minimum, _maximum);

    if (_step > 0.0f)
        snapped = _minimum + std::round((snapped - _minimum) / _step) * _step;

    value.set(std::clamp(snapped, _minimum, _maximum));
}

glm::vec2 Slider::measureContent(const Constraints&)
{
    const f32 row = resolvedStyle().height.value_or(theme().metrics.rowHeight);

    //! No intrinsic width: a slider is as wide as it is given, and reporting a
    //! width would make it refuse to shrink.
    return {0.0f, row};
}

void Slider::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    const Rect content = contentRect();
    const Rect track = _track(content);
    const f32 fraction = _fraction();

    const Corners rounded = Corners::all(track.height() * 0.5f);

    out.fillRect(track, withAlpha(surfaceColor(style), alpha), rounded);

    if (fraction > 0.0f)
    {
        const Rect filled{track.min, {track.min.x + track.width() * fraction, track.max.y}};
        out.fillRect(filled, withAlpha(style.accent, alpha), rounded);
    }

    const f32 radius = _knobRadius();
    const glm::vec2 centre{track.min.x + track.width() * fraction, content.center().y};

    const Rect knob{centre - radius, centre + radius};

    out.drawRect(knob, withAlpha(style.accent, alpha),
                 withAlpha(style.border.pick(pressed(), isHovered()), alpha),
                 std::max(1.0f, style.borderWidth), Corners::all(radius));

    paintFocusRing(out, style, knob);
}

bool Slider::onPointerDown(const PointerEvent& event)
{
    if (!Control::onPointerDown(event))
        return false;

    _commit(_valueAt(event.position.x));
    return true;
}

bool Slider::onPointerMove(const PointerEvent& event)
{
    if (!pressed())
        return false;

    _commit(_valueAt(event.position.x));
    return true;
}

bool Slider::onPointerUp(const PointerEvent& event)
{
    return Control::onPointerUp(event);
}

bool Slider::onKeyDown(const KeyEvent& event)
{
    const f32 step = _step > 0.0f ? _step : (_maximum - _minimum) / 50.0f;

    switch (event.key)
    {
    case wma::KEY_LEFT:
    case wma::KEY_DOWN:
        _commit(value.get() - step);
        return true;

    case wma::KEY_RIGHT:
    case wma::KEY_UP:
        _commit(value.get() + step);
        return true;

    case wma::KEY_HOME:
        _commit(_minimum);
        return true;

    case wma::KEY_END:
        _commit(_maximum);
        return true;

    default:
        break;
    }

    //! Space and Enter do nothing to a slider, so they are not swallowed here.
    return false;
}

void Slider::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::Slider;
    out.value = value.get();
    out.minValue = _minimum;
    out.maxValue = _maximum;
}

// =============================================================================
// ProgressBar
// =============================================================================

ProgressBar::ProgressBar(f32 initial) : value(std::clamp(initial, 0.0f, 1.0f))
{
    _part = Part::ProgressBar;
    setHitTestVisible(false);

    value.changed().connect([this](f32) { invalidatePaint(); });
}

glm::vec2 ProgressBar::measureContent(const Constraints&)
{
    return {0.0f, std::max(4.0f, theme().metrics.rowHeight * 0.35f)};
}

void ProgressBar::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    const Rect content = contentRect();
    const Corners rounded = Corners::all(std::min(style.rounding, content.height() * 0.5f));

    out.fillRect(content, withAlpha(style.surface.normal, alpha), rounded);

    const f32 fraction = std::clamp(value.get(), 0.0f, 1.0f);
    if (fraction <= 0.0f)
        return;

    out.fillRect({content.min, {content.min.x + content.width() * fraction, content.max.y}},
                 withAlpha(style.accent, alpha), rounded);
}

void ProgressBar::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::ProgressBar;
    out.value = value.get();
    out.minValue = 0.0f;
    out.maxValue = 1.0f;
}

// =============================================================================
// TextField
// =============================================================================

TextField::TextField(std::string value) : text(std::move(value))
{
    _part = Part::TextField;

    text.changed().connect([this](const std::string& updated) {
        //! An assignment from outside can land anywhere relative to the caret,
        //! so both ends are pulled back into the new string and onto a
        //! character boundary.
        _caret = utf8::clampToBoundary(updated, std::min(_caret, updated.size()));
        _selection = _caret;
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
    if (_readOnly == readOnly)
        return;

    _readOnly = readOnly;
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
    ITextShaper* shaper = this->shaper();
    if (!shaper)
        return;

    const TextStyle style = _style();

    if (_shapedSource == text.get() && _shapedFontSize == style.pixelSize)
        return;

    shaper->shape(text.get(), style, kUnbounded, _shaped);

    _shapedSource = text.get();
    _shapedFontSize = style.pixelSize;
}

glm::vec2 TextField::measureContent(const Constraints&)
{
    _reshape();

    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);

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

void TextField::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;
    const bool focused = hasFocus();

    out.drawRect(bounds(), withAlpha(surfaceColor(style), alpha),
                 withAlpha(style.border.pick(focused, isHovered()), alpha), style.borderWidth,
                 Corners::all(style.rounding));

    const Rect content = contentRect();
    _revealCaret(content.width());

    const ClipScope clipped(out, content);

    const glm::vec2 origin{
        content.min.x - _scrollX,
        content.min.y + alignOffset(Alignment::Center, content.height(), _shaped.size.y)};

    if (text.get().empty() && !_placeholder.empty())
    {
        if (ITextShaper* shaper = this->shaper(); shaper && _placeholderShaped.empty())
            shaper->shape(_placeholder, _style(), kUnbounded, _placeholderShaped);

        out.drawText(_placeholderShaped, {content.min.x, origin.y},
                     withAlpha(style.text, alpha * 0.45f));
    }

    if (_hasSelection())
    {
        _selectionRects.clear();
        _shaped.selectionRects(_selectionBegin(), _selectionEnd(), _selectionRects);

        for (const Rect& rect : _selectionRects)
            out.fillRect(rect.translated(origin), withAlpha(style.accent, alpha * 0.35f));
    }

    out.drawText(_shaped, origin, withAlpha(style.text, alpha));

    //! Blink is a square wave on the tick clock, so the caret is solid while
    //! the field is being typed into -- _blink is reset on every edit.
    if (focused && !_readOnly && std::fmod(_blink, kCaretBlinkSeconds * 2.0f) < kCaretBlinkSeconds)
        out.fillRect(_shaped.caretRect(_caret, 1.0f).translated(origin),
                     withAlpha(style.accent, alpha));

    paintFocusRing(out, style, bounds());
}

void TextField::setCaret(usize byte, bool extendSelection)
{
    _caret = utf8::clampToBoundary(text.get(), std::min(byte, text.get().size()));

    if (!extendSelection)
        _selection = _caret;

    _blink = 0.0f;
    invalidatePaint();
}

void TextField::selectAll()
{
    _selection = 0;
    _caret = text.get().size();
    invalidatePaint();
}

void TextField::_deleteSelection()
{
    if (!_hasSelection())
        return;

    std::string updated = text.get();
    const usize begin = _selectionBegin();

    updated.erase(begin, _selectionEnd() - begin);

    _caret = begin;
    _selection = begin;

    //! Written without notifying so the caret is already correct when
    //! observers run; the property fires once, below.
    text.set(std::move(updated), false);
    text.changed().emit(text.get());
}

void TextField::_replaceSelection(std::string_view insertion)
{
    if (_readOnly)
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

    if (fits)
        updated.insert(begin, insertion);

    text.set(std::move(updated), false);

    _caret = std::min(begin + (fits ? insertion.size() : 0), text.get().size());
    _selection = _caret;
    _blink = 0.0f;

    text.changed().emit(text.get());
}

bool TextField::onTextInput(const TextEvent& event)
{
    if (_readOnly || event.text.empty())
        return false;

    _replaceSelection(event.text);
    return true;
}

bool TextField::onKeyDown(const KeyEvent& event)
{
    const std::string& value = text.get();
    const bool shift = event.mods.shift;

    switch (event.key)
    {
    case wma::KEY_LEFT:
        setCaret(event.mods.ctrl ? utf8::previousWord(value, _caret)
                                 : utf8::previousBoundary(value, _caret),
                 shift);
        return true;

    case wma::KEY_RIGHT:
        setCaret(event.mods.ctrl ? utf8::nextWord(value, _caret)
                                 : utf8::nextBoundary(value, _caret),
                 shift);
        return true;

    case wma::KEY_HOME:
        setCaret(0, shift);
        return true;

    case wma::KEY_END:
        setCaret(value.size(), shift);
        return true;

    case wma::KEY_BACKSPACE:
        if (_readOnly)
            return true;

        if (!_hasSelection())
            _selection = utf8::previousBoundary(value, _caret);

        _deleteSelection();
        return true;

    case wma::KEY_DELETE:
        if (_readOnly)
            return true;

        if (!_hasSelection())
            _selection = utf8::nextBoundary(value, _caret);

        _deleteSelection();
        return true;

    case wma::KEY_A:
        if (!event.mods.ctrl)
            return false;

        selectAll();
        return true;

    case wma::KEY_ENTER:
        submitted.emit();
        return true;

    case wma::KEY_ESCAPE:
        if (UIRoot* root = this->root())
            root->clearFocus();
        return true;

    default:
        break;
    }

    //! Everything else -- Tab above all -- is declined, so the root's focus
    //! traversal still works while a field has focus.
    return false;
}

bool TextField::onPointerDown(const PointerEvent& event)
{
    if (event.button != PointerButton::Left)
        return false;

    setPressed(true);
    capturePointer();
    requestFocus(FocusReason::Pointer);

    const Rect content = contentRect();
    const usize byte = _shaped.byteAt({event.position.x - content.min.x + _scrollX, 0.0f});

    if (event.clickCount >= 3)
    {
        selectAll();
    }
    else if (event.clickCount == 2)
    {
        const std::string& value = text.get();
        _selection = utf8::previousWord(value, utf8::nextBoundary(value, byte));
        _caret = utf8::nextWord(value, byte);
        invalidatePaint();
    }
    else
    {
        setCaret(byte, event.mods.shift);
        _dragging = true;
    }

    return true;
}

bool TextField::onPointerMove(const PointerEvent& event)
{
    if (!_dragging)
        return false;

    const Rect content = contentRect();
    setCaret(_shaped.byteAt({event.position.x - content.min.x + _scrollX, 0.0f}), true);
    return true;
}

bool TextField::onPointerUp(const PointerEvent& event)
{
    _dragging = false;
    return Control::onPointerUp(event);
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

    _selection = _caret;
    _dragging = false;
    setAnimating(false);
}

bool TextField::onTick(f32 deltaSeconds)
{
    _blink += deltaSeconds;
    invalidatePaint();

    Control::onTick(deltaSeconds);

    //! Ticked for as long as it has focus: the caret has to keep blinking
    //! whether or not anything else is animating.
    return hasFocus();
}

void TextField::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::TextField;
    out.readOnly = _readOnly;

    if (out.name.empty())
        out.name = text.get();

    if (out.description.empty())
        out.description = _placeholder;
}

} // namespace aura3d::ui
