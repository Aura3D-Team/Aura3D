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

//! Sets a flag for a scope, however it is left.
class FlagScope
{
  public:
    explicit FlagScope(bool &flag) noexcept : _flag(flag)
    {
        _flag = true;
    }
    ~FlagScope()
    {
        _flag = false;
    }
    FlagScope(const FlagScope &) = delete;
    FlagScope &operator=(const FlagScope &) = delete;

  private:
    bool &_flag;
};

/// Blends in premultiplied space.
[[nodiscard]] glm::vec4 mix(const glm::vec4 &a, const glm::vec4 &b, f32 t) noexcept
{
    const f32 u = std::clamp(t, 0.0f, 1.0f);

    const f32 alpha = glm::mix(a.a, b.a, u);

    if (alpha <= 0.0f)
    {
        return glm::vec4(0.0f);
    }

    const glm::vec3 premultiplied = glm::vec3(a) * (a.a * (1.0f - u)) + glm::vec3(b) * (b.a * u);

    const glm::vec3 rgb = premultiplied / alpha;

    return glm::vec4(rgb, alpha);
}

} // namespace

// =============================================================================
// Control
// =============================================================================

Control::Control()
{
    setFocusable(true);
}

wma::SystemCursor Control::cursor() const noexcept
{
    return _cursor;
}

void Control::setCursor(wma::SystemCursor cursor) noexcept
{
    _cursor = cursor;
}

void Control::onPointerEnter()
{
    _hover.to(1.0f, theme().metrics.hoverSeconds);
    setAnimating(true);
}

void Control::onPointerLeave()
{
    _hover.to(0.0f, theme().metrics.hoverSeconds);
    setAnimating(true);
}

void Control::onFocusIn(FocusReason reason)
{
    //! A control clicked into does not show a ring; one tabbed into does. That
    //! difference is the whole reason FocusReason is carried.
    _focusRingVisible = _showsFocusRing && reason != FocusReason::Pointer;
    invalidatePaint();
}

void Control::onPointerCancel()
{
    setPressed(false);
}

void Control::onFocusOut()
{
    _focusRingVisible = false;
    invalidatePaint();
}

bool Control::onTick(f32 deltaSeconds)
{
    const bool running = tickHover(deltaSeconds);
    if (running)
        invalidatePaint();
    return _hover.running();
}

bool Control::tickHover(f32 deltaSeconds) noexcept
{
    //! The tick that finishes the transition still moved the value, so it counts.
    const bool moving = _hover.running();
    _hover.tick(deltaSeconds);
    return moving;
}

void Control::setPressed(bool pressed)
{
    if (_pressed == pressed)
        return;

    _pressed = pressed;
    invalidatePaint();
}

glm::vec4 Control::surfaceColor(const WidgetStyle &style) const
{
    if (_pressed)
        return style.surface.active;

    return mix(style.surface.normal, style.surface.hovered, _hover.value());
}

void Control::paintFocusRing(DrawList &out, const WidgetStyle &style, const Rect &bounds) const
{
    if (!_focusRingVisible || !hasFocus())
        return;

    out.strokeRect(bounds.grown(1.0f), style.accent, 1.0f, Corners::all(style.rounding + 1.0f));
}

bool Control::onPointerDown(const PointerEvent &event)
{
    if (event.button != PointerButton::Left || !hitTest(event.position))
        return false;

    const WidgetRef alive(this);
    setPressed(true);
    capturePointer();
    if (!alive.get() || !hasPointerCapture())
        return true;
    requestFocus(FocusReason::Pointer);
    return true;
}

bool Control::onPointerUp(const PointerEvent &event)
{
    if (event.button != PointerButton::Left || !_pressed)
        return false;

    const WidgetRef alive(this);
    setPressed(false);
    releasePointer();
    if (!alive.get() || !root() || !effectivelyEnabled() || !effectivelyVisible())
        return true;

    //! A press that wandered off the control before release is a cancelled
    //! click, not a click somewhere else.
    if (hitTest(event.position))
        activate();

    return true;
}

bool Control::onKeyDown(const KeyEvent &event)
{
    if (!hasFocus())
        return false;
    if (event.key != wma::KEY_SPACE && event.key != wma::KEY_ENTER)
        return false;

    if (!event.repeat)
        activate();
    return true;
}

void Control::activate()
{
}

// =============================================================================
// Button
// =============================================================================

Button::Button(std::string text)
{
    _part = Part::Button;

    _label = &add<Label>(std::move(text));
    _label->setAlign(Align::Center);
}

