#ifndef AURA_UI_WIDGETS_CONTROLS_H
#define AURA_UI_WIDGETS_CONTROLS_H

#pragma once

#include <string>
#include <vector>

#include "aura/UI/Core/Animation.h"
#include "aura/UI/Core/Property.h"
#include "aura/UI/Widgets/Basic.h"

/**
 * @file Controls.h
 * @brief The widgets that take input.
 *
 * Every one of them is composed rather than derived: a Button owns a Label, a
 * CheckBox owns a Label and draws a box beside it. Nothing here subclasses
 * another control to change how it looks -- that is what @ref Style is for.
 */

namespace aura3d::ui {

/**
 * @class Control
 * @brief Shared behaviour of anything that reacts to the pointer.
 *
 * Hover and press are animated rather than switched, because a control that
 * snaps between two colours is the single most obvious tell of a hand-rolled
 * UI. Both are ordinary transitions, so a theme can turn them off by setting
 * the duration to zero.
 */
class Control : public Widget {
public:
    /// Whether the control draws a focus ring when focused by keyboard. On by
    /// default; a control focused by a click does not show one.
    void setShowsFocusRing(bool shows) noexcept { _showsFocusRing = shows; }

protected:
    Control();

    void onPointerEnter() override;
    void onPointerLeave() override;
    void onFocusIn(FocusReason reason) override;
    void onFocusOut() override;

    bool onTick(f32 deltaSeconds) override;

    /// Interpolation between the style's normal and hovered colours.
    [[nodiscard]] f32 hoverAmount() const noexcept { return _hover.value(); }
    [[nodiscard]] bool pressed() const noexcept { return _pressed; }

    /// The surface colour for the control's current state, blended across the
    /// hover transition.
    [[nodiscard]] glm::vec4 surfaceColor(const WidgetStyle& style) const;

    /// Draws the focus ring, if one is due. Call at the end of paint().
    void paintFocusRing(DrawList& out, const WidgetStyle& style, const Rect& bounds) const;

    /// @{
    /// Press tracking shared by every clickable control: capture on press,
    /// fire on release inside. Subclasses call activate() to do the work.
    bool onPointerDown(const PointerEvent& event) override;
    bool onPointerUp(const PointerEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    /// @}

    /// What the control does when clicked, or when Space/Enter is pressed on
    /// it. The one place a subclass has to fill in.
    virtual void activate();

    void setPressed(bool pressed);

private:
    Transition<f32> _hover{0.0f};
    bool _pressed = false;
    bool _showsFocusRing = true;
    bool _focusRingVisible = false;
};

/**
 * @class Button
 * @brief A labelled push button.
 *
 * @code
 * auto& launch = row.add<Button>("Launch");
 * launch.clicked.connect([this] { launchApplication(); });
 * @endcode
 */
class Button final : public Control {
public:
    explicit Button(std::string text = {});

    /// Fires on release inside the button, and on Space or Enter while it has
    /// focus. Not on press: a press that drags away is not a click.
    Signal<> clicked;

    void setText(std::string text);
    [[nodiscard]] const std::string& text() const noexcept;

    /// The button's own label, for restyling it or replacing its content.
    [[nodiscard]] Label& label() noexcept { return *_label; }

    void accessibility(AccessibilityInfo& out) const override;

protected:
    void paint(DrawList& out) override;
    void activate() override;

    glm::vec2 measureContent(const Constraints& available) override;

private:
    Label* _label = nullptr;
};

/**
 * @class CheckBox
 * @brief A box and a label, toggled by either.
 *
 * @code
 * auto& wireframe = column.add<CheckBox>("Wireframe");
 * wireframe.checked.changed().connect([&](bool on) { renderer.setWireframe(on); });
 * @endcode
 */
class CheckBox final : public Control {
public:
    explicit CheckBox(std::string text = {}, bool checked = false);

    Property<bool> checked;

    void setText(std::string text);
    [[nodiscard]] Label& label() noexcept { return *_label; }

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void arrangeContent(const Rect& content) override;

    void paint(DrawList& out) override;
    void activate() override;

