#ifndef CPU_RENDERER_H
#define CPU_RENDERER_H

#pragma once

#include <memory>

#include "aura/Renderer/IRenderer.h"
#include "CpuAura/ClipGeometry.h"
#include "CpuAura/CpuFrameBufferManager.h"

namespace aura3d {
namespace cpu {

class CPURenderer : public IRenderer {
public:
    CPURenderer(const wma::WindowDetails& windowDetails);
    virtual ~CPURenderer();

    void initialize(aura3d::AuraSettings* settings, const JobSystem* jobs) override;
    void handleWindowChanges() override;
    void cleanup() override;

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u16>&& indices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u32>&& indices) override;
    TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255) override;
    TextureHandle createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height) override;
    TextureHandle createDynamicTexture(u32 width, u32 height) override;
    void updateTextureRegion(TextureHandle handle, u32 x, u32 y,
                             u32 width, u32 height, const u8* rgbaPixels) override;

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
    void drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                     std::span<const u32> indices,
                     TextureHandle texture) override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;

    wma::IWindowManager* getWindowManager()      override { return _windowManagerApi.get(); }
    RendererChoice getBackendType() const   override { return RendererChoice::SOFTWARE; }
    CpuFrameBufferManager* getFrameBufferManager()          { return _frameBufferManager.get(); }

protected:
    void createWindow(const char* title, const wma::WindowBackend& wBackend) override;

private:
    //! Resolves a 1-based texture handle to its base mip, or nullptr.
    const Texture* _resolveTexture(TextureHandle handle) const;

private:
    std::unique_ptr<wma::IWindowManager>   _windowManagerApi;
    std::unique_ptr<CpuFrameBufferManager> _frameBufferManager;

    //! Set once, in initialize(), before createWindow() builds
    //! _frameBufferManager from it. Non-owning: Engine's JobSystem outlives
    //! every renderer, including this one across a backend switch.
    const JobSystem* _jobs = nullptr;

    u32 _clearColorU32 = 0;
    std::vector<std::vector<gfx::Vertex3D>> _vertexBufferPool3d;
    std::vector<std::vector<u32>> _indexBufferPool;
    std::vector<std::vector<cpu::Texture>> _texturePool;

    /// One transformed and lit vertex of the bound buffer, tagged with the
    /// draw that produced it.
    struct CachedVertex {
        ClipVertex vertex{};
        /// Equal to _drawStamp when `vertex` is current for this draw call.
        u64 stamp = 0;
    };
    /// Indexed by vertex index. An indexed mesh references each vertex
    /// several times; stamping lets drawIndexed() transform it once per draw
    /// without clearing the whole array between draws.
    std::vector<CachedVertex> _transformed;
    /// Incremented per drawIndexed(); a wrap resets every stamp first.
    u64 _drawStamp = 0;

    VertexBufferHandle _boundVertexBuffer;
    IndexBufferHandle _boundIndexBuffer;
    TextureHandle _boundTexture;
};

} // namespace cpu
} // namespace aura3d

#endif // CPU_RENDERER_H