void Button::setText(std::string text)
{
    _label->text = std::move(text);
}

const std::string &Button::text() const noexcept
{
    return _label->text.get();
}

glm::vec2 Button::measureContent(const Constraints &available)
{
    const glm::vec2 content = Widget::measureContent(available);
    const WidgetStyle style = resolvedStyle();

    //! A button is at least a row tall whatever its label says, so a column of
    //! them has one rhythm rather than one height per string.
    const f32 height = style.height.value_or(theme().metrics.rowHeight);

    //! The text inset is in contentInsets(), which measure() adds to the width.
    return {content.x, std::max(content.y, height)};
}

Thickness Button::contentInsets() const noexcept
{
    Thickness insets = layout().padding;
    const f32 inset = resolvedStyle().padding;
    insets.left += inset;
    insets.right += inset;
    return insets;
}

void Button::paint(DrawList &out)
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

void Button::accessibility(AccessibilityInfo &out) const
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

    checked.changed().connect(
        [this](bool on)
        {
            _check.to(on ? 1.0f : 0.0f, theme().metrics.hoverSeconds);
            setAnimating(true);
        });
}

void CheckBox::setText(std::string text)
{
    _label->text = std::move(text);
}

Rect CheckBox::markBox(const Rect &content) const
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 side = row * style.markScale;

    return Rect::fromSize({content.min.x, content.min.y + (content.height() - side) * 0.5f}, {side, side});
}

glm::vec2 CheckBox::measureContent(const Constraints &available)
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

void CheckBox::arrangeContent(const Rect &content)
{
    const WidgetStyle style = resolvedStyle();
    const Rect box = markBox(content);

    const Rect labelSlot{{box.max.x + style.padding, content.min.y}, content.max};
    _label->arrange(labelSlot);
}

void CheckBox::paintMark(DrawList &out, const Rect &box, const glm::vec4 &color)
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

void CheckBox::paint(DrawList &out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    const Rect box = markBox(contentRect());

    out.drawRect(box, withAlpha(surfaceColor(style), alpha),
                 withAlpha(style.border.pick(checked.get(), isHovered()), alpha), style.borderWidth,
                 Corners::all(style.rounding));

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
    //! The tick that finishes the mark still moved it, so it is painted.
    const bool markMoving = _check.running();
    _check.tick(deltaSeconds);
    if (markMoving)
        invalidatePaint();

    const bool hover = Control::onTick(deltaSeconds);
    return _check.running() || hover;
}

void CheckBox::accessibility(AccessibilityInfo &out) const
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

    checked.changed().connect(
        [this](bool)
        {
            invalidatePaint();
        });
}

void RadioButton::setText(std::string text)
{
    _label->text = std::move(text);
}

glm::vec2 RadioButton::measureContent(const Constraints &available)
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

void RadioButton::arrangeContent(const Rect &content)
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 side = row * style.markScale;

    _label->arrange(Rect{{content.min.x + side + style.padding, content.min.y}, content.max});
}

void RadioButton::paint(DrawList &out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    const Rect content = contentRect();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 side = row * style.markScale;

    const Rect box = Rect::fromSize({content.min.x, content.min.y + (content.height() - side) * 0.5f}, {side, side});

    out.drawRect(box, withAlpha(surfaceColor(style), alpha),
                 withAlpha(style.border.pick(checked.get(), isHovered()), alpha), style.borderWidth,
                 Corners::all(style.rounding));

    if (checked.get())
        out.fillRect(box.inset(side * style.markInset), withAlpha(style.accent, alpha), Corners::all(style.rounding));

    paintFocusRing(out, style, box);
}

void RadioButton::activate()
{
    //! Unlike a check box, clicking a selected radio does not clear it: the
    //! group must always have a selection once it has one.
    if (effectivelyEnabled() && !checked.get())
        checked = true;
}

void RadioButton::accessibility(AccessibilityInfo &out) const
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

