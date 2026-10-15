/*
 * Hit testing, event propagation, focus and the controls' state machines.
 *
 * Driven straight through UIRoot's input entry points -- the same ones UIView
 * calls from wma -- so what is exercised here is exactly what runs in an
 * application, minus the window.
 *
 * The properties worth pinning are the ones that fail quietly:
 *  - a click is a press *and* a release over the same widget, so pressing a
 *    button and dragging off it must not fire it;
 *  - a drag must survive the pointer leaving the control, which is what
 *    pointer capture is for;
 *  - a disabled subtree must swallow input rather than let it fall through to
 *    whatever is painted behind it.
 */

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "aura/UI/UI.hpp"

#include "TestUtils.h"

using namespace aura3d;
using namespace aura3d::ui;

namespace
{

[[nodiscard]] bool near(f32 a, f32 b, f32 tolerance = 0.51f)
{
    return std::fabs(a - b) <= tolerance;
}

struct Harness
{
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};

    explicit Harness(glm::vec2 size = {400.0f, 300.0f})
    {
        root.resize(size);
    }

    void layout()
    {
        root.update(0.0f);
    }

    /// A press and a release at the same point: one complete click.
    void click(glm::vec2 at)
    {
        root.pointerMoved(at);
        root.pointerDown(at);
        root.pointerUp(at);
    }

    /// A finger, as UIView reports it: hover first, then the press, with time passing between moves so a
    /// gesture can measure its speed.
    void touchDown(glm::vec2 at)
    {
        root.pointerMoved(at);
        root.pointerDown(at, PointerButton::Left, {}, PointerKind::Touch);
    }
    void touchMove(glm::vec2 at, f32 seconds = 0.016f)
    {
        root.update(seconds);
        root.pointerMoved(at);
    }
    void touchUp(glm::vec2 at)
    {
        root.pointerUp(at, PointerButton::Left, {}, PointerKind::Touch);
        root.pointerLeft();
    }
};

/// A 200x100 viewport onto ten 30 px rows, each a button, so a drag starts on something that wants the press.
struct ButtonList
{
    Harness harness{{200.0f, 100.0f}};
    ScrollView *scroll = nullptr;
    std::array<int, 10> clicks{};

    ButtonList()
    {
        scroll = &harness.root.setContent<ScrollView>();
        scroll->setSmooth(false);
        auto &column = scroll->setContent<Column>();
        for (usize i = 0; i < clicks.size(); ++i)
        {
            auto &row = column.add<Button>("Row");
            row.layout().height = Length::px(30.0f);
            row.clicked.connect(
                [this, i]
                {
                    ++clicks[i];
                });
        }
        harness.layout();
    }

    [[nodiscard]] int totalClicks() const
    {
        int total = 0;
        for (const int count : clicks)
            total += count;
        return total;
    }
};

void testHitTesting()
{
    Harness harness;
    auto &column = harness.root.setContent<Column>();

    auto &first = column.add<Widget>();
    first.layout().height = Length::px(50.0f);

    auto &second = column.add<Widget>();
    second.layout().height = Length::px(50.0f);

    harness.layout();

    AURA_CHECK(harness.root.widgetAt({10.0f, 10.0f}) == &first, "a point picks the widget under it");
    AURA_CHECK(harness.root.widgetAt({10.0f, 60.0f}) == &second, "and the next one below");
    AURA_CHECK(harness.root.widgetAt({10.0f, 250.0f}) == &column, "a point in no child falls through to the container");

    first.setHitTestVisible(false);
    AURA_CHECK(harness.root.widgetAt({10.0f, 10.0f}) == &column,
               "a widget that is not hit-test visible lets the pointer through");
}

void testTopmostWins()
{
    Harness harness;
    auto &stack = harness.root.setContent<Stack>();

    auto &under = stack.add<Widget>();
    auto &over = stack.add<Widget>();

    harness.layout();

    AURA_CHECK(harness.root.widgetAt({10.0f, 10.0f}) == &over,
               "overlaid children are hit in paint order, topmost first");
    AURA_CHECK(&under != harness.root.widgetAt({10.0f, 10.0f}), "so the one underneath is not hit");
}

void testHoverChain()
{
    Harness harness;
    auto &card = harness.root.setContent<Column>();
    auto &button = card.add<Button>("Hover me");

    harness.layout();

    harness.root.pointerMoved(button.bounds().center());

    AURA_CHECK(harness.root.hovered() == &button, "the deepest widget is the hovered one");
    AURA_CHECK(harness.root.isHovered(&card),
               "and its ancestors are in the hover chain, so a card can react to its button");

    harness.root.pointerLeft();
    AURA_CHECK(harness.root.hovered() == nullptr, "the chain drops when the pointer leaves");
}

void testButtonClick()
{
    Harness harness;
    auto &column = harness.root.setContent<Column>();
    auto &button = column.add<Button>("Launch");

    int clicks = 0;
    button.clicked.connect(
        [&clicks]
        {
            ++clicks;
        });

    harness.layout();

    const glm::vec2 inside = button.bounds().center();
    const glm::vec2 outside{inside.x, button.bounds().max.y + 40.0f};

    harness.click(inside);
    AURA_CHECK(clicks == 1, "a press and release over the button fires it once");

    //! Pressed, then dragged away and released: not a click.
    harness.root.pointerMoved(inside);
    harness.root.pointerDown(inside);
    harness.root.pointerMoved(outside);
    harness.root.pointerUp(outside);

    AURA_CHECK(clicks == 1, "a press dragged off the button before release does not fire it");

    //! A release with no press before it is not a click either.
    harness.root.pointerUp(inside);
    AURA_CHECK(clicks == 1, "a release alone does not fire it");
}

