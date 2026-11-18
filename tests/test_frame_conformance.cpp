// Cross-backend frame conformance. One scripted window scenario -- first frame, idle frames, an expose, a resize,
// minimize and restore, a move -- runs on every backend built in, through run(). After each step the
// window's own pixels are read back through Xlib, so what is checked is what reached the screen: each GPU backend is
// held to the software backend's image, and every backend to the scene itself.
//
// Opens windows, so it only runs against a disposable X server named by AURA_TEST_DISPLAY (Xvfb), never the
// desktop's $DISPLAY. Without one it reports skipped.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#include "aura/Core/Engine.h"
#include "aura/Renderer/Canvas.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/RendererFactory.h"
#include "aura/UI/UI.hpp"

#include "TestUtils.h"
#include "XlibWindow.h"

using namespace aura3d;

namespace
{

constexpr int kSkipped = 77;
constexpr i32 kWidth = 320;
constexpr i32 kHeight = 240;
//! How long the loop runs after each step before the window is read: enough for the server to deliver the step's
//! events and for the redraw they cause to be presented, a few times the loop's idle wait.
constexpr std::chrono::milliseconds kSettle{350};

//! The scene's colours as they reach the window: an sRGB swapchain stores 0 and 1 unchanged.
constexpr u32 kBackground = 0x0000FF; //! Clear colour.
constexpr u32 kSquare = 0xFF0000;     //! Centred in the content area, so a resize moves it.

using Image = test::xlib::Pixels;

//! Pixels with any channel further than @p tolerance apart; every pixel when the sizes differ.
[[nodiscard]] usize differingPixels(const Image &a, const Image &b, int tolerance)
{
    if (a.width != b.width || a.height != b.height)
        return std::max(a.rgb.size(), b.rgb.size());
    usize differing = 0;
    for (usize i = 0; i < a.rgb.size(); ++i)
        for (const u32 shift : {0u, 8u, 16u})
            if (std::abs(static_cast<int>((a.rgb[i] >> shift) & 0xFFu) -
                         static_cast<int>((b.rgb[i] >> shift) & 0xFFu)) > tolerance)
            {
                ++differing;
                break;
            }
    return differing;
}

//! How many frames a step must draw: none where the window shows nothing new (idle frames, a minimized window),
//! some where the platform or the size asks for a redraw.
enum class Draws : u8
{
    Some,
    None,
    Any,
};

struct Step
{
    const char *name;
    std::function<void(const test::xlib::Window &)> act;
    Draws draws = Draws::Some;
    bool visible = true;  //! Read back afterwards; an unmapped window has no pixels.
    bool resizes = false; //! The window ends the step at 400x300.
};

struct Checkpoint
{
    std::string step;
    Image image;
    int frames = 0; //! Frames drawn during the step.
};

const std::array<Step, 7> &scenario()
{
    static const std::array<Step, 7> steps{{
        {"first frame", nullptr},
        {"idle frames", nullptr, Draws::None},
        {"expose",
         [](const test::xlib::Window &window)
         {
             window.expose();
         }},
        {"resize",
         [](const test::xlib::Window &window)
         {
             window.resize(400, 300);
         },
         Draws::Some, true, true},
        {"minimized",
         [](const test::xlib::Window &window)
         {
             window.unmap();
         },
         Draws::None, false},
        {"restored",
         [](const test::xlib::Window &window)
         {
             window.map();
         }},
        //! Nothing asks for a redraw, and none is needed: the pixels move with the window.
        {"moved",
         [](const test::xlib::Window &window)
         {
             window.move(120, 90);
         },
         Draws::Any},
    }};
    return steps;
}

//! Runs the scenario on @p backend; empty when the backend could not start on this display.
std::vector<Checkpoint> runScenario(RendererChoice backend)
{
    std::string api = RendererChoiceToString(backend);
    std::ranges::transform(api, api.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    AuraConfig config;
    config.set(setting::rendererApi, api)
        .set(setting::windowWidth, kWidth)
        .set(setting::windowHeight, kHeight)
        .set(setting::windowBackend, wma::WindowBackend::X11)
        .set(setting::windowDecorations, wma::DecorationMode::ClientSide)
        .set(setting::vulkanGpu, std::string{"any"})
        .set(setting::audioBackend, wma::AudioBackend::Null)
        .set(setting::loggingLevel, ink::LogLevel::ERROR);
    Engine engine(config);
    IRenderer &renderer = *engine.getRenderer();
    if (renderer.getBackendType() != backend)
        return {};
    wma::IWindowManager &window = *renderer.getWindowManager();
    const test::xlib::Window x11{window.getNativeDisplayHandle(), window.getWindowInstance()};

    renderer.setClearColor(0.0f, 0.0f, 1.0f);
    ui::UIView view(renderer);
    view.attachInput(window);
    view.root().setContent<ui::Widget>();
    gfx::Canvas canvas;

    std::vector<Checkpoint> checkpoints;
    usize step = 0;
    int frames = 0;
    auto stepStart = std::chrono::steady_clock::now();
    renderer.run(
        [&]
        {
            view.update(0.0f);
            if (std::chrono::steady_clock::now() - stepStart >= kSettle)
            {
                x11.sync();
                const Step &current = scenario()[step];
                checkpoints.push_back({current.name, current.visible ? x11.read() : Image{}, frames});
                frames = 0;
                if (++step == scenario().size())
                    window.close();
                else if (scenario()[step].act)
                {
                    scenario()[step].act(x11);
                    x11.sync();
                }
                stepStart = std::chrono::steady_clock::now();
            }
            if (!view.needsDraw() || !renderer.beginRenderPass())
                return;
            ++frames;
            const ui::Rect content = view.root().contentBounds();
            const glm::vec2 centre = glm::floor(content.center());
            canvas.clear();
            canvas.rect(centre - glm::vec2{40.0f, 30.0f}, {80.0f, 60.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
            renderer.drawBatch(canvas);
            view.draw();
            renderer.endRenderPass();
        });
    return checkpoints;
}

//! What the scene itself requires, on every backend: the title bar, the square at the content's centre and the
//! clear colour around it. A regression every backend shares still fails here.
void checkScene(const char *backend, const Checkpoint &checkpoint)
{
    const std::string where = std::string(backend) + " / " + checkpoint.step + ": ";
    const Image &image = checkpoint.image;
    AURA_CHECK(image.valid(), where + "the window can be read back");
    if (!image.valid())
        return;
    const ui::WindowChrome chrome{};
    const i32 bar = static_cast<i32>(chrome.titleHeight) / 2;
    const i32 contentTop = static_cast<i32>(chrome.titleHeight);
    const i32 centreX = image.width / 2;
    const i32 centreY = contentTop + (image.height - contentTop) / 2;
    AURA_CHECK(image.at(image.width / 4, bar) != kBackground && image.at(image.width / 4, bar) < 0x404040,
               where + "the title bar is drawn");
    AURA_CHECK(image.at(centreX, centreY) == kSquare, where + "the square is at the content's centre");
    AURA_CHECK(image.at(image.width / 8, centreY) == kBackground && image.at(centreX, image.height - 8) == kBackground,
               where + "the clear colour surrounds it");
}

} // namespace

int main()
{
    const char *testDisplay = std::getenv("AURA_TEST_DISPLAY");
    if (!testDisplay || !wma::isBackendAvailable(wma::WindowBackend::X11))
    {
        std::puts("[SKIP] frame conformance: needs AURA_TEST_DISPLAY (a disposable X server) and the X11 backend");
        return kSkipped;
    }
    setenv("DISPLAY", testDisplay, 1);

    std::vector<RendererChoice> backends;
    for (const RendererChoice choice : {RendererChoice::SOFTWARE, RendererChoice::OPENGL, RendererChoice::VULKAN})
        if (RendererFactory::isAvailable(choice))
            backends.push_back(choice);
    if (backends.empty() || backends.front() != RendererChoice::SOFTWARE)
    {
        std::puts("[SKIP] frame conformance: the software backend, the reference, is not built");
        return kSkipped;
    }

    std::vector<Checkpoint> reference;
    for (const RendererChoice backend : backends)
    {
        const char *name = RendererChoiceToString(backend);
        std::vector<Checkpoint> run;
        try
        {
            run = runScenario(backend);
        }
        catch (const std::exception &e)
        {
            std::printf("%s: %s\n", name, e.what());
        }
        if (run.empty())
        {
            //! A GPU backend this display cannot run (no driver) is not a conformance failure.
            AURA_CHECK(backend != RendererChoice::SOFTWARE, std::string(name) + ": the reference backend starts");
            std::printf("[SKIP] %s: did not start on this display\n", name);
            continue;
        }
        AURA_CHECK(run.size() == scenario().size(), std::string(name) + ": every step ran");
        for (usize i = 0; i < run.size() && i < scenario().size(); ++i)
        {
            const Step &step = scenario()[i];
            const std::string where = std::string(name) + " / " + step.name + ": ";
            if (step.draws == Draws::Some)
                AURA_CHECK(run[i].frames > 0, where + "draws a frame");
            else if (step.draws == Draws::None)
                AURA_CHECK(run[i].frames == 0, where + "draws no frame");
            if (!step.visible)
                continue;
            checkScene(name, run[i]);
            if (step.resizes)
                AURA_CHECK(run[i].image.width == 400 && run[i].image.height == 300,
                           where + "the window has the new size");
            if (backend != RendererChoice::SOFTWARE && i < reference.size() && reference[i].image.valid())
            {
                //! GPUs rasterise glyph edges a little differently; the golden test's allowance.
                const usize differing = differingPixels(run[i].image, reference[i].image, 32);
                const usize allowed = run[i].image.rgb.size() / 50;
                AURA_CHECK(differing <= allowed, where + "matches the software backend (" + std::to_string(differing) +
                                                     " pixels differ, " + std::to_string(allowed) + " allowed)");
            }
        }
        if (backend == RendererChoice::SOFTWARE)
            reference = run;
    }
    return test::failureCount() == 0 ? 0 : 1;
}
