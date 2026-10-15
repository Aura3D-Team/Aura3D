/*
 * The floating layer, and the widgets that live in it.
 *
 * Overlays are where a UI toolkit's invariants go wrong quietly. A menu that
 * closes while the click that closed it is still unwinding is a use-after-free
 * that will not reproduce on demand; a drop-down that opens off the bottom of
 * the screen looks like a rendering bug; a dialog that lets Tab wander out of
 * itself is unreachable with the pointer that is blocked by its own modality.
 *
 * All of it is a pure function of the tree, the surface and the input, so a
 * UIRoot over the bitmap font tests every bit of it with no window and no GPU.
 */

#include <algorithm>
#include <cmath>
#include <functional>
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

    void frame(f32 deltaSeconds = 0.0f)
    {
        root.update(deltaSeconds);
    }

    void click(glm::vec2 at)
    {
        root.pointerMoved(at);
        root.pointerDown(at);
        root.pointerUp(at);
    }
};

// -----------------------------------------------------------------------------
// The layer itself
// -----------------------------------------------------------------------------

void testOverlayDrawsOverAndHitsFirst()
{
    Harness harness;

    auto &page = harness.root.setContent<Column>();
    auto &under = page.add<Button>("Under");

    harness.frame();

    auto &floating =
        harness.root.overlay().open<Column>(OverlayDesc{.anchor = under.bounds(), .placement = Placement::Over});
    floating.layout().minWidth = 80.0f;
    floating.layout().minHeight = 40.0f;
    floating.style().fill(glm::vec4{1.0f});

    harness.frame();

    //! Over the button, and squarely inside the overlay: the point belongs to
    //! both, and the overlay is the one that must answer for it.
    const glm::vec2 shared = floating.bounds().center();

    AURA_CHECK(under.bounds().contains(shared), "the overlay sits over the button");
    AURA_CHECK(harness.root.widgetAt(shared) == &floating, "an overlay is hit before the content it covers");

    DrawList list;
    harness.root.paint(list);

    //! Recorded after the content, which is what puts it on top: no depth
    //! test, so order is the only thing deciding.
    AURA_CHECK(!list.empty() && list.commands().back().bounds.min == floating.bounds().min,
               "the overlay is the last thing recorded");
}

void testPlacementFlipsAndClamps()
{
    Harness harness({400.0f, 300.0f});
    harness.root.setContent<Column>();

    //! Anchored hard against the bottom edge: Below has nowhere to go, so it
    //! must flip above rather than open off-screen.
    const Rect anchor{{20.0f, 280.0f}, {120.0f, 296.0f}};

    auto &popup = harness.root.overlay().open<Column>(OverlayDesc{.anchor = anchor, .placement = Placement::Below});
    popup.layout().minWidth = 100.0f;
    popup.layout().minHeight = 80.0f;

    harness.frame();

    AURA_CHECK(popup.bounds().max.y <= 300.0f + 0.51f, "a flipped popup stays on screen");
    AURA_CHECK(popup.bounds().max.y <= anchor.min.y + 0.51f, "and opens above the anchor rather than over it");

    //! Wider than the surface allows at that x: clamped, not pushed off.
    auto &wide = harness.root.overlay().open<Column>(
        OverlayDesc{.anchor = Rect{{380.0f, 10.0f}, {390.0f, 26.0f}}, .placement = Placement::Below});
    wide.layout().minWidth = 150.0f;
    wide.layout().minHeight = 20.0f;

    harness.frame();

    AURA_CHECK(wide.bounds().max.x <= 400.0f + 0.51f, "a popup is clamped into the surface");
    AURA_CHECK(wide.bounds().min.x >= 0.0f, "on both sides");
}