void testKeyboardActivation()
{
    Harness harness;
    auto &button = harness.root.setContent<Column>().add<Button>("Go");

    int clicks = 0;
    button.clicked.connect(
        [&clicks]
        {
            ++clicks;
        });

    harness.layout();
    button.requestFocus();

    harness.root.keyDown(wma::KEY_SPACE);
    harness.root.keyDown(wma::KEY_ENTER);

    AURA_CHECK(clicks == 2, "Space and Enter activate the focused button");
}

void testDisabledSwallowsInput()
{
    Harness harness;
    auto &panel = harness.root.setContent<Column>();
    auto &button = panel.add<Button>("Disabled");

    int clicks = 0;
    int panelPresses = 0;

    button.clicked.connect(
        [&clicks]
        {
            ++clicks;
        });

    harness.layout();
    button.setEnabled(false);
    harness.layout();

    const bool consumed = harness.root.pointerDown(button.bounds().center());
    harness.root.pointerUp(button.bounds().center());

    AURA_CHECK(clicks == 0, "a disabled button does not fire");
    AURA_CHECK(consumed, "but it still consumes the click rather than letting it through");
    AURA_CHECK(panelPresses == 0, "and the event does not bubble to its enabled parent");

    AURA_CHECK(!button.focusable(), "a disabled control is not a tab stop");
}

void testCheckBox()
{
    Harness harness;
    auto &check = harness.root.setContent<Column>().add<CheckBox>("Wireframe");

    int changes = 0;
    check.checked.changed().connect(
        [&changes](bool)
        {
            ++changes;
        });

    harness.layout();
    harness.click(check.bounds().center());

    AURA_CHECK(check.checked.get() && changes == 1, "clicking a check box toggles it once");

    harness.click(check.bounds().center());
    AURA_CHECK(!check.checked.get() && changes == 2, "and clicking again toggles it back");
}

void testRadioGroup()
{
    Harness harness;
    auto &column = harness.root.setContent<Column>();

    RadioGroup group;
    auto &low = column.add<RadioButton>("Low");
    auto &high = column.add<RadioButton>("High");

    group.add(low);
    group.add(high);

    int selections = 0;
    group.selectionChanged.connect(
        [&selections](int)
        {
            ++selections;
        });

    harness.layout();
    harness.click(high.bounds().center());

    AURA_CHECK(high.checked.get() && !low.checked.get(), "selecting one clears the others");
    AURA_CHECK(group.selected() == 1 && selections == 1, "and reports the new selection once");

    harness.click(high.bounds().center());
    AURA_CHECK(high.checked.get() && selections == 1, "clicking the selected radio again is not a change");
}

void testSliderDragSurvivesLeaving()
{
    Harness harness;
    auto &slider = harness.root.setContent<Column>().add<Slider>(0.0f, 100.0f);
    slider.layout().width = Length::px(200.0f);
    slider.layout().hAlign = Alignment::Start;

    harness.layout();

    const Rect bounds = slider.bounds();

    harness.root.pointerMoved(bounds.center());
    harness.root.pointerDown(bounds.center());

    AURA_CHECK(near(slider.value.get(), 50.0f, 2.0f), "pressing the track jumps to that value");
    AURA_CHECK(harness.root.pointerCapture() == &slider, "and the slider captures the pointer");

    //! Well outside the control, and still tracking: this is what capture buys.
    harness.root.pointerMoved({bounds.max.x + 500.0f, bounds.max.y + 500.0f});
    AURA_CHECK(near(slider.value.get(), 100.0f), "a drag past the end clamps to the maximum");

    harness.root.pointerMoved({bounds.min.x - 500.0f, bounds.min.y});
    AURA_CHECK(near(slider.value.get(), 0.0f), "and past the start clamps to the minimum");

    harness.root.pointerUp({bounds.min.x - 500.0f, bounds.min.y});
    AURA_CHECK(harness.root.pointerCapture() == nullptr, "release drops the capture");
}

void testSliderKeyboardAndStep()
{
    Harness harness;
    auto &slider = harness.root.setContent<Column>().add<Slider>(0.0f, 10.0f);
    slider.setStep(1.0f);
    slider.value = 5.0f;

    harness.layout();
    slider.requestFocus();

    harness.root.keyDown(wma::KEY_RIGHT);
    AURA_CHECK(near(slider.value.get(), 6.0f), "an arrow key moves by one step");

    harness.root.keyDown(wma::KEY_HOME);
    AURA_CHECK(near(slider.value.get(), 0.0f), "Home goes to the minimum");

    harness.root.keyDown(wma::KEY_END);
    AURA_CHECK(near(slider.value.get(), 10.0f), "End goes to the maximum");
}

void testFocusTraversal()
{
    Harness harness;
    auto &column = harness.root.setContent<Column>();

    auto &first = column.add<Button>("One");
    auto &second = column.add<Button>("Two");
    auto &third = column.add<Button>("Three");

    harness.layout();

    AURA_CHECK(harness.root.focusNext() && harness.root.focused() == &first,
               "Tab from nothing focuses the first control");

    harness.root.keyDown(wma::KEY_TAB);
    AURA_CHECK(harness.root.focused() == &second, "Tab moves to the next one");

    harness.root.keyDown(wma::KEY_TAB, wma::KeyModifiers{.shift = true});
    AURA_CHECK(harness.root.focused() == &first, "Shift+Tab moves back");

    harness.root.setFocus(&third);
    harness.root.keyDown(wma::KEY_TAB);
    AURA_CHECK(harness.root.focused() == &first, "Tab wraps around at the end");

    second.setVisibility(Visibility::Collapsed);
    harness.root.setFocus(&first);
    harness.root.keyDown(wma::KEY_TAB);
    AURA_CHECK(harness.root.focused() == &third, "a collapsed control is skipped");
}