    /// The tick box itself. A radio button is the same widget with a round
    /// box and a dot, which is why this is a hook rather than a constant.
    [[nodiscard]] Rect markBox(const Rect& content) const;

    /// Draws what a checked box contains. Overridden to make it a dot.
    virtual void paintMark(DrawList& out, const Rect& box, const glm::vec4& color);

    Label* _label = nullptr;
    Transition<f32> _check{0.0f};

private:
    bool onTick(f32 deltaSeconds) override;
};

class RadioGroup;

/// One option of a @ref RadioGroup. Looks like a CheckBox with a round box,
/// and behaves differently in exactly one way: it cannot be unchecked by
/// clicking it.
class RadioButton final : public Control {
public:
    explicit RadioButton(std::string text = {});

    Property<bool> checked;

    void setText(std::string text);
    [[nodiscard]] Label& label() noexcept { return *_label; }

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void arrangeContent(const Rect& content) override;
    void paint(DrawList& out) override;
    void activate() override;

private:
    Label* _label = nullptr;
};

/**
 * @class RadioGroup
 * @brief Makes a set of @ref RadioButton exclusive.
 *
 * Not a widget: the buttons stay wherever the layout wants them, which is the
 * whole reason a group is a separate object rather than a container.
 *
 * @code
 * RadioGroup quality;
 * quality.add(column.add<RadioButton>("Low"));
 * quality.add(column.add<RadioButton>("High"));
 * quality.select(1);
 * quality.selectionChanged.connect([](int index) { ... });
 * @endcode
 */
class RadioGroup {
public:
    /// Fires with the index of the newly selected button, or -1.
    Signal<int> selectionChanged;

    /// Adds @p button to the group. It must outlive the group, which is the
    /// normal case: both are owned by the same tree.
    void add(RadioButton& button);

    void select(int index);
    [[nodiscard]] int selected() const noexcept { return _selected; }
    [[nodiscard]] usize size() const noexcept { return _buttons.size(); }

private:
    std::vector<RadioButton*> _buttons;
    std::vector<ScopedConnection> _connections;
    int _selected = -1;
};

/**
 * @class Selectable
 * @brief A row that highlights under the pointer and reports activation.
 *
 * The shared row of every list-shaped thing in the toolkit: a list entry, a
 * drop-down option, a menu command, a tab. They differ by which @ref Part they
 * read (Widget::setPart()) and by what they are put inside, not by being
 * different widgets.
 *
 * @code
 * auto& row = list.add<Selectable>("Rename");
 * row.setDetail("F2");
 * row.activated.connect([&] { rename(); });
 * @endcode
 */
class Selectable final : public Control {
public:
    explicit Selectable(std::string text = {});

    /// Drawn as the current choice. Set by whatever owns the list; a
    /// Selectable does not select itself, because only its owner knows
    /// whether the list is single- or multiple-choice.
    Property<bool> selected;

    /// Clicked, or Space/Enter while focused.
    Signal<> activated;

    void setText(std::string text);
    [[nodiscard]] const std::string& text() const noexcept;

    /// Trailing text, right-aligned: a menu's shortcut, a list row's value.
    void setDetail(std::string text);

    [[nodiscard]] Label& label() noexcept { return *_label; }

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void arrangeContent(const Rect& content) override;
    void paint(DrawList& out) override;
    void activate() override;

private:
    Label* _label = nullptr;
    Label* _detail = nullptr; //! Null until setDetail() is called.
};

/**
 * @class Slider
 * @brief A draggable value between two bounds.
 *
 * Dragging is tracked to release even after the pointer leaves the track, so a
 * fast drag does not snap back. Left/Right adjust by one step when focused.
 */
class Slider final : public Control {
public:
    explicit Slider(f32 minimum = 0.0f, f32 maximum = 1.0f);

    Property<f32> value;

    void setRange(f32 minimum, f32 maximum);

    /// Quantisation. Zero (the default) is continuous; the keyboard then uses
    /// a fiftieth of the range.
    void setStep(f32 step);

    [[nodiscard]] f32 minimum() const noexcept { return _minimum; }
    [[nodiscard]] f32 maximum() const noexcept { return _maximum; }

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void paint(DrawList& out) override;