void testOutsideClickAndEscapeDismiss()
{
    Harness harness;
    harness.root.setContent<Column>();

    auto &popup = harness.root.overlay().open<Column>(OverlayDesc{.anchor = Rect{{10.0f, 10.0f}, {60.0f, 30.0f}}});
    popup.layout().minWidth = 60.0f;
    popup.layout().minHeight = 40.0f;

    const OverlayLayer::Id id = harness.root.overlay().lastId();
    harness.frame();

    AURA_CHECK(harness.root.overlay().isOpen(id), "the popup is open");

    harness.root.pointerDown(popup.bounds().center());
    harness.frame();
    AURA_CHECK(harness.root.overlay().isOpen(id), "a click inside leaves it open");

    harness.root.pointerDown({350.0f, 280.0f});
    harness.frame();
    AURA_CHECK(!harness.root.overlay().isOpen(id), "a click outside dismisses it");

    harness.root.overlay().open<Column>(OverlayDesc{});
    const OverlayLayer::Id second = harness.root.overlay().lastId();
    harness.frame();

    AURA_CHECK(harness.root.keyDown(wma::KEY_ESCAPE), "Escape is consumed by the overlay");
    harness.frame();
    AURA_CHECK(!harness.root.overlay().isOpen(second), "and closes the topmost one");
}

void testStickyOverlayIgnoresOutsideClick()
{
    Harness harness;
    harness.root.setContent<Column>();

    harness.root.overlay().open<Column>(OverlayDesc{.dismissOnOutsideClick = false});
    const OverlayLayer::Id id = harness.root.overlay().lastId();
    harness.frame();

    harness.root.pointerDown({350.0f, 280.0f});
    harness.frame();

    AURA_CHECK(harness.root.overlay().isOpen(id),
               "an overlay that opts out of light dismissal survives a click outside");
}

void testModalBlocksTheContentBeneath()
{
    Harness harness;

    auto &page = harness.root.setContent<Column>();
    auto &button = page.add<Button>("Behind");

    int clicks = 0;
    button.clicked.connect(
        [&clicks]
        {
            ++clicks;
        });

    harness.frame();

    auto &dialog = harness.root.overlay().open<Column>(OverlayDesc{.placement = Placement::Center, .modal = true});
    dialog.layout().minWidth = 100.0f;
    dialog.layout().minHeight = 60.0f;

    harness.frame();

    AURA_CHECK(harness.root.overlay().hasModal(), "the layer reports a modal is up");

    //! Well away from the dialog: the click must not reach the button, and
    //! must not fall through to the application either.
    const bool consumed = harness.root.pointerDown({10.0f, 290.0f});
    harness.root.pointerUp({10.0f, 290.0f});

    AURA_CHECK(clicks == 0, "a modal swallows clicks meant for the content behind it");
    AURA_CHECK(consumed, "and consumes them rather than letting them through");
}

void testCloseIsDeferredSoAnItemCanCloseItsOwnMenu()
{
    Harness harness;
    harness.root.setContent<Column>();

    auto &menu = harness.root.overlay().open<Menu>(OverlayDesc{});
    const OverlayLayer::Id id = harness.root.overlay().lastId();

    int ran = 0;
    menu.addItem("Rename").activated.connect(
        [&ran]
        {
            ++ran;
        });

    harness.frame();

    /*
     * The row destroys the menu it belongs to, from inside its own click. If
     * closing were immediate this would be a use-after-free the moment the
     * handler returned into Control::onPointerUp.
     */
    harness.click(menu.childAt(0).bounds().center());

    AURA_CHECK(ran == 1, "the command ran");
    AURA_CHECK(!harness.root.overlay().empty(), "and the menu still exists at the end of the dispatch that closed it");

    harness.frame();
    AURA_CHECK(!harness.root.overlay().isOpen(id), "the menu is gone by the next frame");
    AURA_CHECK(harness.root.overlay().empty(), "and the layer is empty");
}

void testOnClosedFires()
{
    Harness harness;
    harness.root.setContent<Column>();

    int closed = 0;
    harness.root.overlay().open<Column>(OverlayDesc{.onClosed = [&closed]
                                                    {
                                                        ++closed;
                                                    }});

    const OverlayLayer::Id id = harness.root.overlay().lastId();
    harness.frame();

    harness.root.overlay().close(id);
    AURA_CHECK(closed == 0, "onClosed does not run during the close call itself");

    harness.frame();
    AURA_CHECK(closed == 1, "it runs once the overlay is actually gone");
}