void RadioGroup::add(RadioButton &button)
{
    if (std::ranges::any_of(_buttons,
                            [&button](const WidgetRef &entry)
                            {
                                return entry.get() == &button;
                            }))
        return;
    const auto index = static_cast<int>(_buttons.size());
    _buttons.emplace_back(&button);

    //! Observing the property rather than the click: selecting a button in
    //! code has to clear the others too, and a click sets the property anyway.
    _connections.emplace_back(button.checked.changed().connect(
        [this, index](bool on)
        {
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
    if (index >= 0 && !_buttons[static_cast<usize>(index)].get())
        index = -1;

    const bool changed = _selected != index;
    _selected = index;

    for (usize i = 0; i < _buttons.size(); ++i)
        if (auto *button = static_cast<RadioButton *>(_buttons[i].get()))
            button->checked.set(static_cast<int>(i) == index);

    if (changed)
        selectionChanged.emit(index);
}

int RadioGroup::selected() const noexcept
{
    return _selected >= 0 && _buttons[static_cast<usize>(_selected)].get() ? _selected : -1;
}

// =============================================================================
// Selectable
// =============================================================================

Selectable::Selectable(std::string text)
{
    _part = Part::Selectable;

    _label = &add<Label>(std::move(text));

    selected.changed().connect(
        [this](bool)
        {
            invalidatePaint();
        });
}

void Selectable::setText(std::string text)
{
    _label->text = std::move(text);
}

const std::string &Selectable::text() const noexcept
{
    return _label->text.get();
}

const std::string &Selectable::detail() const noexcept
{
    static const std::string none;
    return _detail ? _detail->text.get() : none;
}

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

glm::vec2 Selectable::measureContent(const Constraints &available)
{
    const WidgetStyle style = resolvedStyle();

    //! The same text inset a Button gives its label. Without it a tab strip
    //! renders as touching words and list rows sit flush against their edge.
    const Constraints inner =
        Constraints::loose({std::max(0.0f, available.max.x - style.padding * 2.0f), available.max.y});

    const glm::vec2 label = _label->measure(inner);
    const glm::vec2 detail = _detail ? _detail->measure(inner) : glm::vec2{0.0f};

    //! The gap only exists when there is something to separate.
    const f32 gap = _detail ? style.padding * 3.0f : 0.0f;

    return {label.x + gap + detail.x + style.padding * 2.0f,
            std::max({label.y, detail.y, style.height.value_or(theme().metrics.rowHeight)})};
}

void Selectable::arrangeContent(const Rect &content)
{
    const WidgetStyle style = resolvedStyle();
    const Rect inner = deflate(content, Thickness::symmetric(style.padding, 0.0f));

    _label->arrange(inner);

    if (!_detail)
        return;

    //! Right-aligned against the row's far edge, whatever the row's width
    //! turned out to be.
    const f32 width = _detail->desiredSize().x;
    _detail->arrange(Rect{{inner.max.x - width, inner.min.y}, inner.max});
}

void Selectable::paint(DrawList &out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    //! Selection outranks hover: a highlighted row that is also pointed at
    //! must still read as the selected one.
    const glm::vec4 fill = selected.get() ? style.surface.active : surfaceColor(style);

    out.drawRect(bounds(), withAlpha(fill, alpha), withAlpha(style.border.pick(selected.get(), isHovered()), alpha),
                 style.borderWidth, Corners::all(style.rounding));

    paintFocusRing(out, style, bounds());
}

void Selectable::activate()
{
    if (effectivelyEnabled())
        activated.emit();
}

void Selectable::accessibility(AccessibilityInfo &out) const
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

    value.changed().connect(
        [this](f32)
        {
            invalidatePaint();
        });
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

Rect Slider::_track(const Rect &content) const
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

glm::vec2 Slider::measureContent(const Constraints &)
{
    const f32 row = resolvedStyle().height.value_or(theme().metrics.rowHeight);

    //! No intrinsic width: a slider is as wide as it is given, and reporting a
    //! width would make it refuse to shrink.
    return {0.0f, row};
}

void Slider::paint(DrawList &out)
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

    out.drawRect(knob, withAlpha(style.accent, alpha), withAlpha(style.border.pick(pressed(), isHovered()), alpha),
                 std::max(1.0f, style.borderWidth), Corners::all(radius));

    paintFocusRing(out, style, knob);
}

bool Slider::onPointerDown(const PointerEvent &event)
{
    const WidgetRef alive(this);
    if (!Control::onPointerDown(event))
        return false;
    if (!alive.get() || !hasPointerCapture())
        return true;

    _commit(_valueAt(event.position.x));
    return true;
}

bool Slider::onPointerMove(const PointerEvent &event)
{
    if (!pressed())
        return false;

    _commit(_valueAt(event.position.x));
    return true;
}

bool Slider::onPointerUp(const PointerEvent &event)
{
    return Control::onPointerUp(event);
}

bool Slider::onKeyDown(const KeyEvent &event)
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

void Slider::accessibility(AccessibilityInfo &out) const
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

    value.changed().connect(
        [this](f32)
        {
            invalidatePaint();
        });
}

glm::vec2 ProgressBar::measureContent(const Constraints &)
{
    return {0.0f, std::max(4.0f, theme().metrics.rowHeight * 0.35f)};
}

void ProgressBar::paint(DrawList &out)
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

void ProgressBar::accessibility(AccessibilityInfo &out) const
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

    text.changed().connect(
        [this](const std::string &updated)
        {
            //! An assignment from outside can land anywhere relative to the caret,
            //! so both ends are pulled back into the new string and onto a
            //! character boundary.
            _caret = utf8::clampToBoundary(updated, std::min(_caret, updated.size()));
            _selection = _caret;

            //! A step is a copy of what the text was, and an assignment from outside has made them all stale.
            if (!_editing)
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
    ITextShaper *shaper = this->shaper();
    if (!shaper)
        return;

    const TextStyle style = _style();

    if (_shapedSource == text.get() && _shapedFontSize == style.pixelSize && _shapedObscured == _obscured &&
        _shaped.scale == shaper->scale())
        return;

    //! One asterisk per byte, not per character: every caret and selection
    //! offset is a byte offset into the real text, and this keeps them valid
    //! in the shaped stand-in without a second mapping.
    if (_obscured)
        shaper->shape(std::string(text.get().size(), '*'), style, kUnbounded, _shaped);
    else
        shaper->shape(text.get(), style, kUnbounded, _shaped);

    _shapedSource = text.get();
    _shapedFontSize = style.pixelSize;
    _shapedObscured = _obscured;
}

void TextField::setObscured(bool obscured)
{
    if (_obscured == obscured)
        return;
    _obscured = obscured;
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
    Thickness insets = layout().padding;
    const f32 inset = resolvedStyle().padding;
    insets.left += inset;
    insets.right += inset;
    return insets;
}

glm::vec2 TextField::measureContent(const Constraints &)
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

void TextField::paint(DrawList &out)
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
    if (focused && !_readOnly && _caretShown())
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
    if (_obscured)
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
        const FlagScope editing(_editing);
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
    return !_readOnly && !_undo.empty();
}

bool TextField::canRedo() const noexcept
{
    return !_readOnly && !_redo.empty();
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

    const FlagScope editing(_editing);
    text.changed().emit(text.get());
}

void TextField::_replaceSelection(std::string_view insertion, EditKind kind)
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

    _remember(kind);

    if (fits)
        updated.insert(begin, insertion);

    text.set(std::move(updated), false);

    _caret = std::min(begin + (fits ? insertion.size() : 0), text.get().size());
    _selection = _caret;
    _blink = 0.0f;

    const FlagScope editing(_editing);
    text.changed().emit(text.get());
}

