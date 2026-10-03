#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "TestUtils.h"
#include "WindowTestUtils.h"
#include "aura/UI/UI.hpp"
#include "aura/UI/WindowDecoration.h"

using namespace aura3d::ui;

namespace
{

bool near(f32 a, f32 b)
{
    return std::fabs(a - b) < 0.01f;
}

struct Harness
{
    aura3d::test::FakeWindow window;
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};
    DefaultWindowDecoration *decoration = nullptr;
    DrawList list;
    std::string title = "Aura3D test application";

    explicit Harness(glm::vec2 size = {480, 240})
    {
        root.resize(size);
        root.setContent<Widget>();
        auto owned = std::make_unique<DefaultWindowDecoration>();
        decoration = owned.get();
        root.setDecoration(std::move(owned));
        frame();
    }

    void frame(f32 seconds = 0.2f)
    {
        if (decoration)
            decoration->update(window, title);
        root.update(seconds);
    }

    void click(glm::vec2 position, PointerButton button = PointerButton::Left)
    {
        frame(0.5f);
        root.pointerDown(position, button);
        root.pointerUp(position, button);
    }

    void paint()
    {
        root.paint(list);
    }
};

usize iconCommands(const DrawList &list, const Rect &button)
{
    usize count = 0;
    for (const auto &command : list.commands())
        if (command.type == DrawCommandType::Rect && command.bounds.width() <= 12 && command.bounds.height() <= 12 &&
            button.contains(command.bounds.center()))
            ++count;
    return count;
}

void nativeCommands()
{
    Harness h;
    auto &bar = *h.decoration;
    h.click(bar.minimizeButton().bounds().center());
    AURA_CHECK(h.window.minimizes == 1 && h.window.maximizes == 0, "minimize submits only the native minimize request");

    h.click(bar.maximizeButton().bounds().center());
    h.frame();
    AccessibilityInfo info{};
    bar.maximizeButton().accessibility(info);
    AURA_CHECK(h.window.maximizes == 1 && h.window.restores == 0 && info.name == "Maximize",
               "a maximize request does not invent a confirmed maximized state");
    h.paint();
    const usize normalIcon = iconCommands(h.list, bar.maximizeButton().bounds());

    h.window.maximized = true;
    h.frame();
    bar.maximizeButton().accessibility(info = {});
    h.paint();
    AURA_CHECK(info.name == "Restore" && iconCommands(h.list, bar.maximizeButton().bounds()) > normalIcon,
               "confirmed maximization changes both the accessible action and the restore icon");
    h.click(bar.maximizeButton().bounds().center());
    AURA_CHECK(h.window.restores == 1, "the maximized control requests native restore");

    h.window.maximized = false;
    h.frame();
    bar.maximizeButton().accessibility(info = {});
    AURA_CHECK(info.name == "Maximize", "confirmed restoration changes the action back to maximize");
    h.click(bar.closeButton().bounds().center());
    AURA_CHECK(h.window.shouldClose() && h.window.closes == 1, "close requests shutdown");
}

void frameRegions()
{
    using enum wma::WindowHit;
    Harness h;
    const auto &bar = *h.decoration;
    const auto hit = [&bar](glm::vec2 point)
    {
        return bar.windowHit(point);
    };
    AURA_CHECK(hit(h.decoration->titleLabel().bounds().center()) == Caption && hit({60, 17}) == Caption,
               "the title and the empty bar drag the window");
    AURA_CHECK(hit(h.decoration->minimizeButton().bounds().center()) == Client &&
                   hit(h.decoration->closeButton().bounds().center()) == Client && hit({240, 120}) == Client,
               "caption buttons and application content stay client areas");
    AURA_CHECK(hit({0.5f, 120}) == Left && hit({479.5f, 120}) == Right && hit({240, 0.5f}) == Top &&
                   hit({240, 239.5f}) == Bottom,
               "each border resizes from its own edge");
    AURA_CHECK(hit({0.5f, 10}) == TopLeft && hit({479.5f, 0.5f}) == TopRight && hit({10, 239.5f}) == BottomLeft &&
                   hit({479.5f, 230}) == BottomRight,
               "corners resize diagonally, even over the close button");

    h.window.maximized = true;
    h.frame();
    AURA_CHECK(hit({0.5f, 120}) == Client && hit({479.5f, 0.5f}) == Client && hit({240, 0.5f}) == Caption,
               "a maximized window has no resize border");
}

