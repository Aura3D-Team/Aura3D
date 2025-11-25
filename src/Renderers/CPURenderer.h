#ifndef CPU_RENDERER_H
#define CPU_RENDERER_H

#pragma once

#include <memory>

#include "Renderers/Renderer.h"
#include "CpuAura/CpuFrameBufferManager.h"

namespace aura3d {
namespace cpu {

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
    CPURenderer(const wma::WindowDetails& windowDetails);

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
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;

    std::unique_ptr<CpuFrameBufferManager> _frameBufferManager;

    bool _isInitialized = false;
};

}
} // namespace aura3d

#endif // CPU_RENDERER_H
