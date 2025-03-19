#include "CpuRunner.h"

#include "aura.hpp"
#include "Utils/ColorsDefinitions.h"
#include "AuraLogger/AuraLogger.h"

namespace aura3d {

CpuRunner::CpuRunner(WindowDetails windowDetails)
{
#ifdef SDL_WINDOW_MANAGER
    _windowManagerApi = std::make_unique<aura3d::SDLAuraWindowManager>(windowDetails);
#else
    _windowManagerApi = std::make_unique<aura3d::GlfwAuraWindowManager>(windowDetails);
#endif

    _windowManagerApi->createWindow(APPLICATION_NAME);

    aura3d::CpuFrameBufferManager::Config frameBufferSettings = {};
    frameBufferSettings.width = windowDetails.width;
    frameBufferSettings.height = windowDetails.height;

    _frameBufferManager = std::make_unique<aura3d::CpuFrameBufferManager>(_windowManagerApi->getWindowInstance(), frameBufferSettings);
}

CpuRunner::~CpuRunner()
{
    // Empty
}

void CpuRunner::run()
{
    auto window = _windowManagerApi->getWindowInstance();
    auto windowDetails = _windowManagerApi->getWindowDetails();

    SDL_SetWindowData(window, "CpuFrameBufferManager", _frameBufferManager.get());

    const int centerX = windowDetails->width / 2;
    const int centerY = windowDetails->height / 2;

    _windowManagerApi->process([&]() {
        // Clear the framebuffer
        _frameBufferManager->clear(aura3d::colors::CORNSILK_UINT32); // Black with full alpha

        // Draw some circles
        // _frameBufferManager->drawCircle(centerX, centerY, 100, aura3d::colors::CORNSILK_UINT32); // Red
        // _frameBufferManager->drawCircle(centerX, centerY, 80, aura3d::colors::GREEN_WEB_UINT32);  // Green
        // _frameBufferManager->drawCircle(centerX, centerY, 60, aura3d::colors::BLUE_UINT32);  // Blue


        // _frameBufferManager->drawRect(centerX, centerY, 100, 80, aura3d::colors::BLUE_UINT32);

        // _frameBufferManager->drawTriangle(
        //     centerX + 150, centerY + 50,
        //     centerX, centerY - 100,
        //     centerX + 250, centerY - 200,
        //     aura3d::colors::DARK_VIOLET_UINT32
        // );

        _frameBufferManager->drawRect(centerX-150, centerY-150, 300, 300, aura3d::colors::BLUE_UINT32);
        // _frameBufferManager->drawFillRect(centerX-150, centerY-150, 300, 300, aura3d::colors::BLUE_UINT32);

        // _frameBufferManager->drawRoundedRect(
        //     centerX - 75, centerY + 100,
        //     150, 80, 20,
        //     aura3d::colors::CYAN_UINT32
        // );

        // for (int i = 0; i < windowDetails->width; i += 50) {
        //     _frameBufferManager->drawLine(i, 0, i, windowDetails->height, aura3d::colors::BLACK_UINT32);
        // }

        // for (int j = 0; j < windowDetails->height; j += 50) {
        //     _frameBufferManager->drawAALine(0, j, windowDetails->width, j, aura3d::colors::BLACK_UINT32);
        // }

        _frameBufferManager->renderFramebuffer();
    });
}

}
