#include "aura/Renderer/Software/CPURenderer.h"

#include "aura/aura.h"

namespace aura3d {
namespace cpu {

CPURenderer::CPURenderer(const wma::WindowDetails& windowDetails)
    : IRenderer(windowDetails)
{
    INK_INFO << "Renderer - SOFTWARE";
}

CPURenderer::~CPURenderer()
{
    cleanup();
}

void CPURenderer::initialize()
{
    // Empty
}

void CPURenderer::createWindow(const char* title)
{

    _windowManagerApi = wma::createWindowManager(
        wma::WindowBackend::SDL2, _windowDetails, wma::GraphicsAPI::CPU);


    _windowManagerApi->createWindow(title);

    CpuFrameBufferManager::Config cfg = {};
    cfg.width  = _windowDetails.width;
    cfg.height = _windowDetails.height;

    _frameBufferManager = std::make_unique<CpuFrameBufferManager>(
        (SDL_Window*)_windowManagerApi->getWindowInstance(), cfg);

    SDL_SetWindowData(
        (SDL_Window*)_windowManagerApi->getWindowInstance(),
        "CpuFrameBufferManager",
        _frameBufferManager.get());

    _windowManagerApi->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{
        [this](){ cleanup(); }, nullptr });
}

void CPURenderer::handleWindowChanges()
{
    if (!_frameBufferManager) return;
    auto* wd = _windowManagerApi->getWindowDetails();
    _frameBufferManager->resizeFramebuffer(wd->width, wd->height);
}

void CPURenderer::cleanup()
{
    _frameBufferManager.reset();
    _windowManagerApi.reset();
}

}
} // namespace aura3d
