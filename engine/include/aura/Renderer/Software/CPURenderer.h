#ifndef CPU_RENDERER_H
#define CPU_RENDERER_H

#pragma once

#include <memory>

#include "CpuAura/ClipGeometry.h"
#include "CpuAura/CpuFrameBufferManager.h"
#include "aura/Renderer/IRenderer.h"

namespace aura3d
{
namespace cpu
{

class CPURenderer : public IRenderer
{
  public:
    CPURenderer(const wma::WindowDetails &windowDetails);
    virtual ~CPURenderer();

    void initialize(aura3d::AuraSettings *settings, const JobSystem *jobs) override;
    void handleWindowChanges() override;
    void cleanup() override;

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D> &&vertices) override;
    bool updateVertexBuffer(VertexBufferHandle handle, std::span<const gfx::Vertex3D> vertices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u16> &&indices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u32> &&indices) override;
    TextureHandle createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height) override;
    TextureHandle createTexture(const TextureDesc &desc, std::span<const std::byte> pixels) override;
    TextureHandle createDynamicTexture(u32 width, u32 height) override;
    TextureHandle createCoverageTexture(u32 width, u32 height) override;
    void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels) override;
    void updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                     const u8 *coverage) override;

    void endRenderPass() override;
    RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc) override;
    bool resizeRenderTarget(RenderTargetHandle target, u32 width, u32 height) override;
    [[nodiscard]] glm::uvec2 targetSize(RenderTargetHandle target) const override;
    [[nodiscard]] TextureHandle renderTargetTexture(RenderTargetHandle target) const override;
    bool readPixels(RenderTargetHandle target, std::span<u8> rgba) override;

    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;
    using IRenderer::drawBatch;
    void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices, TextureHandle texture,
                   gfx::BatchSpace space = gfx::BatchSpace::Screen) override;
    [[nodiscard]] glm::uvec2 renderTargetSize() const noexcept override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;

    //! Lambert and the directional light only: no PBR, and instancing loops through drawMeshes(). Compute runs
    //! ComputeDesc::cpu kernels on the job system.
    [[nodiscard]] RendererCaps caps() const noexcept override;
    BufferHandle createBuffer(usize bytes, std::span<const std::byte> initial = {}) override;
    bool updateBuffer(BufferHandle buffer, usize offset, std::span<const std::byte> bytes) override;
    bool readBuffer(BufferHandle buffer, usize offset, std::span<std::byte> out) override;
    ComputeHandle createCompute(const ComputeDesc &desc) override;
    void dispatch(ComputeHandle kernel, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                  std::span<const std::byte> constants = {}) override;
    void drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                   BufferHandle arguments, usize argumentOffset = 0) override;
    RendererChoice getBackendType() const override
    {
        return RendererChoice::SOFTWARE;
    }
    CpuFrameBufferManager *getFrameBufferManager()
    {
        return _frameBufferManager.get();
    }

  protected:
    bool acquireFrame() override;
    bool presentFrame() override;
    void openRenderPass() override;
    void openRenderPass(RenderTargetHandle target) override;
    void createWindow(const char *title, const wma::WindowBackend &wBackend) override;

  private:
    //! Resolves a 1-based texture handle to its base mip, or nullptr.
    const Texture *_resolveTexture(TextureHandle handle) const;

  private:
    std::unique_ptr<CpuFrameBufferManager> _frameBufferManager;

    struct RenderTarget
    {
        std::unique_ptr<CpuFrameBufferManager> frame;
        TextureHandle colour;
        bool srgb = false;
        bool depth = false;
    };
    std::vector<RenderTarget> _renderTargets; //! handle - 1 == index
    //! Where draws land: the window's framebuffer, or the open pass's target.
    CpuFrameBufferManager *_activeFrame = nullptr;
    u32 _passTarget = 0;

    //! Set once, in initialize(), before createWindow() builds
    //! _frameBufferManager from it. Non-owning: Engine's JobSystem outlives
    //! every renderer, including this one across a backend switch.
    const JobSystem *_jobs = nullptr;

    u32 _clearColorU32 = 0;
    std::vector<std::vector<std::byte>> _buffers; //! handle - 1 == index
    //! @p handle's bytes, or nullptr.
    [[nodiscard]] std::vector<std::byte> *bufferOf(BufferHandle handle) noexcept;
    std::vector<ComputeDesc> _kernels; //! handle - 1 == index; only buffers and cpu are used
    std::vector<std::vector<gfx::Vertex3D>> _vertexBufferPool3d;
    std::vector<std::vector<u32>> _indexBufferPool;
    std::vector<std::vector<cpu::Texture>> _texturePool;

    /// One transformed and lit vertex of the bound buffer, tagged with the
    /// draw that produced it.
    struct CachedVertex
    {
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
};

} // namespace cpu
} // namespace aura3d

#endif // CPU_RENDERER_H