bool TextField::copy()
{
    UIRoot *ui = root();

    //! A passphrase is not for the clipboard: other applications read it.
    if (ui == nullptr || _obscured || !_hasSelection())
        return false;

    return ui->clipboard().setText(selectedText());
}

bool TextField::cut()
{
    //! The text goes only once the clipboard has it.
    if (_readOnly || !copy())
        return false;

    _deleteSelection(EditKind::Other);
    return true;
}

bool TextField::paste()
{
    UIRoot *ui = root();
    if (_readOnly || ui == nullptr)
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
    if (_readOnly || event.text.empty())
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
        if (_readOnly)
            return true;

        if (_hasSelection())
            _deleteSelection(EditKind::Other);
        else
            _deleteRange(utf8::previousBoundary(value, _caret), _caret, EditKind::Deleting);
        return true;

    case wma::KEY_DELETE:
        if (_readOnly)
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
        _touchPress = true;
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
        _dragging = true;
    }
}

bool TextField::onPointerMove(const PointerEvent &event)
{
    if (!_dragging || !hasPointerCapture())
        return false;

    const Rect content = contentRect();
    setCaret(_shaped.byteAt({event.position.x - content.min.x + _scrollX, 0.0f}), true);
    return true;
}

bool TextField::onPointerUp(const PointerEvent &event)
{
    if (event.button != PointerButton::Left)
        return false;
    _dragging = false;

    const bool tap = _touchPress && hitTest(event.position);
    _touchPress = false;
    if (tap)
    {
        _beginEdit(event, _touchClicks);
        _dragging = false; //! A finger that lifted has nothing left to drag a selection with.
    }

    return Control::onPointerUp(event);
}

void TextField::onPointerCancel()
{
    _touchPress = false;
    _dragging = false;
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
    _dragging = false;
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
    out.readOnly = _readOnly;

    //! A passphrase is not read back to anyone.
    if (out.name.empty() && !_obscured)
        out.name = text.get();

    if (out.description.empty())
        out.description = _placeholder;
}

} // namespace aura3d::ui
