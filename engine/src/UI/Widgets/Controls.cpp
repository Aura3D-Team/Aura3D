#include "aura/UI/Widgets/Controls.h"

#include <algorithm>
#include <cmath>

#include "aura/UI/UIRoot.h"

namespace aura3d::ui
{

namespace
{

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

//! A check box's or radio button's mark: a square of @p side at the left of @p content, centred vertically.
[[nodiscard]] Rect markRect(const Rect &content, f32 side) noexcept
{
    return Rect::fromSize({content.min.x, content.min.y + (content.height() - side) * 0.5f}, {side, side});
}

//! The mark, the gap and the label. The label is measured against what is left after the other two, so a
//! wrapping label in a narrow column wraps at the right width.
[[nodiscard]] glm::vec2 measureMarked(Widget &label, const Constraints &available, f32 row, f32 side, f32 gap)
{
    Constraints labelSpace = available;
    if (!isUnbounded(labelSpace.max.x))
        labelSpace.max.x = std::max(0.0f, labelSpace.max.x - side - gap);

    const glm::vec2 size = label.measure(labelSpace);

    return {side + gap + size.x, std::max({row, side, size.y})};
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
    INK_FLAG_CLEAR(_controlFlags, ControlFlags::FocusRingVisible);
    if (INK_FLAG_CHECK(_controlFlags, ControlFlags::ShowsFocusRing) && reason != FocusReason::Pointer)
        INK_FLAG_SET(_controlFlags, ControlFlags::FocusRingVisible);
    invalidatePaint();
}

void Control::onPointerCancel()
{
    setPressed(false);
}

void Control::onFocusOut()
{
    INK_FLAG_CLEAR(_controlFlags, ControlFlags::FocusRingVisible);
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
    if (this->pressed() == pressed)
        return;

    pressed ? INK_FLAG_SET(_controlFlags, ControlFlags::Pressed) : INK_FLAG_CLEAR(_controlFlags, ControlFlags::Pressed);
    invalidatePaint();
}

glm::vec4 Control::surfaceColor(const WidgetStyle &style) const
{
    if (INK_FLAG_CHECK(_controlFlags, ControlFlags::Pressed))
        return style.surface.active;

    return mix(style.surface.normal, style.surface.hovered, _hover.value());
}

Thickness Control::textInsets() const noexcept
{
    Thickness insets = layout().padding;
    const f32 inset = resolvedStyle().padding;
    insets.left += inset;
    insets.right += inset;
    return insets;
}

void Control::paintSurface(DrawList &out, const Rect &rect, const WidgetStyle &style, bool active, f32 alpha) const
{
    out.drawRect(rect, withAlpha(surfaceColor(style), alpha), withAlpha(style.border.pick(interaction(active)), alpha),
                 style.borderWidth, Corners::all(style.rounding));
}

void Control::paintFocusRing(DrawList &out, const WidgetStyle &style, const Rect &bounds) const
{
    if (!INK_FLAG_CHECK(_controlFlags, ControlFlags::FocusRingVisible) || !hasFocus())
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
    if (event.button != PointerButton::Left || !INK_FLAG_CHECK(_controlFlags, ControlFlags::Pressed))
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
    const f32 height = rowHeight(style);

    //! The text inset is in contentInsets(), which measure() adds to the width.
    return {content.x, std::max(content.y, height)};
}

Thickness Button::contentInsets() const noexcept
{
    return textInsets();
}

void Button::paint(DrawList &out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = paintAlpha();

    paintSurface(out, bounds(), style, pressed(), alpha);

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
    const f32 row = rowHeight(style);
    return markRect(content, row * style.markScale);
}

glm::vec2 CheckBox::measureContent(const Constraints &available)
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = rowHeight(style);
    return measureMarked(*_label, available, row, row * style.markScale, style.padding);
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
    const f32 alpha = paintAlpha();

    const Rect box = markBox(contentRect());

    paintSurface(out, box, style, checked.get(), alpha);

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
    out.set(AccessibilityState::Checked, checked.get());
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
    const f32 row = rowHeight(style);
    return measureMarked(*_label, available, row, row * style.markScale, style.padding);
}

void RadioButton::arrangeContent(const Rect &content)
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = rowHeight(style);
    const f32 side = row * style.markScale;

    _label->arrange(Rect{{content.min.x + side + style.padding, content.min.y}, content.max});
}

void RadioButton::paint(DrawList &out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = paintAlpha();

    const Rect box = markRect(contentRect(), rowHeight(style) * style.markScale);

    paintSurface(out, box, style, checked.get(), alpha);

    if (checked.get())
        out.fillRect(box.inset(box.width() * style.markInset), withAlpha(style.accent, alpha),
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

void RadioButton::accessibility(AccessibilityInfo &out) const
{
    Widget::accessibility(out);

    out.role = Role::RadioButton;
    out.set(AccessibilityState::Checked, checked.get());
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

    return {label.x + gap + detail.x + style.padding * 2.0f, std::max({label.y, detail.y, rowHeight(style)})};
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
    const f32 alpha = paintAlpha();

    //! Selection outranks hover: a highlighted row that is also pointed at
    //! must still read as the selected one.
    const glm::vec4 fill = selected.get() ? style.surface.active : surfaceColor(style);

    out.drawRect(bounds(), withAlpha(fill, alpha), withAlpha(style.border.pick(interaction(selected.get())), alpha),
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
    out.set(AccessibilityState::Selected, selected.get());

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
    return rowHeight(resolvedStyle()) * 0.5f;
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
    const f32 row = rowHeight(resolvedStyle());

    //! No intrinsic width: a slider is as wide as it is given, and reporting a
    //! width would make it refuse to shrink.
    return {0.0f, row};
}

void Slider::paint(DrawList &out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = paintAlpha();

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

    out.drawRect(knob, withAlpha(style.accent, alpha), withAlpha(style.border.pick(interaction(pressed())), alpha),
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
    const f32 alpha = paintAlpha();

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

} // namespace aura3d::ui
