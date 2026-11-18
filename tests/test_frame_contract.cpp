// The frame contract IRenderer::run() and its backends keep: what a resize, an idle frame or a dropped present
// does, and who gets to see it. Driven headless through the software backend and a scripted window.
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/JobSystem/JobSystem.h"
#ifdef AURA_HAS_CPU
#include "aura/Renderer/Software/CPURenderer.h"
#endif

#include "TestUtils.h"
#include "WindowTestUtils.h"

using namespace aura3d;

namespace
{

#ifdef AURA_HAS_CPU
//! A software renderer on a FakeWindow it presents to; the window stays owned by the renderer.
struct ScriptedRenderer
{
    JobSystem jobs{1};
    cpu::CPURenderer renderer{wma::WindowDetails{.width = 64, .height = 48}};
    test::FakeWindow *window = nullptr;

    ScriptedRenderer()
    {
        renderer.setWindowFactory(
            [this](auto, const wma::WindowDetails &details, auto)
            {
                auto made = std::make_unique<test::FakeWindow>();
                made->details.width = details.width;
                made->details.height = details.height;
                made->presentable = true;
                window = made.get();
                return made;
            });
        renderer.initialize(AuraSettings::get(), &jobs);
    }
};

void checkResizeReachesEveryReader()
{
    ScriptedRenderer scripted;
    test::FakeWindow &window = *scripted.window;
    IRenderer &renderer = scripted.renderer;

    int polls = 0;
    window.onPoll = [&polls](test::FakeWindow &w)
    {
        switch (++polls)
        {
        case 2:
            w.resize(100, 80);
            break;
        case 4:
            w.resize(120, 90);
            w.resize(128, 96);
            break;
        case 6:
            w.close();
            break;
        default:
            break;
        }
    };

    wma::ResizeWatch appResizes;
    std::vector<int> framesThatSawResize;
    std::vector<glm::uvec2> drawnSizes;
    int frame = 0;
    renderer.run(
        [&]
        {
            ++frame;
            if (appResizes.consume(*window.getWindowFlags()))
                framesThatSawResize.push_back(frame);
            renderer.beginRenderPass();
            drawnSizes.push_back(renderer.renderTargetSize());
            renderer.endRenderPass();
        });

    AURA_CHECK(frame == 6, "resize: run() ran one frame per poll until the window closed");
    AURA_CHECK((framesThatSawResize == std::vector<int>{2, 4}),
               "resize: the app sees each resize inside run(), though the renderer consumed it first");
    AURA_CHECK(drawnSizes.size() == 6 && drawnSizes[1] == glm::uvec2(100, 80),
               "resize: the renderer applied the resize before the frame that sees it");
    AURA_CHECK(drawnSizes.size() == 6 && drawnSizes[3] == glm::uvec2(128, 96),
               "resize: a burst of resizes between two frames is applied once, at its last size");
}

void checkNeedsFrameUntilPresented()
{
    ScriptedRenderer scripted;
    test::FakeWindow &window = *scripted.window;
    IRenderer &renderer = scripted.renderer;
    const auto frame = [&renderer]
    {
        renderer.beginFrame();
        renderer.beginRenderPass();
        renderer.endRenderPass();
        renderer.endFrame();
    };

    AURA_CHECK(renderer.needsFrame(), "needsFrame: true before the first frame reaches the screen");
    frame();
    AURA_CHECK(!renderer.needsFrame(), "needsFrame: false once one has");

    window.resize(32, 32);
    AURA_CHECK(renderer.needsFrame(), "needsFrame: a resize needs a frame before the renderer has seen it");
    renderer.beginFrame();
    AURA_CHECK(renderer.needsFrame(), "needsFrame: and still once beginFrame() applied it, until it presents");
    renderer.endFrame();
    AURA_CHECK(!renderer.needsFrame(), "needsFrame: false once the resized frame presented");

    window.presentable = false;
    frame();
    AURA_CHECK(renderer.needsFrame(), "needsFrame: a dropped present needs another frame");
    frame();
    AURA_CHECK(renderer.needsFrame(), "needsFrame: for as long as presents keep dropping");
    window.presentable = true;
    frame();
    AURA_CHECK(!renderer.needsFrame(), "needsFrame: until one reaches the screen");

    window.flags.surfaceLost = true;
    AURA_CHECK(renderer.needsFrame(), "needsFrame: a lost surface needs a frame");
    window.flags.surfaceLost = false;

    window.flags.markExposed();
    AURA_CHECK(renderer.needsFrame(), "needsFrame: an expose needs a frame");
    renderer.beginFrame();
    AURA_CHECK(renderer.needsFrame(), "needsFrame: until the frame that redraws it");
    renderer.endFrame();
    AURA_CHECK(!renderer.needsFrame(), "needsFrame: reaches the screen");
}

//! One scripted window state per frame, and the frameBegun() every backend must report for it.
void checkFrameBegunSequence()
{
    ScriptedRenderer scripted;
    test::FakeWindow &window = *scripted.window;
    IRenderer &renderer = scripted.renderer;

    struct Step
    {
        const char *state;
        bool minimized;
        bool occluded;
        bool ready;
        bool surfaceAvailable;
        bool begun;
    };
    constexpr std::array steps{
        Step{"a normal frame", false, false, true, true, true},
        Step{"a minimized window", true, false, true, true, false},
        Step{"the restored window", false, false, true, true, true},
        Step{"an occluded window", false, true, true, true, false},
        Step{"the window back on screen", false, false, true, true, true},
        Step{"a frame still on its way to the screen", false, false, false, true, false},
        Step{"that frame shown", false, false, true, true, true},
        Step{"no surface", false, false, true, false, false},
        Step{"the surface back", false, false, true, true, true},
    };

    AURA_CHECK(!renderer.frameBegun(), "frameBegun: false before any frame");
    for (const Step &step : steps)
    {
        window.setVisibility(step.minimized, step.occluded);
        window.ready = step.ready;
        window.surfaceAvailable = step.surfaceAvailable;
        const int presents = window.presents;
        renderer.beginFrame();
        AURA_CHECK(renderer.frameBegun() == step.begun,
                   std::string("frameBegun: ") + (step.begun ? "true" : "false") + " for " + step.state);
        renderer.beginRenderPass();
        renderer.endRenderPass();
        renderer.endFrame();
        AURA_CHECK(!renderer.frameBegun(), std::string("frameBegun: false again after endFrame() for ") + step.state);
        AURA_CHECK((window.presents > presents) == step.begun,
                   std::string("frameBegun: ") + step.state + (step.begun ? " presents" : " presents nothing"));
        AURA_CHECK(renderer.needsFrame() != step.begun,
                   std::string("frameBegun: ") + step.state +
                       (step.begun ? " needs no frame after it presented" : " still needs one"));
    }

    window.resize(40, 30);
    window.setVisibility(true);
    renderer.beginFrame();
    renderer.endFrame();
    AURA_CHECK(renderer.renderTargetSize() == glm::uvec2(64, 48),
               "frameBegun: a resize while minimized is not applied by a frame that did not begin");
    window.setVisibility(false);
    renderer.beginFrame();
    AURA_CHECK(renderer.frameBegun() && renderer.renderTargetSize() == glm::uvec2(40, 30),
               "frameBegun: the restored frame applies it");
    renderer.endFrame();
}

//! run() with a callback that draws only when something changed: a frame only then, and a sleep otherwise.
void checkRunSkipsIdleFrames()
{
    ScriptedRenderer scripted;
    test::FakeWindow &window = *scripted.window;
    IRenderer &renderer = scripted.renderer;

    int polls = 0;
    bool dirty = false; //! The app's own "something changed", cleared by the frame that draws it.
    window.onPoll = [&](test::FakeWindow &w)
    {
        switch (++polls)
        {
        case 2:
            w.flags.markExposed();
            break;
        case 3:
            dirty = true;
            break;
        case 5:
            w.resize(80, 60);
            break;
        case 6:
            w.setVisibility(true);
            dirty = true;
            break;
        case 8:
            w.setVisibility(false);
            break;
        case 9:
            w.close();
            break;
        default:
            break;
        }
    };

    int iterations = 0;
    std::vector<int> drawnAt;
    renderer.run(
        [&]
        {
            ++iterations;
            if (!dirty && !renderer.needsFrame())
                return;
            AURA_CHECK(!renderer.frameBegun(), "on demand: the frame has not begun before the first pass");
            if (!renderer.beginRenderPass())
                return; //! Nothing to draw into: the change stays pending.
            drawnAt.push_back(polls);
            AURA_CHECK(renderer.frameBegun(), "on demand: a pass that opened has a frame");
            renderer.endRenderPass();
            dirty = false;
        });

    AURA_CHECK(iterations == 9, "on demand: the callback runs every iteration, drawn or not");
    AURA_CHECK((drawnAt == std::vector<int>{1, 2, 3, 5, 8}),
               "on demand: frames are drawn for the first frame, an expose, a change, a resize and the restored "
               "window only");
    AURA_CHECK(window.presents == 5, "on demand: only those frames present");
    AURA_CHECK(window.waits == 4, "on demand: every other iteration sleeps, minimized ones included");
    AURA_CHECK(!renderer.needsFrame(), "on demand: and the window ends up showing the last frame");
}

//! Offscreen work inside run() records without touching the window, minimized or not.
void checkOffscreenOnlyPresentsNothing()
{
    ScriptedRenderer scripted;
    test::FakeWindow &window = *scripted.window;
    IRenderer &renderer = scripted.renderer;
    const RenderTargetHandle target = renderer.createRenderTarget({.width = 16, .height = 16});

    int polls = 0;
    window.onPoll = [&](test::FakeWindow &w)
    {
        switch (++polls)
        {
        case 3:
            w.setVisibility(true);
            break;
        case 5:
            w.close();
            break;
        default:
            break;
        }
    };

    std::vector<bool> opened;
    bool keptFrame = false;
    renderer.run(
        [&]
        {
            if (polls == 3) //! After the presented first iteration and an offscreen-only second.
                keptFrame = !renderer.needsFrame();
            if (polls == 1 && renderer.beginRenderPass())
                renderer.endRenderPass();
            opened.push_back(renderer.beginRenderPass(target));
            renderer.endRenderPass();
        });

    AURA_CHECK((opened == std::vector<bool>{true, true, true, true, true}),
               "offscreen: the pass opens every iteration, minimized ones included");
    AURA_CHECK(window.presents == 1, "offscreen: only the iteration with a window pass presents");
    AURA_CHECK(keptFrame, "offscreen: and the window keeps the frame it shows");
    AURA_CHECK(window.waits == 4, "offscreen: an iteration that presents nothing still sleeps");
}

//! What a backend reports through the window that the frame contract depends on.
void checkPresentSideEffects()
{
    ScriptedRenderer scripted;
    test::FakeWindow &window = *scripted.window;
    IRenderer &renderer = scripted.renderer;

    window.flags.surfaceLost = true;
    AURA_CHECK(renderer.beginFrame(), "surface: a frame begins on a window whose surface was replaced");
    renderer.endFrame();
    AURA_CHECK(!window.flags.surfaceLost && !renderer.needsFrame(),
               "surface: the frame that began clears it, so the window can idle again");

    //! Pacing is the window's readyToPresent(), not a wait inside the present: a window that is not ready is asked
    //! for no framebuffer, and a present never blocks.
    window.ready = false;
    const int locks = window.locks;
    AURA_CHECK(!renderer.beginFrame() && window.locks == locks, "pacing: a window that is not ready is not drawn into");
    renderer.endFrame();
    window.ready = true;
    window.presentable = false;
    renderer.beginFrame();
    renderer.endFrame();
    AURA_CHECK(window.swaps == 0, "pacing: a refused framebuffer waits for nothing");
    window.presentable = true;

    window.setVisibility(true);
    AURA_CHECK(!renderer.beginFrame(), "explicit: a minimized window begins no frame");
    const int presents = window.presents;
    renderer.endFrame();
    AURA_CHECK(window.presents == presents, "explicit: and endFrame() after it presents nothing");
    window.setVisibility(false);

    renderer.beginFrame();
    AURA_CHECK(renderer.beginFrame() && window.presents == presents + 1,
               "explicit: a frame left open is presented by the next beginFrame()");
    renderer.endFrame();
}
#endif

} // namespace

int main()
{
#ifdef AURA_HAS_CPU
    checkResizeReachesEveryReader();
    checkNeedsFrameUntilPresented();
    checkFrameBegunSequence();
    checkRunSkipsIdleFrames();
    checkOffscreenOnlyPresentsNothing();
    checkPresentSideEffects();
#else
    std::puts("[SKIP] frame contract: built without the software backend");
#endif
    return test::failureCount() == 0 ? 0 : 1;
}