void testExitTransitionKeepsItUntilDone()
{
    Harness harness;
    harness.root.setContent<Column>();

    int closed = 0;
    std::function<void()> finish;
    auto &box = harness.root.overlay().open<Column>(
        OverlayDesc{.anchor = Rect::fromSize({20.0f, 20.0f}, {40.0f, 20.0f}),
                    .transition = {.exit =
                                       [&finish](Widget &, const OverlayDesc &, std::function<void()> done)
                                   {
                                       finish = std::move(done);
                                   }},
                    .onClosed = [&closed]
                    {
                        ++closed;
                    }});
    const OverlayLayer::Id id = harness.root.overlay().lastId();
    box.layout().minWidth = 80.0f;
    box.layout().minHeight = 40.0f;
    harness.frame();

    harness.root.overlay().close(id);
    AURA_CHECK(!harness.root.overlay().isOpen(id) && finish, "closing takes it out of play and hands the exit a done");
    AURA_CHECK(box.visibility() == Visibility::Visible && !box.hitTestVisible(), "it stays on screen, inert");
    AURA_CHECK(harness.root.overlay().widgetAt(box.bounds().center()) == nullptr, "and the pointer passes through it");

    for (int i = 0; i < 10; ++i)
        harness.frame(0.1f);
    AURA_CHECK(closed == 0 && !harness.root.overlay().empty(), "nothing goes until done is called, however long");

    finish();
    AURA_CHECK(box.visibility() == Visibility::Collapsed, "done takes it off screen at once");
    harness.frame();
    AURA_CHECK(closed == 1 && harness.root.overlay().empty(), "and the next frame destroys it and runs onClosed");

    finish();
    harness.frame();
    AURA_CHECK(closed == 1, "a done called again, after the overlay is gone, does nothing");
}

void testClosingReleasesFocusWhileTheExitRuns()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();
    auto &below = page.add<Button>("Below");
    harness.frame();
    harness.root.setFocus(&below);

    auto &box = harness.root.overlay().open<Column>(
        OverlayDesc{.transition = {.exit = [](Widget &, const OverlayDesc &, const std::function<void()> &)
                                   {
                                   }}});
    const OverlayLayer::Id id = harness.root.overlay().lastId();
    auto &field = box.add<TextField>();
    harness.frame();
    harness.root.setFocus(&field);
    AURA_CHECK(harness.root.focused() == &field, "the field in the overlay has focus");

    harness.root.overlay().close(id);
    AURA_CHECK(harness.root.focused() == &below, "focus goes back at once, not when the exit ends");
}

