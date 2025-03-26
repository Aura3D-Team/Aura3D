#ifndef CPU_RENDERER_H
#define CPU_RENDERER_H

#pragma once

#ifdef SDL_WINDOW_MANAGER
#include <AuraWindowManagers/SDLAuraWindowManager/SDLAuraWindowManager.h>
#else
#include <AuraWindowManagers/GlfwWindowManager/GlfwAuraWindowManager.h>
#endif

#include <memory>
#include <nlohmann/json.hpp>

#include "Renderers/Renderer.h"
#include <CpuAura/CpuFrameBufferManager.h>

namespace aura3d {

/**
 * @brief CPU-based software implementation of the Renderer interface
 */
class CPURenderer : public Renderer {
public:
    /**
     * @brief Construct a CPU-based renderer
     *
     * @param windowDetails Window configuration
     */
    CPURenderer(const WindowDetails& windowDetails);

    /**
     * @brief Destroy the CPU renderer
     */
    virtual ~CPURenderer();

    /**
     * @brief Initialize the CPU renderer
     */
    void initialize() override;

    /**
     * @brief Run the main rendering loop
     */
    void run() override;

    /**
     * @brief Handle window changes (resize, etc.)
     */
    void handleWindowChanges() override;

    /**
     * @brief Clean up renderer resources
     */
    void cleanup() override;

protected:
    /**
     * @brief Create the window
     *
     * @param title Window title
     */
    void createWindow(const char* title) override;

private:
#ifdef SDL_WINDOW_MANAGER
    std::unique_ptr<aura3d::SDLAuraWindowManager> _windowManagerApi;
#else
    std::unique_ptr<aura3d::GlfwAuraWindowManager> _windowManagerApi;
#endif
    std::unique_ptr<CpuFrameBufferManager> _frameBufferManager;

    bool _isInitialized = false;
};

} // namespace aura3d

#endif // CPU_RENDERER_H