void cancelledAndKeyboardActions()
{
    Harness h;
    auto &button = h.decoration->closeButton();
    const glm::vec2 point = button.bounds().center();
    h.root.pointerDown(point);
    AURA_CHECK(h.root.pointerCapture() == &button, "window controls reuse normal button pointer capture");
    h.root.pointerMoved({20, 120});
    h.root.pointerUp({20, 120});
    AURA_CHECK(h.window.closes == 0 && !h.root.pointerCapture(), "dragging off a close button cancels the action");

    h.root.pointerDown(point);
    h.root.cancelInput();
    h.root.pointerUp(point);
    AURA_CHECK(h.window.closes == 0, "focus loss cancels a pending close press");

    h.decoration->minimizeButton().requestFocus();
    h.root.keyDown(wma::KEY_ENTER);
    AURA_CHECK(h.window.minimizes == 1, "decoration buttons keep keyboard activation");
}

class OverflowContent final : public Widget
{
  protected:
    void paint(DrawList &out) override
    {
        out.fillRect(root()->surface(), {1, 0, 1, 1});
    }
};

void layoutAndClipping()
{
    Harness h;
    auto &content = h.root.setContent<OverflowContent>();
    h.title = std::string(200, 'W');
    h.frame();
    const f32 titleHeight = h.root.contentBounds().min.y;
    const Rect close = h.decoration->closeButton().bounds();
    AURA_CHECK(near(h.decoration->minimizeButton().bounds().height(), titleHeight) && near(close.max.x, 480) &&
                   near(close.min.y, 0),
               "window controls span the full title-bar height and reach the corner");
    AURA_CHECK(h.root.decoration() == h.decoration && content.bounds().min == h.root.contentBounds().min &&
                   content.bounds().max == h.root.contentBounds().max,
               "replacing application content preserves the decoration and lays content inside it");
    h.paint();
    bool contentClipped = false;
    bool titleClipped = false;
    for (const auto &command : h.list.commands())
    {
        if (command.color == glm::vec4{1, 0, 1, 1})
            contentClipped = near(command.clip.min.y, h.root.contentBounds().min.y);
        if (command.type == DrawCommandType::Text && command.text == &h.decoration->titleLabel().shaped())
            titleClipped = command.clip.max.x <= h.decoration->minimizeButton().bounds().min.x;
    }
    AURA_CHECK(contentClipped, "application painting cannot cover the reserved title bar");
    AURA_CHECK(titleClipped, "a long application title is clipped before the window controls");

    for (const glm::vec2 size : {glm::vec2{90, 100}, glm::vec2{20, 10}, glm::vec2{0, 0}})
    {
        h.root.resize(size);
        h.frame();
        const Rect available = h.root.contentBounds();
        AURA_CHECK(available.height() >= 0 && available.min.y <= size.y,
                   "a tiny surface leaves a nonnegative content rectangle");
        f32 previousEnd = 0;
        for (Button *button :
             {&h.decoration->minimizeButton(), &h.decoration->maximizeButton(), &h.decoration->closeButton()})
        {
            const Rect bounds = button->bounds();
            AURA_CHECK(bounds.min.x >= previousEnd - 0.01f && bounds.max.x <= size.x + 0.01f,
                       "controls stay ordered and inside a narrow surface");
            previousEnd = bounds.max.x;
        }
    }

    h.root.resize({480, 240});
    h.decoration->setVisibility(Visibility::Collapsed);
    h.frame();
    AURA_CHECK(near(h.root.contentBounds().min.y, 0) && near(h.root.contentBounds().min.x, 0),
               "a collapsed decoration reserves no space");
}