    bool onPointerDown(const PointerEvent& event) override;
    bool onPointerMove(const PointerEvent& event) override;
    bool onPointerUp(const PointerEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;

private:
    /// Value at pointer position @p x, snapped to the step and clamped.
    [[nodiscard]] f32 _valueAt(f32 x) const;

    /// Where in [0,1] the current value sits.
    [[nodiscard]] f32 _fraction() const noexcept;

    [[nodiscard]] Rect _track(const Rect& content) const;
    [[nodiscard]] f32 _knobRadius() const noexcept;

    void _commit(f32 raw);

    f32 _minimum = 0.0f;
    f32 _maximum = 1.0f;
    f32 _step = 0.0f;
};

/// A read-only bar. @c value is a fraction in [0,1]; set it above 1 and it
/// clamps rather than overflowing its track.
class ProgressBar final : public Widget {
public:
    explicit ProgressBar(f32 value = 0.0f);

    Property<f32> value;

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void paint(DrawList& out) override;
};

/**
 * @class TextField
 * @brief A single-line editable field.
 *
 * Reads the platform's committed text rather than translating key codes, so it
 * is correct through dead keys, IME and any keyboard layout. Caret and
 * selection are byte offsets into valid UTF-8 -- every movement goes through
 * @c ui::utf8, so nothing can leave the caret inside a character.
 *
 * @code
 * auto& name = form.add<TextField>();
 * name.setPlaceholder("Scene name");
 * name.submitted.connect([&] { rename(name.text.get()); });
 * @endcode
 */
class TextField final : public Control {
public:
    explicit TextField(std::string text = {});

    Property<std::string> text;

    /// Fires on Enter.
    Signal<> submitted;

    /// Drawn dimmed while the field is empty.
    void setPlaceholder(std::string placeholder);

    /// Upper bound on the stored text. Input past it is dropped whole, never
    /// cutting a character in half.
    void setMaxBytes(usize bytes) noexcept { _maxBytes = bytes; }

    void setReadOnly(bool readOnly);
    [[nodiscard]] bool readOnly() const noexcept { return _readOnly; }

    void selectAll();
    void setCaret(usize byte, bool extendSelection = false);
    [[nodiscard]] usize caret() const noexcept { return _caret; }

    [[nodiscard]] bool wantsTextInput() const noexcept override { return !_readOnly; }

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void paint(DrawList& out) override;

    bool onPointerDown(const PointerEvent& event) override;
    bool onPointerMove(const PointerEvent& event) override;
    bool onPointerUp(const PointerEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    bool onTextInput(const TextEvent& event) override;

    void onFocusIn(FocusReason reason) override;
    void onFocusOut() override;

    bool onTick(f32 deltaSeconds) override;

private:
    void _reshape();

    /// Replaces the selection (or inserts at the caret) with @p insertion.
    void _replaceSelection(std::string_view insertion);

    void _deleteSelection();

    [[nodiscard]] bool _hasSelection() const noexcept { return _selection != _caret; }
    [[nodiscard]] usize _selectionBegin() const noexcept { return std::min(_selection, _caret); }
    [[nodiscard]] usize _selectionEnd() const noexcept { return std::max(_selection, _caret); }

    /// Slides the horizontal scroll so the caret is inside the viewport.
    void _revealCaret(f32 viewportWidth);

    [[nodiscard]] TextStyle _style() const;

    std::string _placeholder;

    ShapedText _shaped;
    ShapedText _placeholderShaped;

    //! What _shaped was produced from, so typing re-shapes once per keystroke
    //! and a repaint re-shapes not at all.
    std::string _shapedSource;
    f32 _shapedFontSize = 0.0f;

    usize _caret = 0;
    usize _selection = 0; //! The other end of the selection; equal to _caret when there is none.
    usize _maxBytes = 1024;

    f32 _scrollX = 0.0f;
    f32 _blink = 0.0f;

    bool _readOnly = false;
    bool _dragging = false;
    std::vector<Rect> _selectionRects;
};

} // namespace aura3d::ui

#endif // AURA_UI_WIDGETS_CONTROLS_H
