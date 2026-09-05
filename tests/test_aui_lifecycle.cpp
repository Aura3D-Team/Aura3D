#include "aura/UI/UI.hpp"
#include "TestUtils.h"

using namespace aura3d::ui;

namespace {
struct Harness {
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};
    Harness() { root.resize({400.0f, 300.0f}); }
    void frame() { root.update(0.2f); }
    void click(glm::vec2 point) { root.pointerDown(point); root.pointerUp(point); }
};

void hiddenSubtree()
{
    Harness h;
    auto& page = h.root.setContent<Column>();
    auto& button = page.add<Button>("Hide");
    int clicks = 0;
    button.clicked.connect([&] { ++clicks; });
    h.frame();
    h.root.pointerDown(button.bounds().center());
    page.setVisibility(Visibility::Hidden);
    AURA_CHECK(!h.root.focused() && !h.root.pointerCapture(), "hiding an ancestor releases focus and capture immediately");
    AURA_CHECK(!button.focusable(), "a descendant of a hidden ancestor cannot acquire focus");
    h.root.keyDown(wma::KEY_SPACE);
    AURA_CHECK(clicks == 0, "hidden controls receive no keyboard activation");
    page.setVisibility(Visibility::Visible);
    h.root.pointerUp(button.bounds().center());
    AURA_CHECK(clicks == 0, "showing a hidden pressed control does not resurrect its click");
}

void cancelledPress()
{
    Harness h;
    auto& button = h.root.setContent<Column>().add<Button>("Cancel");
    int clicks = 0;
    button.clicked.connect([&] { ++clicks; });
    h.frame();
    const auto point = button.bounds().center();
    h.root.pointerDown(point);
    button.setEnabled(false);
    button.setEnabled(true);
    h.root.pointerUp(point);
    AURA_CHECK(clicks == 0, "disable and re-enable cancels an active press");
    h.root.pointerDown(point);
    h.root.capturePointer(nullptr);
    h.root.pointerUp(point);
    AURA_CHECK(clicks == 0, "losing pointer capture cancels an active press");
}

void matchingRelease()
{
    Harness h;
    auto& button = h.root.setContent<Button>("Button");
    int clicks = 0;
    button.clicked.connect([&] { ++clicks; });
    h.frame();
    const auto point = button.bounds().center();
    h.root.pointerDown(point);
    h.root.pointerUp(point, PointerButton::Right);
    AURA_CHECK(clicks == 0 && h.root.pointerCapture() == &button, "right release cannot finish a left press");
    h.root.pointerUp(point);
    AURA_CHECK(clicks == 1, "the matching release activates once");
}

void foreignFocus()
{
    Harness a, b;
    auto& foreign = b.root.setContent<Button>("Foreign");
    a.root.setFocus(&foreign);
    a.root.capturePointer(&foreign);
    AURA_CHECK(!a.root.focused() && !a.root.pointerCapture(), "a root rejects widgets owned by another root");
    a.root.clearFocus();
    a.root.capturePointer(nullptr);
}

void modalKeyboardAndPointer()
{
    Harness h;
    auto& behind = h.root.setContent<Button>("Behind");
    int clicks = 0;
    behind.clicked.connect([&] { ++clicks; });
    h.frame();
    behind.requestFocus();
    auto& lower = h.root.overlay().open<Button>({.anchor = {{0, 0}, {0, 0}}}, "Lower popup");
    int lowerClicks = 0;
    lower.clicked.connect([&] { ++lowerClicks; });
    auto& modal = h.root.overlay().open<Column>({.placement = Placement::Center, .modal = true, .dismissOnOutsideClick = false});
    modal.add<Button>("Inside");
    h.frame();
    h.root.keyDown(wma::KEY_SPACE);
    AURA_CHECK(clicks == 0, "opening a modal prevents keyboard activation behind it");
    h.click(lower.bounds().center());
    AURA_CHECK(lowerClicks == 0, "a modal blocks earlier overlays as well as content");
}

void dismissalConsumesClick()
{
    Harness h;
    auto& button = h.root.setContent<Button>("Behind");
    int clicks = 0;
    button.clicked.connect([&] { ++clicks; });
    auto& popup = h.root.overlay().open<Column>({.placement = Placement::Center});
    popup.layout().minWidth = 60;
    popup.layout().minHeight = 40;
    h.frame();
    h.click({10, 10});
    AURA_CHECK(clicks == 0, "outside dismissal consumes the click before it reaches content");
}

