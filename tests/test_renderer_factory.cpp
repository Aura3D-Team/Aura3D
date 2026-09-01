/*
 * Backend selection and the per-backend properties that hang off it.
 *
 * These are queries, not constructions: everything here answers a question
 * about a backend without needing a window, a driver or a GPU, which is what
 * makes them testable at all.
 *
 * The clip-space mapping is the reason this suite exists. A projection built
 * for the wrong depth range does not fail, throw or warn -- the scene renders,
 * and then z-fights against its own rasteriser, which reads as a driver or
 * model problem rather than as a one-line configuration mistake. Nothing else
 * in the tree would catch it, so it is pinned here.
 */

#include "aura/Core/Camera/Camera.h"
#include "aura/Renderer/RendererFactory.h"

#include "TestUtils.h"

using namespace aura3d;

namespace {

void testClipSpaceMapping()
{
    // Vulkan and Metal rasterise a [0,1] depth range; OpenGL and the software
    // rasteriser use [-1,1]. Camera picks its glm builder (_ZO vs _NO) from
    // this, so the two must agree with what the backend actually does.
    AURA_CHECK(RendererFactory::clipSpaceFor(RendererChoice::VULKAN) == Camera::ClipSpace::Vulkan,
               "Vulkan renders a [0,1] depth range");
    AURA_CHECK(RendererFactory::clipSpaceFor(RendererChoice::METAL) == Camera::ClipSpace::Vulkan,
               "Metal renders a [0,1] depth range");
    AURA_CHECK(RendererFactory::clipSpaceFor(RendererChoice::OPENGL) == Camera::ClipSpace::OpenGL,
               "OpenGL renders a [-1,1] depth range");
    AURA_CHECK(RendererFactory::clipSpaceFor(RendererChoice::SOFTWARE) == Camera::ClipSpace::OpenGL,
               "the software rasteriser renders a [-1,1] depth range");
}

void testResolveLandsOnSomethingAvailable()
{
    // Whatever is asked for, resolve() has to answer with a backend this build
    // actually contains -- that promise is what lets create() degrade instead
    // of throwing, and what lets a settings.json written for one platform run
    // on another.
    constexpr RendererChoice kAll[] = {RendererChoice::VULKAN, RendererChoice::METAL,
                                       RendererChoice::OPENGL, RendererChoice::SOFTWARE};

    for (const RendererChoice choice : kAll)
    {
        const RendererChoice resolved = RendererFactory::resolve(choice);
        if (!RendererFactory::isAvailable(resolved))
        {
            AURA_CHECK(false, "resolve() always lands on an available backend");
            return;
        }
    }
    AURA_CHECK(true, "resolve() always lands on an available backend");

    // An available backend is never degraded away from: asking for what you
    // have must give you exactly that.
    for (const RendererChoice choice : kAll)
    {
        if (RendererFactory::isAvailable(choice) && RendererFactory::resolve(choice) != choice)
        {
            AURA_CHECK(false, "resolve() leaves an available backend alone");
            return;
        }
    }
    AURA_CHECK(true, "resolve() leaves an available backend alone");
}

void testDefaultChoiceIsUsable()
{
    const RendererChoice fallback = RendererFactory::defaultChoice();

    AURA_CHECK(RendererFactory::isAvailable(fallback),
               "the default backend is one this build compiled in");
    AURA_CHECK(RendererFactory::resolve(fallback) == fallback,
               "the default backend resolves to itself");
}

} // namespace

int main()
{
    testClipSpaceMapping();
    testResolveLandsOnSomethingAvailable();
    testDefaultChoiceIsUsable();

    AURA_TEST_MAIN_RETURN();
}