void testDefaultTransitionReachesBuiltInOverlays()
{
    Harness harness;
    std::vector<Placement> entered;
    bool placedWhenEntered = true;
    int defaultExits = 0;
    harness.root.overlay().setDefaultTransition(
        {.enter =
             [&entered, &placedWhenEntered](Widget &overlay, const OverlayDesc &desc)
         {
             placedWhenEntered = placedWhenEntered && !overlay.bounds().empty();
             entered.push_back(desc.placement);
             overlay.setOpacity(0.0f);
         },
         .exit =
             [&defaultExits](Widget &, const OverlayDesc &, const std::function<void()> &done)
         {
             ++defaultExits;
             done();
         }});

    auto &page = harness.root.setContent<Column>();
    auto &dropdown = page.add<Dropdown>(std::vector<std::string>{"Vulkan", "OpenGL"});
    harness.frame();

    harness.click(dropdown.bounds().center());
    harness.frame();
    AURA_CHECK(entered.size() == 1 && entered[0] == Placement::Below,
               "the list a drop-down opens goes through the application's enter");
    AURA_CHECK(harness.root.overlay().topmost()->opacity() == 0.0f, "which set its starting state");
    AURA_CHECK(placedWhenEntered, "after it was placed, so its bounds are final");

    dropdown.close();
    harness.frame();
    AURA_CHECK(defaultExits == 1 && harness.root.overlay().empty(), "and its exit, done at once, closes it as before");

    //! Opened and closed before it was ever laid out: nobody saw it, so there is nothing to fade out.
    entered.clear();
    harness.root.overlay().open<Column>(OverlayDesc{});
    harness.root.overlay().close(harness.root.overlay().lastId());
    harness.frame();
    AURA_CHECK(defaultExits == 1 && entered.empty() && harness.root.overlay().empty(),
               "an overlay closed before it was placed skips both hooks");

    int ownExits = 0;
    harness.root.overlay().open<Column>(
        OverlayDesc{.transition = {.exit = [&ownExits](Widget &, const OverlayDesc &, const std::function<void()> &done)
                                   {
                                       ++ownExits;
                                       done();
                                   }}});
    harness.frame();
    harness.root.overlay().close(harness.root.overlay().lastId());
    AURA_CHECK(ownExits == 1 && defaultExits == 1 && entered.size() == 1,
               "an overlay's own hook wins over the default; the one it leaves unset still falls back");
}

/// Every node's name, depth first.
void namesIn(const AccessibilityNode &node, std::vector<std::string> &out)
{
    if (!node.info.name.empty())
        out.push_back(node.info.name);
    for (const AccessibilityNode &child : node.children)
        namesIn(child, out);
}

[[nodiscard]] bool reads(const AccessibilityNode &tree, const std::string &name)
{
    std::vector<std::string> names;
    namesIn(tree, names);
    return std::ranges::find(names, name) != names.end();
}

void testOverlaysAreInTheAccessibilityTree()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();
    page.add<Button>("Page button");

    auto &menu =
        harness.root.overlay().open<Menu>(OverlayDesc{.anchor = Rect::fromSize({20.0f, 20.0f}, {40.0f, 20.0f})});
    menu.addItem("Rename");
    const OverlayLayer::Id menuId = harness.root.overlay().lastId();
    harness.frame();

    AccessibilityNode tree = harness.root.accessibilityTree();
    AURA_CHECK(reads(tree, "Page button") && reads(tree, "Rename"), "an open menu is read alongside the page");

    //! Back to front, as drawn: the last child of the window is the menu.
    AURA_CHECK(!tree.children.empty() && reads(tree.children.back(), "Rename"),
               "and it comes after the content, the way it is drawn over it");

    harness.root.overlay().close(menuId);
    tree = harness.root.accessibilityTree();
    AURA_CHECK(!reads(tree, "Rename") && reads(tree, "Page button"), "a closed menu is gone from it at once");
}

void testClosingOverlayIsNotAnnouncedWhileItsExitDraws()
{
    Harness harness;
    harness.root.setContent<Column>();
    auto &menu = harness.root.overlay().open<Menu>(
        OverlayDesc{.anchor = Rect::fromSize({20.0f, 20.0f}, {40.0f, 20.0f}),
                    .transition = {.exit = [](Widget &, const OverlayDesc &, const std::function<void()> &)
                                   {
                                   }}});
    menu.addItem("Rename");
    const OverlayLayer::Id id = harness.root.overlay().lastId();
    harness.frame();
    harness.root.overlay().close(id);

    AURA_CHECK(menu.visibility() == Visibility::Visible, "the exit is still being drawn");
    AURA_CHECK(!reads(harness.root.accessibilityTree(), "Rename"), "but a screen reader no longer finds it");
}

