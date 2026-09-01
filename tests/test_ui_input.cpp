/*
 * The UI's *attached* input path: Context::attachInput() plus ui::InputRouter,
 * driven by a fake window that dispatches events exactly as a backend does.
 *
 * test_ui covers the same widgets through newFrame(const Input&), where the
 * test hands the UI a filled-in Input itself. That leaves the whole wiring
 * untested -- which wma context each subscription lands in, whether the
 * per-frame buffer swap actually delivers what the callbacks collected, and
 * whether a router mode switch keeps the UI reachable. Every application uses
 * exactly that wiring and none of it fails loudly: a field that never receives
 * a keystroke still focuses, draws its caret and looks entirely correct.
 */

#include <span>
#include <string>
#include <vector>

#include <wma/wma.hpp>

#include "aura/Renderer/IRenderer.h"
#include "aura/UI/AuraUI.h"
#include "aura/UI/InputRouter.h"

#include "TestUtils.h"

using namespace aura3d;

namespace {

//! Records nothing: this suite asserts on edited values and capture flags, not
//! on geometry. Handles are 1-based so isValidHandle() accepts them and the UI
//! considers itself usable.
class StubRenderer final : public IRenderer {
public:
    StubRenderer() : IRenderer(wma::WindowDetails{}) {}

    TextureHandle createDynamicTexture(u32, u32) override { return ++_nextTexture; }
    void updateTextureRegion(TextureHandle, u32, u32, u32, u32, const u8*) override {}
    void drawBatch2D(std::span<const gfx::Vertex2D>, std::span<const u32>, TextureHandle) override {}

    //! Everything below is inert: this suite never reaches for it.
    void initialize(AuraSettings*, const JobSystem*) override {}
    void handleWindowChanges() override {}
    void cleanup() override {}
    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&&) override { return {}; }
    IndexBufferHandle createIndexBuffer(std::vector<u16>&&) override { return {}; }
    IndexBufferHandle createIndexBuffer(std::vector<u32>&&) override { return {}; }
    TextureHandle createSolidColorTexture(u8, u8, u8, u8) override { return {}; }
    TextureHandle createTextureFromPixels(const u8*, u32, u32) override { return {}; }
    void beginFrame() override {}
    void beginRenderPass() override {}
    void endRenderPass() override {}
    void endFrame() override {}
    void setTransform(const gfx::TransformUBO&) override {}
    void bindVertexBuffer(VertexBufferHandle) override {}
    void bindIndexBuffer(IndexBufferHandle) override {}
    void bindTexture(TextureHandle) override {}
    void drawIndexed(u32, u32) override {}
    void draw(u32, u32) override {}
    void setClearColor(f32, f32, f32, f32) override {}
    wma::IWindowManager* getWindowManager() override { return nullptr; }
    RendererChoice getBackendType() const override { return RendererChoice::SOFTWARE; }

protected:
    void createWindow(const char*, const wma::WindowBackend&) override {}

private:
    TextureHandle _nextTexture{0};
};

//! Exposes the protected dispatch a real backend calls, so a test can deliver
//! the same events xkb/SDL would.
class FakeKeyboard final : public wma::KeyboardListener {
public:
    using wma::KeyboardListener::dispatchKeyPress;
    using wma::KeyboardListener::dispatchKeyRelease;
    using wma::KeyboardListener::dispatchText;

    //! One keystroke as a backend delivers it: the key event and, for a
    //! printable key, the character it produced.
    void type(char ascii)
    {
        dispatchText(static_cast<wma::Codepoint>(static_cast<unsigned char>(ascii)));
    }
};

class FakeMouse final : public wma::MouseListener {
public:
    //! currentPosition_ is written by the caller on every real backend, not by
    //! MouseListener::dispatchMove(), and Context polls it rather than
    //! subscribing -- so a fake that only dispatches leaves the UI at (0, 0).
    void moveTo(f64 x, f64 y)
    {
        currentPosition_ = wma::WMAMousePosition{x, y};
        processPendingEvents(wma::PendingEvent{wma::PendingEvent::WMAMove, currentPosition_});
    }