void iconGeometry()
{
    Harness h;
    h.paint();
    const Rect minimize = h.decoration->minimizeButton().bounds();
    const Rect maximize = h.decoration->maximizeButton().bounds();
    const Rect close = h.decoration->closeButton().bounds();
    bool dash = false;
    bool square = false;
    usize crossStrokes = 0;
    for (const auto &command : h.list.commands())
    {
        const glm::vec2 size = command.bounds.size();
        const glm::vec2 center = command.bounds.center();
        dash |= command.type == DrawCommandType::Rect && minimize.contains(center) && near(size.x, 12) &&
                near(size.y, 1);
        square |= command.type == DrawCommandType::Rect && maximize.contains(center) && near(size.x, 12) &&
                  near(size.y, 12) && near(command.borderWidth, 1);
        crossStrokes += command.type == DrawCommandType::Mask && close.contains(center) && near(size.x, 12);
    }
    AURA_CHECK(dash && square && crossStrokes == 2, "caption glyphs span 12px with 1px strokes");
}

void centredTitle()
{
    Harness h;
    h.title = "Aura";
    h.frame();
    const Rect title = h.decoration->titleLabel().bounds();
    AURA_CHECK(title.width() > 0 && near(title.center().x, h.root.size().x * 0.5f),
               "a short title is centred on the window, not beside the controls");
}

void overlaysAndModals()
{
    using enum wma::WindowHit;
    Harness h;
    const glm::vec2 bar{60, 17};
    h.root.overlay().open<Column>(OverlayDesc{.anchor = Rect{{100, 100}, {140, 120}}});
    h.frame();
    AURA_CHECK(h.decoration->windowHit(bar) == Client && h.decoration->windowHit({0.5f, 120}) == Client,
               "while a menu is open the first press on the frame reaches AuraUI");
    h.root.pointerDown(bar);
    h.root.pointerUp(bar);
    h.frame();
    AURA_CHECK(h.root.overlay().empty() && h.decoration->windowHit(bar) == Caption,
               "that press closes the menu and the bar drags again");

    h.root.overlay().open<Column>(OverlayDesc{.anchor = Rect{{150, 80}, {330, 160}},
                                              .placement = Placement::Over,
                                              .modal = true,
                                              .dismissOnOutsideClick = false});
    h.frame();
    AURA_CHECK(h.decoration->windowHit(bar) == Caption && h.decoration->windowHit({0.5f, 120}) == Left,
               "a modal dialog leaves the title bar and border working");
    AURA_CHECK(h.decoration->windowHit(h.decoration->closeButton().bounds().center()) == Client,
               "caption buttons stay client areas under a modal");

    const u64 generation = h.root.decorationGeneration();
    h.root.setDecoration(nullptr);
    AURA_CHECK(h.root.decorationGeneration() != generation, "replacing the decoration changes its generation");
    h.decoration = nullptr;
}

void resizeBorder()
{
    Harness h;
    const Rect inner = h.root.contentBounds();
    AURA_CHECK(near(inner.min.x, 1.5f) && near(inner.min.y, 34) && near(inner.max.x, 478.5f) && near(inner.max.y, 238.5f),
               "a resizable window frames its content with the title bar and a thin border");

    h.paint();
    const glm::vec4 barColor = h.decoration->resolvedStyle().surface.normal;
    bool framed = false;
    for (const auto &command : h.list.commands())
        framed |= command.type == DrawCommandType::Rect && command.color == barColor &&
                  near(command.bounds.min.x, 0) && near(command.bounds.max.x, 1.5f) && near(command.bounds.min.y, 34);
    AURA_CHECK(framed, "the border is painted in the title bar's colour");

    h.window.maximized = true;
    h.frame();
    const Rect full = h.root.contentBounds();
    AURA_CHECK(near(full.min.x, 0) && near(full.max.x, 480) && near(full.max.y, 240) && near(full.min.y, 34),
               "a maximized window gives the border back to the content");
}