void testModalOverlayIsADialog()
{
    Harness harness;
    harness.root.setContent<Column>().add<Button>("Behind");

    auto &dialog = harness.root.overlay().open<Column>(OverlayDesc{.placement = Placement::Center, .modal = true});
    dialog.add<Label>("Delete everything?");
    dialog.add<Button>("Cancel");
    harness.frame();

    const AccessibilityNode tree = harness.root.accessibilityTree();
    const AccessibilityNode &top = tree.children.back();
    AURA_CHECK(top.info.modal && top.info.role == Role::Window, "a modal overlay is a modal window");
    AURA_CHECK(reads(top, "Delete everything?") && reads(top, "Cancel"), "and holds what is in the dialog");
    AURA_CHECK(!tree.info.modal, "the page behind is not marked: it is the bridge that hides it");
}

void testTooltipIsADescriptionNotANode()
{
    Harness harness;
    auto &button = harness.root.setContent<Column>().add<Button>("Compile");
    button.setTooltip("Build the project");
    harness.frame();

    AccessibilityInfo info{};
    button.accessibility(info);
    AURA_CHECK(info.description == "Build the project", "a tooltip is what a screen reader reads after the name");

    button.setAccessibleDescription("Runs the build");
    button.accessibility(info = {});
    AURA_CHECK(info.description == "Runs the build", "unless the widget describes itself");

    //! Dwell on it long enough for the tooltip overlay to open.
    harness.root.pointerMoved(button.bounds().center());
    for (int i = 0; i < 12; ++i)
        harness.frame(0.1f);
    AURA_CHECK(!harness.root.overlay().empty() && !reads(harness.root.accessibilityTree(), "Build the project"),
               "and the tooltip overlay itself is not a second node for the same words");
}

void testFocusIsTrappedInTheTopmostOverlay()
{
    Harness harness;

    auto &page = harness.root.setContent<Column>();
    auto &outside = page.add<Button>("Outside");

    harness.frame();
    outside.requestFocus();

    auto &dialog = harness.root.overlay().open<Column>(OverlayDesc{.modal = true});
    auto &first = dialog.add<Button>("First");
    auto &second = dialog.add<Button>("Second");

    harness.frame();

    AURA_CHECK(harness.root.focused() == &first, "opening a modal moves focus inside it");

    harness.root.keyDown(wma::KEY_TAB);
    AURA_CHECK(harness.root.focused() == &second, "and moves within it");

    harness.root.keyDown(wma::KEY_TAB);
    AURA_CHECK(harness.root.focused() == &first, "and wraps inside it rather than escaping to the content behind");
}

// -----------------------------------------------------------------------------
// Dropdown
// -----------------------------------------------------------------------------

void testDropdown()
{
    Harness harness;

    auto &page = harness.root.setContent<Column>();
    auto &backend = page.add<Dropdown>(std::vector<std::string>{"Vulkan", "OpenGL", "CPU"});
    backend.setPlaceholder("Choose");

    int changes = 0;
    backend.selected.changed().connect(
        [&changes](int)
        {
            ++changes;
        });

    harness.frame();

    AURA_CHECK(backend.selected.get() == -1 && backend.text() == "Choose", "an unset drop-down shows its placeholder");

    harness.click(backend.bounds().center());
    harness.frame();

    AURA_CHECK(backend.isOpen(), "clicking opens the list");
    AURA_CHECK(!harness.root.overlay().empty(), "into the floating layer");

    //! The list is the topmost overlay; its rows are Selectables.
    Widget *list = harness.root.overlay().topmost();
    AURA_CHECK(list != nullptr && list->childCount() == 3, "one row per item");

    harness.click(list->childAt(1).bounds().center());
    harness.frame();

    AURA_CHECK(backend.selected.get() == 1 && backend.text() == "OpenGL", "choosing a row selects it");
    AURA_CHECK(!backend.isOpen(), "and closes the list");
    AURA_CHECK(changes == 1, "reporting the change once");
}

