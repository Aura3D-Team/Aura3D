#ifndef CPU_RENDERER_H
#define CPU_RENDERER_H

#pragma once

#include <memory>

#include "aura/Renderer/IRenderer.h"
#include "CpuAura/CpuFrameBufferManager.h"

namespace aura3d {
namespace cpu {

/**
 * @brief CPU-based software renderer.
 *
 * initialize() creates the SDL2 window and the CpuFrameBufferManager.
 * All drawing calls and the per-frame loop are the responsibility of the
 * consuming application (Sandbox).
 */
class CPURenderer : public IRenderer {
public:
    CPURenderer(const wma::WindowDetails& windowDetails);
    virtual ~CPURenderer();

    /**
     * @brief Create the window and framebuffer.
     */
    void initialize() override;

    /**
     * @brief Resize the framebuffer after a window resize.
     */
    void handleWindowChanges() override;

    /**
     * @brief Release all resources.
     */
    void cleanup() override;

    wma::IWindowManager*    getWindowManager()    override { return _windowManagerApi.get(); }
    CpuFrameBufferManager*  getFrameBufferManager()        { return _frameBufferManager.get(); }

protected:
    void createWindow(const char* title) override;

private:
    std::unique_ptr<wma::IWindowManager>   _windowManagerApi;
    std::unique_ptr<CpuFrameBufferManager> _frameBufferManager;
};

}
} // namespace aura3d

#endif // CPU_RENDERER_H
