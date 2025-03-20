#include "GlRunner.h"

#include <aura.hpp>

#include "AuraLogger/AuraLogger.h"

namespace aura3d {

GlRunner::GlRunner(WindowDetails windowDetails) {
#ifdef SDL_WINDOW_MANAGER
    _windowManagerApi = std::make_unique<aura3d::SDLAuraWindowManager>(windowDetails);
#else
    _windowManagerApi = std::make_unique<aura3d::GlfwAuraWindowManager>(windowDetails);
#endif
}

void GlRunner::run()
{
    _windowManagerApi->createWindow(APPLICATION_NAME);

    _windowManagerApi->process([&]() {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    });
}

}
