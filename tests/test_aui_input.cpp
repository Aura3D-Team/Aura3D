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

#include <cmath>
#include <string>
#include <vector>

#include "aura/UI/UI.hpp"

#include "TestUtils.h"

using namespace aura3d;
using namespace aura3d::ui;

namespace {

[[nodiscard]] bool near(f32 a, f32 b, f32 tolerance = 0.51f)
{
    return std::fabs(a - b) <= tolerance;
}

struct Harness {
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};

    explicit Harness(glm::vec2 size = {400.0f, 300.0f}) { root.resize(size); }

    void layout() { root.update(0.0f); }

    /// A press and a release at the same point: one complete click.
    void click(glm::vec2 at)
    {
        root.pointerMoved(at);
        root.pointerDown(at);
        root.pointerUp(at);
    }
};

void testHitTesting()
{
    Harness harness;
    auto& column = harness.root.setContent<Column>();

    auto& first = column.add<Widget>();
    first.layout().height = Length::px(50.0f);

    auto& second = column.add<Widget>();
    second.layout().height = Length::px(50.0f);

    harness.layout();

    AURA_CHECK(harness.root.widgetAt({10.0f, 10.0f}) == &first, "a point picks the widget under it");
    AURA_CHECK(harness.root.widgetAt({10.0f, 60.0f}) == &second, "and the next one below");
    AURA_CHECK(harness.root.widgetAt({10.0f, 250.0f}) == &column,
               "a point in no child falls through to the container");

    first.setHitTestVisible(false);
    AURA_CHECK(harness.root.widgetAt({10.0f, 10.0f}) == &column,
               "a widget that is not hit-test visible lets the pointer through");
}

void testTopmostWins()
{
    Harness harness;
    auto& stack = harness.root.setContent<Stack>();

    auto& under = stack.add<Widget>();
    auto& over = stack.add<Widget>();

    harness.layout();

    AURA_CHECK(harness.root.widgetAt({10.0f, 10.0f}) == &over,
               "overlaid children are hit in paint order, topmost first");
    AURA_CHECK(&under != harness.root.widgetAt({10.0f, 10.0f}), "so the one underneath is not hit");
}

void testHoverChain()
{
    Harness harness;
    auto& card = harness.root.setContent<Column>();
    auto& button = card.add<Button>("Hover me");

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
    auto& column = harness.root.setContent<Column>();
    auto& button = column.add<Button>("Launch");

    int clicks = 0;
    button.clicked.connect([&clicks] { ++clicks; });

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
    auto& button = harness.root.setContent<Column>().add<Button>("Go");

    int clicks = 0;
    button.clicked.connect([&clicks] { ++clicks; });

    harness.layout();
    button.requestFocus();

    harness.root.keyDown(wma::KEY_SPACE);
    harness.root.keyDown(wma::KEY_ENTER);

    AURA_CHECK(clicks == 2, "Space and Enter activate the focused button");
}

void testDisabledSwallowsInput()
{
    Harness harness;
    auto& panel = harness.root.setContent<Column>();
    auto& button = panel.add<Button>("Disabled");

    int clicks = 0;
    int panelPresses = 0;

    button.clicked.connect([&clicks] { ++clicks; });

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
    auto& check = harness.root.setContent<Column>().add<CheckBox>("Wireframe");

    int changes = 0;
    check.checked.changed().connect([&changes](bool) { ++changes; });

    harness.layout();
    harness.click(check.bounds().center());

    AURA_CHECK(check.checked.get() && changes == 1, "clicking a check box toggles it once");

    harness.click(check.bounds().center());
    AURA_CHECK(!check.checked.get() && changes == 2, "and clicking again toggles it back");
}

void testRadioGroup()
{
    Harness harness;
    auto& column = harness.root.setContent<Column>();

    RadioGroup group;
    auto& low = column.add<RadioButton>("Low");
    auto& high = column.add<RadioButton>("High");

    group.add(low);
    group.add(high);

    int selections = 0;
    group.selectionChanged.connect([&selections](int) { ++selections; });

    harness.layout();
    harness.click(high.bounds().center());

    AURA_CHECK(high.checked.get() && !low.checked.get(), "selecting one clears the others");
    AURA_CHECK(group.selected() == 1 && selections == 1, "and reports the new selection once");

    harness.click(high.bounds().center());
    AURA_CHECK(high.checked.get() && selections == 1,
               "clicking the selected radio again is not a change");
}