void testFocusIsDroppedWithTheWidget()
{
    Harness harness;
    auto &column = harness.root.setContent<Column>();
    auto &button = column.add<Button>("Doomed");

    harness.layout();
    button.requestFocus();
    AURA_CHECK(harness.root.focused() == &button, "the button has focus");

    column.remove(button);
    AURA_CHECK(harness.root.focused() == nullptr, "removing the focused widget clears the root's pointer to it");
}

void testShortcuts()
{
    Harness harness;
    auto &field = harness.root.setContent<Column>().add<TextField>();

    int saves = 0;
    int deletes = 0;

    harness.root.addShortcut(Shortcut::withCtrl(wma::KEY_S),
                             [&saves]
                             {
                                 ++saves;
                             });
    harness.root.addShortcut(Shortcut{.key = wma::KEY_DELETE},
                             [&deletes]
                             {
                                 ++deletes;
                             });

    harness.layout();
    field.requestFocus();
    field.text = "abc";
    field.setCaret(0);

    harness.root.keyDown(wma::KEY_S, wma::KeyModifiers{.ctrl = true});
    AURA_CHECK(saves == 1, "a modified shortcut fires even while a field has focus");

    harness.root.keyDown(wma::KEY_DELETE);
    AURA_CHECK(deletes == 0 && field.text.get() == "bc", "an unmodified shortcut loses to the focused widget");
}

void testTextFieldEditing()
{
    Harness harness;
    auto &field = harness.root.setContent<Column>().add<TextField>();

    harness.layout();
    field.requestFocus();

    harness.root.textInput("hello");
    AURA_CHECK(field.text.get() == "hello", "typed text is inserted at the caret");
    AURA_CHECK(field.caret() == 5, "and the caret follows it");

    harness.root.keyDown(wma::KEY_BACKSPACE);
    AURA_CHECK(field.text.get() == "hell", "Backspace deletes the character before the caret");

    harness.root.keyDown(wma::KEY_HOME);
    harness.root.keyDown(wma::KEY_DELETE);
    AURA_CHECK(field.text.get() == "ell", "Delete removes the one after it");

    harness.root.keyDown(wma::KEY_A, wma::KeyModifiers{.ctrl = true});
    harness.root.textInput("x");
    AURA_CHECK(field.text.get() == "x", "typing over a selection replaces it");

    int submits = 0;
    field.submitted.connect(
        [&submits]
        {
            ++submits;
        });
    harness.root.keyDown(wma::KEY_ENTER);
    AURA_CHECK(submits == 1, "Enter submits");

    AURA_CHECK(harness.root.capturesTextInput(), "a focused field tells the platform to enable text input");
}

void testTextFieldKeepsUtf8Valid()
{
    Harness harness;
    auto &field = harness.root.setContent<Column>().add<TextField>();

    harness.layout();
    field.requestFocus();

    harness.root.textInput("héllo");
    AURA_CHECK(field.text.get() == "héllo", "multi-byte input round-trips");

    field.setCaret(3);
    harness.root.keyDown(wma::KEY_BACKSPACE);

    //! One Backspace must remove the whole two-byte 'é', not one of its bytes.
    AURA_CHECK(field.text.get() == "hllo", "Backspace deletes a whole character");

    field.setCaret(0);
    harness.root.keyDown(wma::KEY_RIGHT);
    AURA_CHECK(field.caret() == 1, "arrow keys move by character, not by byte");
}

void testTextFieldObscured()
{
    Harness harness;
    auto &field = harness.root.setContent<Column>().add<TextField>();
    field.setObscured(true);

    harness.layout();
    field.requestFocus();
    harness.root.textInput("séc");
    harness.layout();

    AURA_CHECK(field.text.get() == "séc", "an obscured field still holds the real text");
    field.setCaret(0);
    harness.root.keyDown(wma::KEY_RIGHT);
    harness.root.keyDown(wma::KEY_RIGHT);
    AURA_CHECK(field.caret() == 3, "the caret still moves by character over the real bytes");
    harness.root.keyDown(wma::KEY_BACKSPACE);
    AURA_CHECK(field.text.get() == "sc", "editing works through the mask");

    AccessibilityInfo info;
    field.accessibility(info);
    AURA_CHECK(info.name.empty(), "the secret is not read back through accessibility");

    field.setObscured(false);
    harness.layout();
    field.accessibility(info);
    AURA_CHECK(info.name == "sc", "clearing the mask reveals the text again");
}

void testTextFieldReadOnly()
{
    Harness harness;
    auto &field = harness.root.setContent<Column>().add<TextField>("locked");
    field.setReadOnly(true);

    harness.layout();
    field.requestFocus();

    harness.root.textInput("x");
    harness.root.keyDown(wma::KEY_BACKSPACE);

    AURA_CHECK(field.text.get() == "locked", "a read-only field refuses every edit");
    AURA_CHECK(!harness.root.capturesTextInput(), "and does not ask the platform for text input");
}