void testDropdownKeyboard()
{
    Harness harness;

    auto &page = harness.root.setContent<Column>();
    auto &choice = page.add<Dropdown>(std::vector<std::string>{"One", "Two", "Three"});
    choice.selected = 0;

    harness.frame();
    choice.requestFocus();

    //! Shut, the arrows step the selection directly -- a form stays fillable
    //! without ever opening a list.
    harness.root.keyDown(wma::KEY_DOWN);
    AURA_CHECK(choice.selected.get() == 1 && !choice.isOpen(), "Down steps the selection while the list is shut");

    harness.root.keyDown(wma::KEY_UP);
    AURA_CHECK(choice.selected.get() == 0, "and Up steps it back");

    harness.root.keyDown(wma::KEY_SPACE);
    harness.frame();
    AURA_CHECK(choice.isOpen(), "Space opens the list");

    harness.root.keyDown(wma::KEY_DOWN);
    harness.root.keyDown(wma::KEY_ENTER);
    harness.frame();

    AURA_CHECK(choice.selected.get() == 1 && !choice.isOpen(), "Down then Enter takes the highlighted row");

    harness.root.keyDown(wma::KEY_SPACE);
    harness.frame();
    harness.root.keyDown(wma::KEY_ESCAPE);
    harness.frame();

    AURA_CHECK(!choice.isOpen(), "Escape closes it");
    AURA_CHECK(choice.selected.get() == 1, "leaving the selection alone");
}

// -----------------------------------------------------------------------------
// Menu and tooltip
// -----------------------------------------------------------------------------

void testMenuAndSubmenu()
{
    Harness harness;
    harness.root.setContent<Column>();

    auto &menu = harness.root.overlay().open<Menu>(
        OverlayDesc{.anchor = Rect{{50.0f, 50.0f}, {50.0f, 50.0f}}, .placement = Placement::Cursor});

    int renamed = 0;
    menu.addItem("Rename", "F2")
        .activated.connect(
            [&renamed]
            {
                ++renamed;
            });
    menu.addSeparator();

    int nested = 0;
    menu.addSubmenu("Export",
                    [&nested](Menu &submenu)
                    {
                        submenu.addItem("PNG").activated.connect(
                            [&nested]
                            {
                                ++nested;
                            });
                    });

    harness.frame();

    AURA_CHECK(menu.childCount() == 3, "items and separators are rows of the menu");

    //! The submenu row opens a second overlay beside the first.
    harness.click(menu.childAt(2).bounds().center());
    harness.frame();

    Widget *submenu = harness.root.overlay().topmost();
    AURA_CHECK(submenu != nullptr && submenu != &menu, "a submenu opens as its own overlay");

    harness.click(submenu->childAt(0).bounds().center());
    harness.frame();

    AURA_CHECK(nested == 1, "its command runs");
    AURA_CHECK(harness.root.overlay().empty(), "and choosing it tears down the whole menu stack");
}

void testMenuCommandLeavesThePanelUnderIt()
{
    Harness harness;
    harness.root.setContent<Column>();

    //! A light-dismissible panel, as a launcher or a popover is, with a
    //! context menu opened from inside it.
    auto &panel = harness.root.overlay().open<Column>(OverlayDesc{.placement = Placement::Center});
    const OverlayLayer::Id panelId = harness.root.overlay().lastId();
    harness.frame();

    auto &menu = harness.root.overlay().open<Menu>(
        OverlayDesc{.anchor = Rect{{50.0f, 50.0f}, {50.0f, 50.0f}}, .placement = Placement::Cursor});
    int chosen = 0;
    menu.addItem("Pin").activated.connect(
        [&chosen]
        {
            ++chosen;
        });
    harness.frame();

    harness.click(menu.childAt(0).bounds().center());
    harness.frame();

    AURA_CHECK(chosen == 1, "the command runs");
    AURA_CHECK(harness.root.overlay().isOpen(panelId) && harness.root.overlay().topmost() == &panel,
               "choosing a menu command closes the menu and leaves the panel it came from");
}

