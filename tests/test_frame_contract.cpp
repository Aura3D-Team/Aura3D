// The frame contract IRenderer::run() and its backends keep: what a resize, an idle frame or a dropped present
// does, and who gets to see it. Driven headless through the software backend and a scripted window.
#include <cstdio>
#include <memory>
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

    window.resize(32, 32);
    AURA_CHECK(renderer.needsFrame(), "resize: an unapplied resize needs a frame");
    renderer.beginFrame();
    renderer.endFrame();
    AURA_CHECK(!renderer.needsFrame(), "resize: and stops needing one once a frame presented it");
}
#endif

} // namespace

int main()
{
#ifdef AURA_HAS_CPU
    checkResizeReachesEveryReader();
#else
    std::puts("[SKIP] frame contract: built without the software backend");
#endif
    return test::failureCount() == 0 ? 0 : 1;
}