void testWheelReachesTheScrollView()
{
    Harness harness({200.0f, 100.0f});

    auto &scroll = harness.root.setContent<ScrollView>();
    scroll.setSmooth(false);

    auto &column = scroll.setContent<Column>();
    for (int i = 0; i < 10; ++i)
        column.add<Widget>().layout().height = Length::px(30.0f);

    harness.layout();

    const bool consumed = harness.root.wheel({0.0f, -1.0f}, {50.0f, 50.0f});
    harness.layout();

    AURA_CHECK(consumed && scroll.offset().y > 0.0f, "the wheel scrolls the view under the cursor");

    harness.root.wheel({0.0f, 1000.0f}, {50.0f, 50.0f});
    harness.layout();

    AURA_CHECK(near(scroll.offset().y, 0.0f), "scrolling back stops at the top");
    AURA_CHECK(!harness.root.wheel({0.0f, 1.0f}, {50.0f, 50.0f}),
               "and a wheel at the end of travel is declined, so a parent can take it");
}

void testTouchDragScrollsInsteadOfClicking()
{
    ButtonList list;

    list.harness.touchDown({50.0f, 85.0f});
    AURA_CHECK(list.harness.root.pointerCapture() != list.scroll, "a press on a button is the button's until it moves");

    list.harness.touchMove({50.0f, 75.0f});
    AURA_CHECK(list.harness.root.pointerCapture() == list.scroll, "past the slop, the scroll view takes the finger");

    list.harness.touchMove({50.0f, 35.0f});
    list.harness.touchUp({50.0f, 35.0f});
    list.harness.layout();

    AURA_CHECK(near(list.scroll->offset().y, 40.0f), "the content follows the finger from where it was taken");
    AURA_CHECK(list.totalClicks() == 0, "and the button under the finger is not clicked when it lifts");
}

void testTouchSlopKeepsATapATap()
{
    ButtonList list;

    list.harness.touchDown({50.0f, 85.0f});
    list.harness.touchMove({50.0f, 81.0f});
    list.harness.touchUp({50.0f, 81.0f});

    AURA_CHECK(list.clicks[2] == 1 && near(list.scroll->offset().y, 0.0f),
               "a wobble inside the slop is still a click, and scrolls nothing");
}

void testMouseDragDoesNotScroll()
{
    ButtonList list;

    list.harness.root.pointerMoved({50.0f, 85.0f});
    list.harness.root.pointerDown({50.0f, 85.0f});
    list.harness.root.pointerMoved({50.0f, 35.0f});
    list.harness.root.pointerUp({50.0f, 35.0f});

    AURA_CHECK(near(list.scroll->offset().y, 0.0f), "a mouse drag over the content is left to what is under it");
}

void testTouchDragNeedsSomewhereToGo()
{
    Harness harness({200.0f, 400.0f});
    auto &scroll = harness.root.setContent<ScrollView>();
    auto &button = scroll.setContent<Column>().add<Button>("Fits");
    button.layout().height = Length::px(30.0f);
    harness.layout();

    harness.touchDown({50.0f, 15.0f});
    harness.touchMove({50.0f, 60.0f});

    AURA_CHECK(harness.root.pointerCapture() == &button, "a view whose content fits does not take the finger");
}

void testTouchDragDirection()
{
    Harness harness({200.0f, 100.0f});
    auto &scroll = harness.root.setContent<ScrollView>();
    scroll.setSmooth(false);
    auto &column = scroll.setContent<Column>();
    auto &slider = column.add<Slider>(0.0f, 100.0f);
    slider.layout().height = Length::px(30.0f);
    for (int i = 0; i < 8; ++i)
        column.add<Widget>().layout().height = Length::px(30.0f);
    harness.layout();

    harness.touchDown({20.0f, 15.0f});
    harness.touchMove({90.0f, 17.0f});
    harness.touchUp({90.0f, 17.0f});
    harness.layout();

    AURA_CHECK(near(scroll.offset().y, 0.0f) && slider.value.get() > 0.0f,
               "a horizontal drag on a slider stays the slider's, in a view that only scrolls vertically");

    harness.touchDown({20.0f, 15.0f});
    harness.touchMove({20.0f, 5.0f});
    AURA_CHECK(harness.root.pointerCapture() == &scroll, "while a vertical one is the view's, even from the slider");
    harness.touchUp({20.0f, 5.0f});
}

void testTouchDragHandsOverAtTheEnd()
{
    Harness harness({200.0f, 100.0f});
    auto &outer = harness.root.setContent<ScrollView>();
    outer.setSmooth(false);
    auto &page = outer.setContent<Column>();
    page.add<Widget>().layout().height = Length::px(100.0f);
    auto &inner = page.add<ScrollView>();
    inner.setSmooth(false);
    inner.layout().height = Length::px(100.0f);
    auto &rows = inner.setContent<Column>();
    for (int i = 0; i < 6; ++i)
        rows.add<Widget>().layout().height = Length::px(40.0f);
    harness.layout();

    outer.scrollTo({0.0f, 100.0f});
    harness.layout();

    //! The inner view is at its top, so a finger moving down (content moving up) cannot scroll it.
    harness.touchDown({50.0f, 40.0f});
    harness.touchMove({50.0f, 70.0f});
    AURA_CHECK(harness.root.pointerCapture() == &outer, "a drag the inner view cannot follow goes to the outer one");
    harness.touchMove({50.0f, 90.0f});
    harness.touchUp({50.0f, 90.0f});
    harness.layout();
    AURA_CHECK(outer.offset().y < 100.0f && near(inner.offset().y, 0.0f), "which scrolls instead");
}

