#include "aura/Renderer/IRenderer.h"

namespace aura3d {

void IRenderer::run(std::function<void()> onFrame)
{
    auto* windowMgr = getWindowManager();
    if (!windowMgr) return;

    auto runCleanup = [this]() { cleanup(); };
    windowMgr->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{ runCleanup, nullptr });

    _running = true;
    windowMgr->process([&]() {
        beginFrame();
        onFrame();
        endFrame();
    });
}

} // namespace aura3d