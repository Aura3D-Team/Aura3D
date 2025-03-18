#ifndef CPURUNNER_H
#define CPURUNNER_H

#pragma once

#ifdef SDL_WINDOW_MANAGER
#include <AuraWindowManagers/SDLAuraWindowManager/SDLAuraWindowManager.h>
#else
#include <AuraWindowManagers/GlfwWindowManager/GlfwAuraWindowManager.h>
#endif

#include <memory>

#include <CpuAura/CpuFrameBufferManager.h>

namespace aura3d {

class CpuRunner
{
public:
    CpuRunner(WindowDetails windowDetails);

    void run();

private:
#ifdef SDL_WINDOW_MANAGER
    std::unique_ptr<aura3d::SDLAuraWindowManager> _windowManagerApi;
#else
    std::unique_ptr<aura3d::GlfwAuraWindowManager> _windowManagerApi;
#endif

    std::unique_ptr<CpuFrameBufferManager> _cpuFrameBufferManager;
};

}

#endif // CPURUNNER_H