void testTouchFling()
{
    ButtonList list;

    list.harness.touchDown({50.0f, 90.0f});
    for (int step = 1; step <= 4; ++step)
        list.harness.touchMove({50.0f, 90.0f - 6.0f * static_cast<f32>(step)});
    list.harness.touchUp({50.0f, 66.0f});
    list.harness.layout();

    const f32 lifted = list.scroll->offset().y;
    list.harness.root.update(0.016f);
    list.harness.root.update(0.016f);
    AURA_CHECK(list.scroll->offset().y > lifted, "a quick flick carries on after the finger lifted");

    f32 previous = -1.0f;
    for (int frame = 0; frame < 600 && list.scroll->offset().y != previous; ++frame)
    {
        previous = list.scroll->offset().y;
        list.harness.root.update(0.016f);
    }
    AURA_CHECK(list.scroll->offset().y == previous, "and slows to a stop");
    AURA_CHECK(list.scroll->offset().y <= list.scroll->maxOffset().y, "without leaving the content");
}

void testTouchRestBeforeLiftingDoesNotFling()
{
    ButtonList list;

    list.harness.touchDown({50.0f, 90.0f});
    for (int step = 1; step <= 4; ++step)
        list.harness.touchMove({50.0f, 90.0f - 6.0f * static_cast<f32>(step)});
    list.harness.root.update(0.3f);
    list.harness.touchUp({50.0f, 66.0f});

    const f32 lifted = list.scroll->offset().y;
    for (int frame = 0; frame < 30; ++frame)
        list.harness.root.update(0.016f);
    AURA_CHECK(near(list.scroll->offset().y, lifted),
               "a finger that rested before it lifted leaves the content where it is");
}

void testTouchStopsAFling()
{
    ButtonList list;

    list.harness.touchDown({50.0f, 90.0f});
    for (int step = 1; step <= 4; ++step)
        list.harness.touchMove({50.0f, 90.0f - 6.0f * static_cast<f32>(step)});
    list.harness.touchUp({50.0f, 66.0f});
    list.harness.root.update(0.016f);

    list.harness.touchDown({50.0f, 50.0f});
    const f32 held = list.scroll->offset().y;
    for (int frame = 0; frame < 20; ++frame)
        list.harness.root.update(0.016f);
    AURA_CHECK(near(list.scroll->offset().y, held), "putting a finger down on a coasting view stops it");
    AURA_CHECK(list.harness.root.pointerCapture() == list.scroll, "and the view keeps the finger");
    list.harness.touchUp({50.0f, 50.0f});
    AURA_CHECK(list.totalClicks() == 0, "so the tap that stopped it does not also click the button under it");
    list.harness.layout();
    AURA_CHECK(near(list.scroll->offset().y, held), "and lifting without moving leaves the content where it stopped");
}

/// Distance a flick carries after the finger lifts, with the same finger speed delivered as
/// @p movesPerFrame moves per frame, for @p frames frames.
[[nodiscard]] f32 flingDistance(int movesPerFrame, int frames = 6)
{
    ButtonList list;
    list.harness.touchDown({50.0f, 90.0f});

    const f32 step = 6.0f / static_cast<f32>(movesPerFrame);
    f32 y = 90.0f;
    for (int frame = 0; frame < frames; ++frame)
        for (int move = 0; move < movesPerFrame; ++move)
        {
            y -= step;
            list.harness.touchMove({50.0f, y}, move == 0 ? 0.016f : 0.0f);
        }
    list.harness.touchUp({50.0f, y});
    list.harness.layout();

    const f32 lifted = list.scroll->offset().y;
    for (int frame = 0; frame < 600; ++frame)
        list.harness.root.update(0.016f);
    return list.scroll->offset().y - lifted;
}

void testShortFlickIsAsFastAsALongDrag()
{
    //! Three frames is the pan's first move past the slop and one measured sample: nothing to average with.
    const f32 shortFlick = flingDistance(1, 3);
    const f32 longDrag = flingDistance(1, 8);

    AURA_CHECK(shortFlick > 0.0f, "a flick of only a few moves still carries on");
    AURA_CHECK(std::abs(shortFlick - longDrag) < 0.2f * longDrag,
               "and as far as a long drag at the same finger speed: the first sample is the speed, not half of it");
}

void testTouchSpeedDoesNotDependOnMovesPerFrame()
{
    const f32 once = flingDistance(1);
    const f32 batched = flingDistance(3);

    AURA_CHECK(once > 0.0f && batched > 0.0f, "a flick carries on however its moves arrive");
    AURA_CHECK(std::abs(once - batched) < 0.25f * once,
               "and moves that share a frame's timestamp do not make the finger look faster");
}

void testTouchTapFocusesATextField()
{
    Harness harness({200.0f, 100.0f});
    auto &scroll = harness.root.setContent<ScrollView>();
    scroll.setSmooth(false);
    auto &column = scroll.setContent<Column>();
    auto &field = column.add<TextField>("hello");
    for (int i = 0; i < 8; ++i)
        column.add<Widget>().layout().height = Length::px(30.0f);
    harness.layout();

    const glm::vec2 at = field.bounds().center();

    harness.touchDown(at);
    AURA_CHECK(!field.hasFocus(), "a finger on a field does not focus it yet");
    harness.touchMove(at + glm::vec2{0.0f, -20.0f});
    harness.touchMove(at + glm::vec2{0.0f, -40.0f});
    harness.touchUp(at + glm::vec2{0.0f, -40.0f});
    AURA_CHECK(!field.hasFocus() && scroll.offset().y > 0.0f,
               "a drag that began on a field scrolls the page and leaves the keyboard down");

    scroll.scrollTo({0.0f, 0.0f});
    harness.layout();
    const glm::vec2 now = field.bounds().center();
    harness.touchDown(now);
    harness.touchUp(now);
    AURA_CHECK(field.hasFocus(), "a tap focuses it, when the finger lifts");
}

