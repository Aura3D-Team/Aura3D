// The golden scene on the software backend, headless, against tests/assets/golden/scene.ppm.
// GPU backends shade PBR, so they are held to scene-gpu.ppm (written by lavapipe) instead.
// AURA_GOLDEN_UPDATE=1 rewrites the reference; GPU backends are compared to the same file by
// `benchmark_runtime <backend> golden`, which needs a display and so is not run by ctest.
#include <cstdlib>
#include <string>
#include <vector>

#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/JobSystem/JobSystem.h"
#ifdef AURA_HAS_CPU
#include "aura/Renderer/Software/CPURenderer.h"
#endif

#include "GoldenScene.h"
#include "TestUtils.h"
#include "WindowTestUtils.h"

using namespace aura3d;

int main()
{
#ifdef AURA_HAS_CPU
    const std::string path = std::string(AURA_TEST_ASSETS_DIR) + "/golden/scene.ppm";
    JobSystem jobs{4};
    cpu::CPURenderer renderer(wma::WindowDetails{.width = 16, .height = 16});
    renderer.setWindowFactory(
        [](auto, const auto &, auto)
        {
            return std::make_unique<test::FakeWindow>();
        });
    renderer.initialize(AuraSettings::get(), &jobs);
    Camera::setClipSpace(Camera::ClipSpace::OpenGL);

    const test::GoldenScene scene = test::buildGoldenScene(renderer);
    AURA_CHECK(isValidHandle(scene.frame) && isValidHandle(scene.inset), "golden: the targets are created");
    test::drawGoldenScene(renderer, scene);
    std::vector<u8> rgba(static_cast<usize>(test::kGoldenSize) * test::kGoldenSize * 4);
    AURA_CHECK(renderer.readPixels(scene.frame, rgba), "golden: the frame reads back");

    if (std::getenv("AURA_GOLDEN_UPDATE"))
    {
        AURA_CHECK(test::writeGolden(path, rgba, test::kGoldenSize, test::kGoldenSize), "golden: reference rewritten");
        return aura3d::test::failureCount() == 0 ? 0 : 1;
    }
    const std::vector<u8> golden = test::readGolden(path, test::kGoldenSize, test::kGoldenSize);
    AURA_CHECK(!golden.empty(), "golden: reference exists (AURA_GOLDEN_UPDATE=1 writes it)");
    //! The software backend produced the reference, so it must match it exactly.
    const test::GoldenDiff diff = test::compareGolden(rgba, golden, 0);
    AURA_CHECK(diff.differing == 0, "golden: software output matches the reference (" + std::to_string(diff.differing) +
                                        " pixels differ, worst " + std::to_string(diff.worst) + ")");

    test::drawGoldenScene(renderer, scene, /*instanced=*/true);
    AURA_CHECK(renderer.readPixels(scene.frame, rgba) && test::compareGolden(rgba, golden, 0).differing == 0,
               "golden: drawMeshInstanced() draws exactly what drawMeshes() does");
    return aura3d::test::failureCount() == 0 ? 0 : 1;
#else
    std::puts("[SKIP] golden: built without the software backend");
    return 0;
#endif
}