    void setButton(bool down)
    {
        processPendingEvents(wma::PendingEvent{down ? wma::PendingEvent::WMAButtonPress
                                                    : wma::PendingEvent::WMAButtonRelease,
                                               wma::MouseButton::WMALeft});
    }

private:
    void updateCursorState() override {}
};

class FakeWindow final : public wma::IWindowManager {
public:
    void createWindow(const char*) override {}
    void pollEvents() override {}
    void* getWindowInstance() override { return nullptr; }

    wma::WindowFlags* getWindowFlags() noexcept override { return &_flags; }
    const wma::WindowDetails* getWindowDetails() noexcept override { return &_details; }
    const std::vector<const char*> getVulkanExtensions() const override { return {}; }

    wma::KeyboardListener& getKeyboardListener() noexcept override { return keyboard; }
    wma::MouseListener& getMouseListener() noexcept override { return mouse; }

    void setTextInputEnabled(bool enabled) noexcept override { textInputEnabled = enabled; }
    [[nodiscard]] bool isTextInputEnabled() const noexcept override { return textInputEnabled; }

    bool shouldClose() const override { return false; }
    wma::WindowBackend getBackendType() const override { return wma::WindowBackend::SDL3; }
    wma::GraphicsAPI getGraphicsAPI() const override { return wma::GraphicsAPI::CPU; }
    wma::WmaCode destroy() override { return wma::WmaCode::Ok; }

    FakeKeyboard keyboard;
    FakeMouse mouse;
    bool textInputEnabled = false;

private:
    wma::WindowFlags _flags{};
    wma::WindowDetails _details{};
};

constexpr glm::vec2 kPanelOrigin{20.0f, 20.0f};
constexpr float kPanelWidth = 220.0f;