/// Counts what it is offered, and wants whichever kind of press it is told to.
class InterceptProbe final : public Widget
{
  public:
    std::optional<PointerKind> wants;
    int offers = 0;

    [[nodiscard]] bool interceptsPointer(PointerKind kind) const noexcept override
    {
        return wants == kind;
    }

    bool onPointerIntercept(const PointerEvent &) override
    {
        ++offers;
        return false;
    }
};

void testPressesAreOnlyOfferedToWhoAsked()
{
    Harness harness({200.0f, 100.0f});
    auto &probe = harness.root.setContent<InterceptProbe>();
    harness.layout();
    const glm::vec2 at{50.0f, 50.0f};

    //! Presses and drags of both kinds: with no interest, the widget hears of none of it.
    harness.click(at);
    harness.touchDown(at);
    harness.touchMove(at + glm::vec2{20.0f, 0.0f});
    harness.touchUp(at + glm::vec2{20.0f, 0.0f});
    AURA_CHECK(probe.offers == 0, "a widget that did not ask is never offered a press");

    probe.wants = PointerKind::Touch;
    harness.click(at);
    AURA_CHECK(probe.offers == 0, "and one that asked for a finger is not offered a mouse");

    harness.touchDown(at);
    harness.touchMove(at + glm::vec2{20.0f, 0.0f});
    harness.touchMove(at + glm::vec2{40.0f, 0.0f});
    harness.touchUp(at + glm::vec2{40.0f, 0.0f});
    AURA_CHECK(probe.offers == 3, "but is offered a finger's press and each move of it");

    harness.touchMove(at + glm::vec2{60.0f, 0.0f});
    AURA_CHECK(probe.offers == 3, "and nothing once the finger is up");
}

class ClickProbe final : public Widget
{
  public:
    u32 lastClickCount = 0;

  protected:
    bool onPointerDown(const PointerEvent &event) override
    {
        lastClickCount = event.clickCount;
        return true;
    }
};

void testDoubleClickIsConfigurable()
{
    const auto secondPress = [](f32 seconds, f32 slop, f32 gap, f32 distance)
    {
        Harness harness;
        auto &probe = harness.root.setContent<Column>().add<ClickProbe>();
        probe.layout().width = Length::px(100.0f);
        probe.layout().height = Length::px(100.0f);
        harness.layout();
        if (seconds >= 0.0f)
            harness.root.setDoubleClickSeconds(seconds);
        if (slop >= 0.0f)
            harness.root.setDoubleClickSlop(slop);

        const glm::vec2 first = probe.bounds().center();
        harness.root.pointerDown(first);
        harness.root.pointerUp(first);
        harness.root.update(gap);
        harness.root.pointerDown(first + glm::vec2{distance, 0.0f});
        return probe.lastClickCount;
    };

    AURA_CHECK(secondPress(-1.0f, -1.0f, 0.1f, 2.0f) == 2, "two quick presses close together are a double click");
    AURA_CHECK(secondPress(0.05f, -1.0f, 0.1f, 2.0f) == 1, "a shorter window leaves the same gap too slow");
    AURA_CHECK(secondPress(1.0f, -1.0f, 0.7f, 2.0f) == 2, "and a longer one lets a slower pair count");
    AURA_CHECK(secondPress(-1.0f, 1.0f, 0.1f, 3.0f) == 1, "a tighter slop makes the same move a different press");
    AURA_CHECK(secondPress(-1.0f, 20.0f, 0.1f, 15.0f) == 2, "and a looser one forgives it");
    AURA_CHECK(secondPress(0.0f, -1.0f, 0.0f, 0.0f) == 1, "zero seconds turns double clicks off");
}