void testSliderDragSurvivesLeaving()
{
    Harness harness;
    auto& slider = harness.root.setContent<Column>().add<Slider>(0.0f, 100.0f);
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
    auto& slider = harness.root.setContent<Column>().add<Slider>(0.0f, 10.0f);
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
    auto& column = harness.root.setContent<Column>();

    auto& first = column.add<Button>("One");
    auto& second = column.add<Button>("Two");
    auto& third = column.add<Button>("Three");

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
    auto& column = harness.root.setContent<Column>();
    auto& button = column.add<Button>("Doomed");

    harness.layout();
    button.requestFocus();
    AURA_CHECK(harness.root.focused() == &button, "the button has focus");

    column.remove(button);
    AURA_CHECK(harness.root.focused() == nullptr,
               "removing the focused widget clears the root's pointer to it");
}

void testShortcuts()
{
    Harness harness;
    auto& field = harness.root.setContent<Column>().add<TextField>();

    int saves = 0;
    int deletes = 0;

    harness.root.addShortcut(Shortcut::withCtrl(wma::KEY_S), [&saves] { ++saves; });
    harness.root.addShortcut(Shortcut{.key = wma::KEY_DELETE}, [&deletes] { ++deletes; });

    harness.layout();
    field.requestFocus();
    field.text = "abc";
    field.setCaret(0);

    harness.root.keyDown(wma::KEY_S, wma::KeyModifiers{.ctrl = true});
    AURA_CHECK(saves == 1, "a modified shortcut fires even while a field has focus");

    harness.root.keyDown(wma::KEY_DELETE);
    AURA_CHECK(deletes == 0 && field.text.get() == "bc",
               "an unmodified shortcut loses to the focused widget");
}

void testTextFieldEditing()
{
    Harness harness;
    auto& field = harness.root.setContent<Column>().add<TextField>();

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
    field.submitted.connect([&submits] { ++submits; });
    harness.root.keyDown(wma::KEY_ENTER);
    AURA_CHECK(submits == 1, "Enter submits");

    AURA_CHECK(harness.root.capturesTextInput(),
               "a focused field tells the platform to enable text input");
}

void testTextFieldKeepsUtf8Valid()
{
    Harness harness;
    auto& field = harness.root.setContent<Column>().add<TextField>();

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

void testTextFieldReadOnly()
{
    Harness harness;
    auto& field = harness.root.setContent<Column>().add<TextField>("locked");
    field.setReadOnly(true);

    harness.layout();
    field.requestFocus();

    harness.root.textInput("x");
    harness.root.keyDown(wma::KEY_BACKSPACE);

    AURA_CHECK(field.text.get() == "locked", "a read-only field refuses every edit");
    AURA_CHECK(!harness.root.capturesTextInput(),
               "and does not ask the platform for text input");
}

void testWheelReachesTheScrollView()
{
    Harness harness({200.0f, 100.0f});

    auto& scroll = harness.root.setContent<ScrollView>();
    scroll.setSmooth(false);

    auto& column = scroll.setContent<Column>();
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

void testAccessibilityTree()
{
    Harness harness;
    auto& column = harness.root.setContent<Column>();

    auto& button = column.add<Button>("Launch");
    auto& check = column.add<CheckBox>("Wireframe", true);
    auto& slider = column.add<Slider>(0.0f, 10.0f);
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
    AURA_CHECK(info.role == Role::CheckBox && info.checked,
               "a check box reports its checked state");

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
        const ScopedConnection scoped = signal.connect([&calls](int) { ++calls; });
        signal.emit(1);
    }

    signal.emit(2);
    AURA_CHECK(calls == 1, "a ScopedConnection disconnects when it goes out of scope");

    //! Disconnecting from inside an emit must not corrupt the iteration.
    Connection self;
    int reentrant = 0;
    self = signal.connect([&](int) {
        ++reentrant;
        self.disconnect();
    });

    signal.emit(3);
    signal.emit(4);
    AURA_CHECK(reentrant == 1, "a slot may disconnect itself while the signal is emitting");

    Connection dangling;
    {
        Signal<int> temporary;
        dangling = temporary.connect([](int) {});
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
    testTextFieldReadOnly();
    testWheelReachesTheScrollView();
    testAccessibilityTree();
    testSignalLifetime();

    AURA_TEST_MAIN_RETURN();
}
