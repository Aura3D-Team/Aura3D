#ifndef CPU_RENDERER_H
#define CPU_RENDERER_H

#pragma once

#include <memory>

#include "aura/Renderer/IRenderer.h"
#include "CpuAura/CpuFrameBufferManager.h"

namespace aura3d {
namespace cpu {

class CPURenderer : public IRenderer {
public:
    CPURenderer(const wma::WindowDetails& windowDetails, RendererMode mode);
    virtual ~CPURenderer();

    void initialize() override;
    void handleWindowChanges() override;
    void cleanup() override;

    wma::IWindowManager*    getWindowManager()      override { return _windowManagerApi.get(); }
    RendererChoice          getBackendType() const   override { return RendererChoice::SOFTWARE; }
    CpuFrameBufferManager*  getFrameBufferManager()          { return _frameBufferManager.get(); }

protected:
    void createWindow(const char* title) override;

private:
    std::unique_ptr<wma::IWindowManager>   _windowManagerApi;
    std::unique_ptr<CpuFrameBufferManager> _frameBufferManager;
};

}
} // namespace aura3d

#endif // CPU_RENDERER_H