void tooltipAllowsTab()
{
    Harness h;
    auto& page = h.root.setContent<Column>();
    auto& first = page.add<Button>("First");
    auto& second = page.add<Button>("Second");
    first.setTooltip("Hint");
    h.root.setTooltipDelay(0.01f);
    h.frame();
    first.requestFocus();
    h.root.pointerMoved(first.bounds().center());
    h.frame();
    h.root.keyDown(wma::KEY_TAB);
    AURA_CHECK(h.root.focused() == &second, "a tooltip does not trap keyboard traversal");
}

void disclosureRelease()
{
    Harness h;
    auto& section = h.root.setContent<Column>().add<CollapsingHeader>("Section");
    section.content().add<Button>("Child");
    h.frame();
    h.root.pointerDown(section.bounds().min + glm::vec2{5, 5});
    h.root.pointerUp(section.content().bounds().center());
    AURA_CHECK(section.expanded.get(), "a header drag released over its content cancels activation");
}

void removedPopupOwner()
{
    Harness h;
    auto& page = h.root.setContent<Column>();
    auto& dropdown = page.add<Dropdown>(std::vector<std::string>{"One", "Two"});
    h.frame();
    h.click(dropdown.bounds().center());
    h.frame();
    page.remove(dropdown);
    h.frame();
    AURA_CHECK(h.root.overlay().empty(), "removing an open dropdown closes its popup without a stale callback");
}

void callbacksRemoveWidgets()
{
    Harness h;
    auto& page = h.root.setContent<Column>();
    auto& node = page.add<TreeNode>("Remove me");
    node.activated.connect([&] { page.remove(node); });
    h.frame();
    h.click(node.bounds().min + glm::vec2{5, 5});
    AURA_CHECK(page.childCount() == 0, "a tree activation can remove its own widget");

    struct Tick : Widget {
        std::function<void()> action;
        void start() { setAnimating(true); }
        bool onTick(f32) override { action(); return false; }
    };
    auto& first = page.add<Tick>();
    auto& second = page.add<Tick>();
    first.action = [&] { page.remove(second); };
    second.action = [] { AURA_CHECK(false, "a removed animation must never tick"); };
    first.start(); second.start();
    h.frame();
    AURA_CHECK(page.childCount() == 1, "a tick can remove a later widget in the animation snapshot");
}

void bindingAndRadioLifetime()
{
    Property<int> source{1};
    ScopedConnection binding;
    {
        Property<int> target;
        binding = target.bind(source);
    }
    source = 2;
    AURA_CHECK(source.get() == 2, "a binding becomes inert when its target dies");

    Harness h;
    RadioGroup group;
    auto& page = h.root.setContent<Column>();
    auto& a = page.add<RadioButton>("A");
    auto& b = page.add<RadioButton>("B");
    group.add(a); group.add(b); group.select(0);
    page.remove(a);
    AURA_CHECK(group.selected() == -1, "a removed selected radio leaves no dangling selection");
    group.select(1);
    AURA_CHECK(b.checked.get(), "a radio group remains usable after a member is removed");
}

void scrollAxesAndCancellation()
{
    Harness h;
    auto& scroll = h.root.setContent<ScrollView>();
    scroll.setScrollable(true, true);
    scroll.setSmooth(false);
    auto& content = scroll.setContent<Widget>();
    content.layout().width = Length::px(1000);
    content.layout().height = Length::px(1000);
    h.frame();
    AURA_CHECK(h.root.wheel({-1, 0}, {30, 30}) && scroll.offset().x > 0,
               "horizontal wheel deltas scroll horizontally");
    scroll.setScrollable(false, true);
    h.frame();
    AURA_CHECK(scroll.offset().x == 0, "disabling an axis clamps its offset to zero");
    scroll.scrollTo({0, 100});
    scroll.clearChildren();
    h.frame();
    AURA_CHECK(scroll.offset() == glm::vec2(0), "removing scroll content resets the offset");
}

void nativeFocusLoss()
{
    Harness h;
    auto& slider = h.root.setContent<Slider>(0, 100);
    h.frame();
    h.root.pointerDown({100, 30});
    const float value = slider.value.get();
    h.root.cancelInput();
    h.root.pointerMoved({200, 30});
    AURA_CHECK(!h.root.pointerCapture() && !h.root.focused() && slider.value.get() == value,
               "window focus loss cancels drag and keyboard focus");
}
}

int main()
{
    hiddenSubtree();
    cancelledPress();
    matchingRelease();
    foreignFocus();
    modalKeyboardAndPointer();
    dismissalConsumesClick();
    tooltipAllowsTab();
    disclosureRelease();
    removedPopupOwner();
    callbacksRemoveWidgets();
    bindingAndRadioLifetime();
    scrollAxesAndCancellation();
    nativeFocusLoss();
    AURA_TEST_MAIN_RETURN();
}