DrawCommand buttonFill(Harness &h, Button &button)
{
    h.root.pointerMoved(button.bounds().center());
    h.frame(0.2f);
    h.paint();
    for (const auto &command : h.list.commands())
        if (command.type == DrawCommandType::Rect && command.bounds.min == button.bounds().min &&
            command.bounds.max == button.bounds().max)
            return command;
    return {.color = glm::vec4{0.0f}};
}

bool square(const DrawCommand &command)
{
    const Corners &r = command.radius;
    return r.topLeft == 0 && r.topRight == 0 && r.bottomRight == 0 && r.bottomLeft == 0;
}

void hoverFeedback()
{
    Harness h;
    const DrawCommand minimize = buttonFill(h, h.decoration->minimizeButton());
    const DrawCommand maximize = buttonFill(h, h.decoration->maximizeButton());
    const DrawCommand close = buttonFill(h, h.decoration->closeButton());
    const glm::vec4 gray = minimize.color;
    const glm::vec4 blue = maximize.color;
    const glm::vec4 red = close.color;
    AURA_CHECK(gray.a > 0 && near(gray.r, gray.g) && near(gray.g, gray.b),
               "minimize hover paints a neutral gray surface");
    AURA_CHECK(blue.a > 0 && blue.b > blue.r && blue.b > blue.g, "maximize hover paints a blue surface");
    AURA_CHECK(red.a > 0 && red.r > red.g && red.r > red.b, "close hover paints a red surface");
    AURA_CHECK(square(minimize) && square(maximize) && square(close), "hover surfaces fill square hit targets");
}

class CustomDecoration final : public IWindowDecoration
{
  public:
    CustomDecoration()
    {
        layout().padding = Thickness{0, 52, 0, 0};
        add<Label>("Custom header");
    }
    void update(wma::IWindowManager &, std::string_view) override
    {
    }
};

void replacementAndFocus()
{
    Harness h;
    auto &content = h.root.setContent<Button>("Application action");
    h.frame();
    content.requestFocus();
    h.root.focusNext();
    AURA_CHECK(h.root.focused() == &h.decoration->minimizeButton(),
               "focus traversal reaches the decoration after application content");
    const auto tree = h.root.accessibilityTree();
    bool hasMinimize = false;
    const auto inspect = [&](auto &&self, const AccessibilityNode &node) -> void
    {
        hasMinimize |= node.info.name == "Minimize";
        for (const auto &child : node.children)
            self(self, child);
    };
    inspect(inspect, tree);
    AURA_CHECK(hasMinimize, "decoration controls participate in the root accessibility tree");

    const glm::vec2 closePoint = h.decoration->closeButton().bounds().center();
    const WidgetRef oldButton(&h.decoration->closeButton());
    h.root.pointerDown(closePoint);
    auto custom = std::make_unique<CustomDecoration>();
    auto *customPointer = custom.get();
    h.decoration = nullptr;
    h.root.setDecoration(std::move(custom));
    AURA_CHECK(!oldButton.get() && !h.root.pointerCapture() && !h.root.focused(),
               "replacing a pressed decoration destroys its old controls and releases capture and focus");
    h.frame();
    h.root.pointerUp(closePoint);
    AURA_CHECK(h.window.closes == 0 && h.root.decoration() == customPointer && h.root.content() == &content,
               "a custom decoration preserves application content and cannot activate an old pending close");
    AURA_CHECK(near(h.root.contentBounds().min.y, 52), "a custom decoration reserves space through its insets");
    h.root.setDecoration(nullptr);
    h.frame();
    AURA_CHECK(!h.root.decoration() && near(content.bounds().min.y, 0),
               "disabling the decoration restores the full content area");
}

} // namespace

int main()
{
    nativeCommands();
    frameRegions();
    overlaysAndModals();
    cancelledAndKeyboardActions();
    layoutAndClipping();
    iconGeometry();
    centredTitle();
    resizeBorder();
    hoverFeedback();
    replacementAndFocus();
    AURA_TEST_MAIN_RETURN();
}
