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
    CPURenderer(const wma::WindowDetails& windowDetails);
    virtual ~CPURenderer();

    void initialize(aura3d::AuraSettings* settings) override;
    void handleWindowChanges() override;
    void cleanup() override;

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices) override;
    IndexBufferHandle  createIndexBuffer(std::vector<u16>&& indices) override;
    IndexBufferHandle  createIndexBuffer(std::vector<u32>&& indices) override;
    TextureHandle      createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255) override;

    void beginFrame() override;
    void beginRenderPass() override;
    void endRenderPass() override;
    void endFrame() override;

    void setTransform(const gfx::TransformUBO& ubo) override;
    void bindVertexBuffer(VertexBufferHandle handle) override;
    void bindIndexBuffer(IndexBufferHandle handle) override;
    void bindTexture(TextureHandle handle) override;
    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;

    wma::IWindowManager*    getWindowManager()      override { return _windowManagerApi.get(); }
    RendererChoice          getBackendType() const   override { return RendererChoice::SOFTWARE; }
    CpuFrameBufferManager*  getFrameBufferManager()          { return _frameBufferManager.get(); }

protected:
    void createWindow(const char* title, const wma::WindowBackend& wBackend) override;

private:
    std::unique_ptr<wma::IWindowManager>   _windowManagerApi;
    std::unique_ptr<CpuFrameBufferManager> _frameBufferManager;

    u32 _clearColorU32 = 0;
    std::vector<std::vector<gfx::Vertex3D>> _vertexBufferPool3d;
    std::vector<std::vector<u32>> _indexBufferPool;
    std::vector<std::vector<cpu::Texture>> _texturePool;

    VertexBufferHandle _boundVertexBuffer = INVALID_HANDLE;
    IndexBufferHandle _boundIndexBuffer = INVALID_HANDLE;
    TextureHandle _boundTexture = INVALID_HANDLE;
    gfx::TransformUBO  _currentTransform;

    ink::ThreadPool _workerPool;
};

} // namespace cpu
} // namespace aura3d

#endif // CPU_RENDERER_H