//! One frame of a panel holding a single text field, focused on the first.
void fieldFrame(ui::Context& gui, std::string& value, bool focusFirst)
{
    gui.newFrame();
    (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
    if (focusFirst)
        gui.setKeyboardFocusHere();
    (void)gui.inputText("Field", value);
    gui.endPanel();
    gui.render();
}

void testAttachedFieldReceivesTypedText()
{
    StubRenderer renderer;
    FakeWindow window;
    ui::Context gui(&renderer);
    gui.attachInput(window);

    std::string value;

    fieldFrame(gui, value, /*focusFirst=*/true);
    AURA_CHECK(gui.isCapturingTextInput(), "an attached field asks for platform text input");

    window.keyboard.type('H');
    window.keyboard.type('i');
    fieldFrame(gui, value, false);

    AURA_CHECK(value == "Hi", "text dispatched by the window reaches the focused field");
}

void testAttachedFieldReceivesKeyEvents()
{
    StubRenderer renderer;
    FakeWindow window;
    ui::Context gui(&renderer);
    gui.attachInput(window);

    std::string value = "ab";

    fieldFrame(gui, value, /*focusFirst=*/true);

    window.keyboard.dispatchKeyPress(wma::KEY_BACKSPACE, false);
    fieldFrame(gui, value, false);

    AURA_CHECK(value == "a", "key events dispatched by the window reach the focused field");
}

//! The Sandbox's arrangement: the router owns the contexts and attaches the UI
//! inside the one mode that has a UI, so nothing works unless the subscription
//! landed in that mode's context and the switch made it resolve.
void testRoutedFieldReceivesTypedText()
{
    StubRenderer renderer;
    FakeWindow window;
    ui::Context gui(&renderer);

    ui::InputRouter router(window, gui);
    router.switchTo(ui::InputRouter::kMenu);

    std::string value;

    router.newFrame();
    (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
    gui.setKeyboardFocusHere();
    (void)gui.inputText("Field", value);
    gui.endPanel();
    gui.render();

    AURA_CHECK(gui.isCapturingTextInput(), "a routed field asks for platform text input");

    window.keyboard.type('X');

    router.newFrame();
    (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
    (void)gui.inputText("Field", value);
    gui.endPanel();
    gui.render();

    AURA_CHECK(value == "X", "text reaches a field in the router's UI mode");
}

//! Cursor over the first widget row of a panel at kPanelOrigin, kept well left
//! of the trailing label so it lands on the field itself.
[[nodiscard]] glm::vec2 firstRowPoint(const ui::Metrics& metrics)
{
    return {kPanelOrigin.x + metrics.padding + 10.0f,
            kPanelOrigin.y + metrics.rowHeight + metrics.padding + metrics.rowHeight * 0.5f};
}

/*
 * The Sandbox's flow end to end: a key action opens the UI mode, the field is
 * focused by clicking it rather than by setKeyboardFocusHere(), and only then
 * is anything typed.
 */
void testClickFocusedFieldReceivesTypedText()
{
    StubRenderer renderer;
    FakeWindow window;
    ui::Context gui(&renderer);

    ui::InputRouter router(window, gui);
    router.bindToggle(wma::KEY_F1, ui::InputRouter::kMenu);
    router.bindClose(wma::KEY_ESCAPE, ui::InputRouter::kMenu);

    std::string value;

    const auto frame = [&]() {
        router.newFrame();
        if (router.isMode(ui::InputRouter::kMenu))
        {
            (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
            (void)gui.inputText("Field", value);
            gui.endPanel();
        }
        gui.render();
    };

    window.keyboard.dispatchKeyPress(wma::KEY_F1, false);
    window.keyboard.dispatchKeyRelease(wma::KEY_F1);
    frame();
    AURA_CHECK(router.isMode(ui::InputRouter::kMenu), "F1 opens the UI mode");

    const glm::vec2 point = firstRowPoint(gui.theme.metrics);
    window.mouse.moveTo(point.x, point.y);
    frame();

    window.mouse.setButton(true);
    frame();
    window.mouse.setButton(false);
    frame();

    AURA_CHECK(gui.isCapturingTextInput(), "clicking a field claims platform text input");

    window.keyboard.type('Z');
    frame();

    AURA_CHECK(value == "Z", "text reaches a field focused by clicking it");
}

/*
 * Focus granted by a click, rather than by Tab or setKeyboardFocusHere(),
 * reaches the field a frame later than the keyboard path does. Both the
 * Escape snapshot and the caret placement depend on telling that frame apart.
 */
void testClickFocusPreservesTheEscapeSnapshot()
{
    StubRenderer renderer;
    FakeWindow window;
    ui::Context gui(&renderer);
    gui.attachInput(window);

    std::string value = "original";

    const auto frame = [&]() { fieldFrame(gui, value, /*focusFirst=*/false); };

    frame();

    //! Hard against the left inset, so the caret lands at offset 0 and the
    //! edit below is unambiguous.
    const glm::vec2 point = firstRowPoint(gui.theme.metrics);
    window.mouse.moveTo(kPanelOrigin.x + gui.theme.metrics.padding + 1.0f, point.y);
    frame();

    window.mouse.setButton(true);
    frame();
    window.mouse.setButton(false);
    frame();

    window.keyboard.type('!');
    frame();
    AURA_CHECK(value == "!original", "a click-focused field takes the edit at the caret");

    window.keyboard.dispatchKeyPress(wma::KEY_ESCAPE, false);
    frame();
    AURA_CHECK(value == "original", "Escape reverts a field focused by clicking");
}

void testClickPlacesTheCaretWhereItLanded()
{
    StubRenderer renderer;
    FakeWindow window;
    ui::Context gui(&renderer);
    gui.attachInput(window);

    std::string value = "abc";

    const auto frame = [&]() { fieldFrame(gui, value, /*focusFirst=*/false); };

    frame();

    //! Hard against the field's left inset, so the nearest caret slot is 0.
    const glm::vec2 point = firstRowPoint(gui.theme.metrics);
    window.mouse.moveTo(kPanelOrigin.x + gui.theme.metrics.padding + 1.0f, point.y);
    frame();

    window.mouse.setButton(true);
    frame();
    window.mouse.setButton(false);
    frame();

    window.keyboard.type('X');
    frame();

    AURA_CHECK(value == "Xabc", "a click leaves the caret where it landed, not at the end");
}

} // namespace

int main()
{
    testAttachedFieldReceivesTypedText();
    testAttachedFieldReceivesKeyEvents();
    testRoutedFieldReceivesTypedText();
    testClickFocusedFieldReceivesTypedText();
    testClickFocusPreservesTheEscapeSnapshot();
    testClickPlacesTheCaretWhereItLanded();

    AURA_TEST_MAIN_RETURN();
}