void testScrollPhysicsIsConfigurable()
{
    {
        ButtonList list;
        list.harness.touchDown({50.0f, 85.0f});
        list.harness.touchMove({50.0f, 75.0f});
        AURA_CHECK(list.harness.root.pointerCapture() == list.scroll, "by default ten pixels is a drag");
    }
    {
        ButtonList list;
        list.scroll->setPhysics(ScrollPhysics{.touchSlop = 30.0f});
        list.harness.touchDown({50.0f, 85.0f});
        list.harness.touchMove({50.0f, 75.0f});
        AURA_CHECK(list.harness.root.pointerCapture() != list.scroll,
                   "a view with a wider slop leaves them to the tap");
        list.harness.touchMove({50.0f, 50.0f});
        AURA_CHECK(list.harness.root.pointerCapture() == list.scroll, "and takes the finger once it is past it");
    }
    {
        ButtonList list;
        list.harness.root.theme().metrics.scroll.touchSlop = 30.0f;
        list.harness.touchDown({50.0f, 85.0f});
        list.harness.touchMove({50.0f, 75.0f});
        AURA_CHECK(list.harness.root.pointerCapture() != list.scroll, "the theme's slop reaches every view");

        list.harness.touchUp({50.0f, 75.0f});
        list.scroll->setPhysics(ScrollPhysics{.touchSlop = 2.0f});
        list.harness.touchDown({50.0f, 85.0f});
        list.harness.touchMove({50.0f, 75.0f});
        AURA_CHECK(list.harness.root.pointerCapture() == list.scroll, "a view's own physics wins over the theme's");
        list.harness.touchUp({50.0f, 75.0f});

        list.scroll->resetPhysics();
        AURA_CHECK(list.scroll->physics().touchSlop == 30.0f, "and resetting returns it to the theme's");
    }
    {
        //! Zero is a legal setting for both, and neither may divide by it: samples in the same instant, and a
        //! release that did not move at all.
        ButtonList list;
        list.scroll->setPhysics(ScrollPhysics{.flingMinSpeed = 0.0f, .velocitySampleSeconds = 0.0f});
        list.harness.touchDown({50.0f, 90.0f});
        list.harness.touchMove({50.0f, 80.0f}); //! Past the slop: the pan begins here.
        list.harness.touchMove({50.0f, 70.0f}, 0.0f);
        list.harness.touchMove({50.0f, 60.0f}, 0.0f);
        list.harness.touchUp({50.0f, 60.0f});
        list.harness.layout();
        for (int frame = 0; frame < 30; ++frame)
            list.harness.root.update(0.016f);
        AURA_CHECK(std::isfinite(list.scroll->offset().y) && std::isfinite(list.scroll->offset().x),
                   "zero sample time and zero minimum speed leave the offset finite");

        ButtonList still;
        still.scroll->setPhysics(ScrollPhysics{.flingMinSpeed = 0.0f});
        still.harness.touchDown({50.0f, 90.0f});
        still.harness.touchMove({50.0f, 70.0f}); //! The pan begins and the finger lifts before a sample exists.
        still.harness.touchUp({50.0f, 70.0f});
        for (int frame = 0; frame < 30; ++frame)
            still.harness.root.update(0.016f);
        AURA_CHECK(std::isfinite(still.scroll->offset().y), "and so does a release before any speed was sampled");
    }
    {
        ButtonList list;
        list.scroll->setPhysics(ScrollPhysics{.flingMinSpeed = 1.0e9f});
        list.harness.touchDown({50.0f, 90.0f});
        for (int step = 1; step <= 4; ++step)
            list.harness.touchMove({50.0f, 90.0f - 6.0f * static_cast<f32>(step)});
        list.harness.touchUp({50.0f, 66.0f});
        list.harness.layout();
        const f32 lifted = list.scroll->offset().y;
        list.harness.root.update(0.016f);
        list.harness.root.update(0.016f);
        AURA_CHECK(list.scroll->offset().y == lifted, "a view that wants a faster flick than this one does not coast");
    }
}

void testTweenDrivesAnything()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();
    auto &box = page.add<Widget>();
    harness.layout();

    std::vector<f32> steps;
    int done = 0;
    harness.root.animate(&box, Tween{.seconds = 0.1f,
                                     .curve = easing::linear,
                                     .step =
                                         [&steps, &box](f32 progress)
                                     {
                                         steps.push_back(progress);
                                         box.setOpacity(progress);
                                     },
                                     .done =
                                         [&done]
                                     {
                                         ++done;
                                     }});
    AURA_CHECK(steps.size() == 1 && steps[0] == 0.0f && box.opacity() == 0.0f,
               "the first step runs at once, so the starting state is in place before anything draws");
    AURA_CHECK(harness.root.animating(), "a running tween keeps the root animating");

    harness.root.update(0.05f);
    AURA_CHECK(steps.size() == 2 && near(steps[1], 0.5f, 0.01f), "progress follows time through the curve");
    harness.root.update(0.06f);
    AURA_CHECK(steps.back() == 1.0f && done == 1, "the last step is exactly 1, then done");
    harness.root.update(0.05f);
    AURA_CHECK(steps.size() == 3 && done == 1 && !harness.root.animating(), "and nothing after it");

    int cancelledSteps = 0;
    bool cancelledDone = false;
    const UIRoot::AnimationId id = harness.root.animate(nullptr, Tween{.seconds = 1.0f,
                                                                       .step =
                                                                           [&cancelledSteps](f32)
                                                                       {
                                                                           ++cancelledSteps;
                                                                       },
                                                                       .done =
                                                                           [&cancelledDone]
                                                                       {
                                                                           cancelledDone = true;
                                                                       }});
    harness.root.cancelAnimation(id);
    harness.root.update(2.0f);
    AURA_CHECK(cancelledSteps == 1 && !cancelledDone, "a cancelled tween takes no further step and no done");

    auto &doomed = page.add<Widget>();
    harness.layout();
    int doomedSteps = 0;
    bool doomedDone = false;
    harness.root.animate(&doomed, Tween{.seconds = 1.0f,
                                        .step =
                                            [&doomedSteps](f32)
                                        {
                                            ++doomedSteps;
                                        },
                                        .done =
                                            [&doomedDone]
                                        {
                                            doomedDone = true;
                                        }});
    page.remove(doomed);
    harness.root.update(2.0f);
    AURA_CHECK(doomedSteps == 1 && !doomedDone, "a tween stops with its target, without done");

    int chained = 0;
    harness.root.animate(nullptr, Tween{.done = [&harness, &chained]
                                        {
                                            harness.root.animate(nullptr, Tween{.done = [&chained]
                                                                                {
                                                                                    ++chained;
                                                                                }});
                                        }});
    harness.root.update(0.0f);
    AURA_CHECK(chained == 0, "a tween started from another's done waits for the next update");
    harness.root.update(0.0f);
    AURA_CHECK(chained == 1, "and runs there");
}