void testTooltipOpensOnDwellAndFollowsHover()
{
    Harness harness;
    harness.root.setTooltipDelay(0.4f);

    auto &page = harness.root.setContent<Column>();
    auto &button = page.add<Button>("Compile");
    button.setTooltip("Compile the current project");

    harness.frame();
    harness.root.pointerMoved(button.bounds().center());

    harness.frame(0.2f);
    AURA_CHECK(harness.root.overlay().empty(), "nothing appears before the delay is up");

    harness.frame(0.3f);
    AURA_CHECK(!harness.root.overlay().empty(), "the tooltip opens once the pointer has rested");

    //! Moving off it takes the tooltip with it.
    harness.root.pointerMoved({5.0f, 290.0f});
    harness.frame(0.1f);
    harness.frame(0.1f);

    AURA_CHECK(harness.root.overlay().empty(), "and closes when the pointer leaves");
}

void testTooltipDoesNotShieldTheMenuUnderIt()
{
    Harness harness;
    harness.root.setTooltipDelay(0.1f);
    harness.root.setContent<Column>();

    auto &menu = harness.root.overlay().open<Menu>(
        OverlayDesc{.anchor = Rect{{40.0f, 40.0f}, {40.0f, 40.0f}}, .placement = Placement::Cursor});

    auto &item = menu.addItem("Rename");
    item.setTooltip("Give it another name");

    const OverlayLayer::Id id = harness.root.overlay().lastId();
    harness.frame();

    //! Rest on the item until its tooltip is up, so two overlays are open and
    //! the tooltip is the topmost.
    harness.root.pointerMoved(item.bounds().center());
    harness.frame(0.2f);

    AURA_CHECK(harness.root.overlay().topmost() != &menu, "the tooltip is above the menu");

    /*
     * Escape must reach the menu. A tooltip comes and goes with the pointer
     * and has no business consuming the key, so it is stepped over rather
     * than treated as the thing being dismissed.
     */
    AURA_CHECK(harness.root.keyDown(wma::KEY_ESCAPE), "Escape is used");
    harness.frame();

    AURA_CHECK(!harness.root.overlay().isOpen(id), "and closes the menu, not the tooltip");
}

void testClickThroughOverlayDoesNotBlockDismissal()
{
    Harness harness;
    harness.root.setContent<Column>();

    auto &popup = harness.root.overlay().open<Column>(OverlayDesc{.anchor = Rect{{10.0f, 10.0f}, {40.0f, 30.0f}}});
    popup.layout().minWidth = 40.0f;
    popup.layout().minHeight = 30.0f;

    const OverlayLayer::Id id = harness.root.overlay().lastId();

    /*
     * A click-through overlay that also opts out of light dismissal -- exactly
     * a tooltip. It is above the popup and covers the click, and neither fact
     * may keep the popup open: an overlay nobody can click is not part of the
     * dismissal stack at all.
     */
    auto &veil = harness.root.overlay().open<Column>(
        OverlayDesc{.placement = Placement::Center, .dismissOnOutsideClick = false});
    veil.setHitTestVisible(false);
    veil.layout().minWidth = 400.0f;
    veil.layout().minHeight = 300.0f;

    harness.frame();

    harness.root.pointerDown({350.0f, 280.0f});
    harness.frame();

    AURA_CHECK(!harness.root.overlay().isOpen(id),
               "a click past a click-through overlay still dismisses what is under it");
}

// -----------------------------------------------------------------------------
// Navigation
// -----------------------------------------------------------------------------

void testTabView()
{
    Harness harness;

    auto &tabs = harness.root.setContent<TabView>();
    auto &general = tabs.addTab("General");
    auto &graphics = tabs.addTab("Graphics");

    general.add<Label>("autosave");
    graphics.add<Label>("vsync");

    harness.frame();

    AURA_CHECK(tabs.tabCount() == 2, "two tabs");
    AURA_CHECK(general.visibility() == Visibility::Visible && graphics.visibility() == Visibility::Collapsed,
               "only the current page is shown");

    tabs.current = 1;
    harness.frame();

    AURA_CHECK(graphics.visibility() == Visibility::Visible && general.visibility() == Visibility::Collapsed,
               "switching swaps which page is laid out");

    tabs.current = 99;
    harness.frame();
    AURA_CHECK(tabs.current.get() == 1, "an out-of-range tab is clamped to a real one");
}

