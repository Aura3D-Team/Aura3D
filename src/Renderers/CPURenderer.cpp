#include "Renderers/CPURenderer.h"

#include <ink/ink.hpp>
#include <cmath>
#include <ctime>

#include <wma/wma.hpp>

#include "aura.hpp"
#include "Utils/ColorsDefinitions.h"
#include "Utils/AuraUtils.h"

namespace aura3d {

CPURenderer::CPURenderer(const wma::WindowDetails& windowDetails)
    : Renderer(windowDetails)
{
    // Constructor only stores parameters - initialization happens in initialize()
}

CPURenderer::~CPURenderer()
{
    cleanup();
}

void CPURenderer::initialize()
{
    if (_isInitialized) {
        return;
    }

    wma::WindowBackend windowBackend = wma::WindowBackend::SDL2;

    INK_ASSERT_MSG(windowBackend == wma::WindowBackend::SDL2, "CPURenderer needs WindowBackend to be SDL2");

    _windowManagerApi = wma::createWindowManager(
        windowBackend,
        _windowDetails,
        wma::GraphicsAPI::CPU
    );

    // Create window and framebuffer
    createWindow(APPLICATION_NAME);

    _isInitialized = true;
}

void CPURenderer::createWindow(const char* title)
{
    _windowManagerApi->createWindow(title);

    // Create framebuffer manager after window creation
    CpuFrameBufferManager::Config frameBufferSettings = {};
    frameBufferSettings.width = _windowDetails.width;
    frameBufferSettings.height = _windowDetails.height;

    _frameBufferManager = std::make_unique<aura3d::CpuFrameBufferManager>(
        (SDL_Window*)_windowManagerApi->getWindowInstance(),
        frameBufferSettings
    );

    // Store framebuffer in window data for access in SDL event handlers
    SDL_SetWindowData(
        (SDL_Window*)_windowManagerApi->getWindowInstance(),
        "CpuFrameBufferManager",
        _frameBufferManager.get()
    );
}

void CPURenderer::run()
{
    if (!_isInitialized) {
        initialize();
    }

    auto window = _windowManagerApi->getWindowInstance();
    auto windowDetails = _windowManagerApi->getWindowDetails();
    auto windowFLags = _windowManagerApi->getWindowFlags();

    const int centerX = windowDetails->width / 2;
    const int centerY = windowDetails->height / 2;

    // Main render loop
    _windowManagerApi->process([&]() {
        auto windowDetails = _windowManagerApi->getWindowDetails();
        auto windowFlags = _windowManagerApi->getWindowFlags();

        // Handle window resize
        if (windowFlags->resized) {
            _frameBufferManager->resizeFramebuffer(windowDetails->width, windowDetails->height);
            windowFlags->resized = false;
        }

        // Clear the framebuffer with a base color
        _frameBufferManager->clear(aura3d::colors::CORNSILK_UINT32);

        int width = _frameBufferManager->getWidth();
        int height = _frameBufferManager->getHeight();

        int textY = 20;
        _frameBufferManager->drawText("Testando texto: .", {10, textY}, aura3d::colors::RED_UINT32);
        int textHeight = _frameBufferManager->getTextHeight("Testando texto: ~çã");
        textY += 10 + textHeight;

        // Total number of tracked positions
        std::string posCountText = "Tamanho da fonte";
        _frameBufferManager->drawText(posCountText, {10, textY}, aura3d::colors::RED_UINT32);
        textY += 10 + textHeight;

        _frameBufferManager->drawLine({250, 250}, {400, 400}, aura3d::colors::BLACK_UINT32);

        // Render the final framebuffer
        _frameBufferManager->renderFramebuffer();
    });

    // std::tm tmPrev = {}, tmCurrent = {};
    // std::istringstream ssPrev(startTime);
    // std::istringstream ssCurrent(endTime);
    // ssPrev >> std::get_time(&tmPrev, "%d/%m/%Y %H:%M:%S");
    // ssCurrent >> std::get_time(&tmCurrent, "%d/%m/%Y %H:%M:%S");
    // std::time_t timePrev = std::mktime(&tmPrev);
    // std::time_t timeCurrent = std::mktime(&tmCurrent);
    // f64 seconds = std::difftime(timeCurrent, timePrev);

}

void CPURenderer::handleWindowChanges()
{
    if (!_isInitialized || !_frameBufferManager) {
        return;
    }

    auto windowDetails = _windowManagerApi->getWindowDetails();

    // Resize the framebuffer to match the window
    _frameBufferManager->resizeFramebuffer(windowDetails->width, windowDetails->height);
}

void CPURenderer::cleanup()
{
    if (!_isInitialized) {
        return;
    }

    // Clean up resources
    _frameBufferManager.reset();
    _windowManagerApi.reset();

    _isInitialized = false;
}

} // namespace aura3d