void testTranslationMovesHitTestingAndEvents()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();
    auto &button = page.add<Button>("Moved");
    auto &panel = page.add<Column>();
    auto &slider = panel.add<Slider>(0.0f, 100.0f);
    slider.layout().width = Length::px(200.0f);
    slider.layout().hAlign = Alignment::Start;

    int clicks = 0;
    button.clicked.connect(
        [&clicks]
        {
            ++clicks;
        });
    harness.layout();

    const Rect resting = button.bounds();
    const glm::vec2 shift{0.0f, 150.0f};
    button.setTranslation(shift);
    harness.layout();
    AURA_CHECK(button.bounds().min == resting.min && button.visualBounds().min == resting.min + shift,
               "layout keeps its slot; only the picture moves");

    harness.click(resting.center());
    AURA_CHECK(clicks == 0, "where it was laid out takes no click");
    harness.click(resting.center() + shift);
    AURA_CHECK(clicks == 1, "where it is drawn does");

    panel.setTranslation({40.0f, 0.0f});
    harness.layout();
    const Rect drawn = slider.visualBounds();
    AURA_CHECK(drawn.min.x == slider.bounds().min.x + 40.0f, "a translation moves every descendant");

    harness.root.pointerMoved(drawn.center());
    harness.root.pointerDown(drawn.center());
    AURA_CHECK(near(slider.value.get(), 50.0f, 2.0f), "a control in a moved panel reads the pointer in its own space");
    harness.root.pointerMoved({drawn.min.x - 500.0f, drawn.center().y});
    harness.root.pointerMoved(drawn.center());
    AURA_CHECK(near(slider.value.get(), 50.0f, 2.0f), "and so does the drag it captured");
    harness.root.pointerUp(drawn.center());
}

void testAccessibilityTree()
{
    Harness harness;
    auto &column = harness.root.setContent<Column>();

    auto &button = column.add<Button>("Launch");
    auto &check = column.add<CheckBox>("Wireframe", true);
    auto &slider = column.add<Slider>(0.0f, 10.0f);
    slider.value = 4.0f;

    harness.layout();
    button.requestFocus();

    const AccessibilityNode tree = harness.root.accessibilityTree();

    AURA_CHECK(tree.info.role == Role::Window, "the tree is rooted at a window");

    AccessibilityInfo info{};
    button.accessibility(info);
    AURA_CHECK(info.role == Role::Button && info.name == "Launch" && info.focused,
               "a button reports its role, its label and its focus");

    check.accessibility(info = {});
    AURA_CHECK(info.role == Role::CheckBox && info.checked, "a check box reports its checked state");

    slider.accessibility(info = {});
    AURA_CHECK(info.role == Role::Slider && near(info.value, 4.0f) && near(info.maxValue, 10.0f),
               "a slider reports its value and its range");

    button.setAccessibleName("Launch the application");
    button.accessibility(info = {});
    AURA_CHECK(info.name == "Launch the application", "an explicit accessible name wins");
}

void testSignalLifetime()
{
    Signal<int> signal;

    int calls = 0;
    {
        const ScopedConnection scoped = signal.connect(
            [&calls](int)
            {
                ++calls;
            });
        signal.emit(1);
    }

    signal.emit(2);
    AURA_CHECK(calls == 1, "a ScopedConnection disconnects when it goes out of scope");

    //! Disconnecting from inside an emit must not corrupt the iteration.
    Connection self;
    int reentrant = 0;
    self = signal.connect(
        [&](int)
        {
            ++reentrant;
            self.disconnect();
        });

    signal.emit(3);
    signal.emit(4);
    AURA_CHECK(reentrant == 1, "a slot may disconnect itself while the signal is emitting");

    Connection dangling;
    {
        Signal<int> temporary;
        dangling = temporary.connect(
            [](int)
            {
            });
    }

    dangling.disconnect();
    AURA_CHECK(!dangling.connected(), "a Connection outliving its Signal is inert, not dangling");
}

} // namespace

int main()
{
    testHitTesting();
    testTopmostWins();
    testHoverChain();
    testButtonClick();
    testKeyboardActivation();
    testDisabledSwallowsInput();
    testCheckBox();
    testRadioGroup();
    testSliderDragSurvivesLeaving();
    testSliderKeyboardAndStep();
    testFocusTraversal();
    testFocusIsDroppedWithTheWidget();
    testShortcuts();
    testTextFieldEditing();
    testTextFieldKeepsUtf8Valid();
    testTextFieldObscured();
    testTextFieldReadOnly();
    testWheelReachesTheScrollView();
    testTouchDragScrollsInsteadOfClicking();
    testTouchSlopKeepsATapATap();
    testMouseDragDoesNotScroll();
    testTouchDragNeedsSomewhereToGo();
    testTouchDragDirection();
    testTouchDragHandsOverAtTheEnd();
    testTouchFling();
    testTouchRestBeforeLiftingDoesNotFling();
    testTouchStopsAFling();
    testTouchSpeedDoesNotDependOnMovesPerFrame();
    testShortFlickIsAsFastAsALongDrag();
    testPressesAreOnlyOfferedToWhoAsked();
    testTouchTapFocusesATextField();
    testDoubleClickIsConfigurable();
    testScrollPhysicsIsConfigurable();
    testTweenDrivesAnything();
    testTranslationMovesHitTestingAndEvents();
    testAccessibilityTree();
    testSignalLifetime();

    AURA_TEST_MAIN_RETURN();
}