void testCollapsingHeader()
{
    Harness harness;

    auto &page = harness.root.setContent<Column>();
    auto &section = page.add<CollapsingHeader>("Graphics");
    section.content().add<CheckBox>("VSync");

    harness.frame();
    const f32 open = section.bounds().height();

    section.expanded = false;
    harness.frame();

    AURA_CHECK(section.bounds().height() < open, "folding takes the content's height back");
    AURA_CHECK(section.content().visibility() == Visibility::Collapsed, "and stops laying it out");

    //! Only the header toggles -- a click in the content area must reach what
    //! is actually there.
    section.expanded = true;
    harness.frame();

    const Rect header{section.bounds().min, {section.bounds().max.x, section.bounds().min.y + 4.0f}};

    harness.click(header.center());
    harness.frame();

    AURA_CHECK(!section.expanded.get(), "clicking the header folds it");
}

void testTreeNode()
{
    Harness harness;

    auto &page = harness.root.setContent<Column>();
    auto &assets = page.add<TreeNode>("Assets");

    auto &textures = assets.addChild("Textures");
    textures.addChild("brick.png");

    harness.frame();

    AURA_CHECK(assets.content().childCount() == 1, "a child node lands in the parent's content");
    AURA_CHECK(textures.content().childCount() == 1, "and nests further");

    assets.expanded = true;
    harness.frame();

    //! An expanded child is indented past its parent's label.
    AURA_CHECK(textures.bounds().min.x > assets.bounds().min.x, "children are indented under their parent");

    int activations = 0;
    textures.activated.connect(
        [&activations]
        {
            ++activations;
        });

    harness.click(Rect{textures.bounds().min, {textures.bounds().max.x, textures.bounds().min.y + 4.0f}}.center());

    AURA_CHECK(activations == 1, "clicking a node reports it");
}

void testPropertyBinding()
{
    Property<f32> source{1.0f};
    Property<f32> mirror{};

    {
        const ScopedConnection link = mirror.bind(source);
        AURA_CHECK(near(mirror.get(), 1.0f), "binding takes the current value at once");

        source = 5.0f;
        AURA_CHECK(near(mirror.get(), 5.0f), "and follows later changes");
    }

    source = 9.0f;
    AURA_CHECK(near(mirror.get(), 5.0f), "dropping the connection ends the binding");

    Property<std::string> text{};
    const ScopedConnection converted = text.bindFrom(source,
                                                     [](f32 value)
                                                     {
                                                         return std::to_string(static_cast<int>(value));
                                                     });

    AURA_CHECK(text.get() == "9", "bindFrom converts on the way through");
}

} // namespace

int main()
{
    testOverlayDrawsOverAndHitsFirst();
    testPlacementFlipsAndClamps();
    testOutsideClickAndEscapeDismiss();
    testStickyOverlayIgnoresOutsideClick();
    testModalBlocksTheContentBeneath();
    testCloseIsDeferredSoAnItemCanCloseItsOwnMenu();
    testOnClosedFires();
    testExitTransitionKeepsItUntilDone();
    testClosingReleasesFocusWhileTheExitRuns();
    testDefaultTransitionReachesBuiltInOverlays();
    testOverlaysAreInTheAccessibilityTree();
    testClosingOverlayIsNotAnnouncedWhileItsExitDraws();
    testModalOverlayIsADialog();
    testTooltipIsADescriptionNotANode();
    testFocusIsTrappedInTheTopmostOverlay();
    testDropdown();
    testDropdownKeyboard();
    testMenuAndSubmenu();
    testMenuCommandLeavesThePanelUnderIt();
    testTooltipOpensOnDwellAndFollowsHover();
    testTooltipDoesNotShieldTheMenuUnderIt();
    testClickThroughOverlayDoesNotBlockDismissal();
    testTabView();
    testCollapsingHeader();
    testTreeNode();
    testPropertyBinding();

    AURA_TEST_MAIN_RETURN();
}
