#include "CpuRunner.h"

#include <aura.hpp>

#include <plog/Log.h>

namespace aura3d {

CpuRunner::CpuRunner(WindowDetails windowDetails) {
#ifdef SDL_WINDOW_MANAGER
    _windowManagerApi = std::make_unique<aura3d::SDLAuraWindowManager>(windowDetails);
#else
    _windowManagerApi = std::make_unique<aura3d::GlfwAuraWindowManager>(windowDetails);
#endif

    aura3d::CpuFrameBufferManager::Config frameBufferSettings = {};

    _frameBufferManager = std::make_unique<aura3d::CpuFrameBufferManager>(frameBufferSettings);
}

void CpuRunner::run()
{
    _windowManagerApi->createWindow(APPLICATION_NAME);

    _windowManagerApi->process([&]() {

    });
}

}
