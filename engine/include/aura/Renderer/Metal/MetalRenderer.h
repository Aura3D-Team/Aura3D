#ifndef METAL_RENDERER_H
#define METAL_RENDERER_H

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <semaphore>
#include <span>

#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"
#include "aura/Renderer/Metal/MtlAura/MtlBufferManager/MtlBufferManager.h"
#include "aura/Renderer/Metal/MtlAura/MtlDeviceManager/MtlDeviceManager.h"
#include "aura/Renderer/Metal/MtlAura/MtlDrawableManager/MtlDrawableManager.h"
#include "aura/Renderer/Metal/MtlAura/MtlIndexBufferManager/MtlIndexBufferManager.h"
#include "aura/Renderer/Metal/MtlAura/MtlLayerManager/MtlLayerManager.h"
#include "aura/Renderer/Metal/MtlAura/MtlPipelineManager/MtlPipelineManager.h"
#include "aura/Renderer/Metal/MtlAura/MtlShaderLibraryManager/MtlShaderLibraryManager.h"
#include "aura/Renderer/Metal/MtlAura/MtlTextureManager/MtlTextureManager.h"
#include "aura/Renderer/Metal/MtlAura/MtlVertexBufferManager/MtlVertexBufferManager.h"

namespace aura3d {
namespace mtl {

/**
 * @class MetalRenderer
 * @brief Native Metal implementation of aura3d::IRenderer, for macOS and iOS.
 *
 * The fourth backend alongside Vulkan, OpenGL and the software rasteriser, and
 * the only one available on Apple platforms -- Apple's OpenGL is deprecated and
 * capped at 4.1, and Vulkan exists there only through the MoltenVK translation
 * layer, which this backend deliberately does not use.
 *
 * ## Frame shape
 *
 * @code
 *   beginFrame()        acquire a drawable, start this frame's command buffer
 *     beginRenderPass() open the render encoder over drawable + depth
 *       draw*()         encode
 *     endRenderPass()   close the encoder
 *   endFrame()          present, commit, advance the frame slot
 * @endcode
 *
 * A frame whose drawable could not be acquired (window minimised or off-screen)
 * encodes nothing and presents nothing; every draw entry point tolerates being
 * called during such a frame, so the application's own loop needs no special
 * case for it.
 *
 * ## Where this differs from the Vulkan backend
 *
 * Much of what VulkanRenderer carries has no counterpart here, and the absences
 * are the design rather than an omission:
 *
 *  - No descriptor sets, no descriptor pool, no bindless texture table. Metal
 *    binds a texture to a fragment argument slot directly, so bindTexture() is
 *    one call and the pool-exhaustion problem that shaped VulkanRenderer's
 *    texture path does not exist.
 *  - No uniform buffers. Per-draw transforms and the light block travel through
 *    setVertexBytes()/setFragmentBytes(), Metal's push-constant equivalent, whose
 *    4 KB budget is roomy enough for all of them.
 *  - No swapchain to recreate and no render pass or framebuffer objects. A
 *    CAMetalLayer resizes with one property write, and a render pass is a
 *    descriptor built per frame.
 *  - No multithreaded command recording. drawMeshes() is inherited from
 *    IRenderer, which records the batch serially; Metal's equivalent of
 *    VulkanRenderer's secondary-buffer fan-out is MTLParallelRenderCommandEncoder,
 *    which is not wired up here.
 *
 * ## Threading
 *
 * Single-threaded, like the OpenGL and software backends: every entry point must
 * be called from the thread that called initialize().
 */
class MetalRenderer : public IRenderer {
public:
    MetalRenderer(const wma::WindowDetails& windowDetails);
    virtual ~MetalRenderer();

    void initialize(AuraSettings* settings, const JobSystem* jobs) override;
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
    /*
     * setLight() is deliberately not overridden. The base implementation records
     * the light in IRenderer::_light, and that is already where bindDrawState()
     * reads it from to push the bytes at each draw -- there is no GPU-side buffer
     * to keep in step, so an override would have nothing to add.
     */
    void bindVertexBuffer(VertexBufferHandle handle) override;
    void bindIndexBuffer(IndexBufferHandle handle) override;
    void bindTexture(TextureHandle handle) override;
    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;
    void drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                     std::span<const u32> indices,
                     TextureHandle texture) override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;

    wma::IWindowManager* getWindowManager() override { return _windowManagerApi.get(); }
    RendererChoice getBackendType() const override { return RendererChoice::METAL; }

    MtlDeviceManager* getDeviceManager() { return _deviceManager.get(); }
    MtlLayerManager* getLayerManager() { return _layerManager.get(); }
    MtlDrawableManager* getDrawableManager() { return _drawableManager.get(); }
    MtlVertexBufferManager* getVertexBufferManager() { return _vertexManager.get(); }
    MtlIndexBufferManager* getIndexBufferManager() { return _indexManager.get(); }
    MtlTextureManager* getTextureManager() { return _textureManager.get(); }

protected:
    void createWindow(const char* title, const wma::WindowBackend& wBackend) override;

private:
    //! Binds ESC-to-quit and the rest of the shared input wiring.
    void setupInput();

    //! Builds the scene and overlay pipelines against the layer's formats.
    void createPipelines();

    /**
     * @brief Applies the per-draw state both drawIndexed() and draw() need:
     *        transform bytes, light bytes and the bound texture.
     *
     * The single place that state is written, so the two draw entry points
     * cannot drift apart -- the role VulkanRenderer::bindDrawState() plays there.
     */
    void bindDrawState();

    /**
     * @brief Grows this frame's overlay vertex/index buffers to fit a batch.
     *
     * There is one pair per frame in flight so that writing this frame's geometry
     * cannot scribble over a batch the GPU is still reading. They only ever grow,
     * so a steady-state overlay stops allocating after the first few frames --
     * the same arrangement as VulkanRenderer::ensureOverlay2DCapacity().
     *
     * @return false when either allocation failed, in which case the batch is
     *         skipped rather than drawn from a buffer that is too small.
     */
    [[nodiscard]] bool ensureOverlay2DCapacity(size_t vertexBytes, size_t indexBytes);

    /**
     * @brief The texture a draw should sample, substituting the fallback when
     *        nothing usable is bound.
     *
     * IRenderer lets a caller draw with no texture and lets drawBatch2D() be
     * handed INVALID_HANDLE, but the shaders always sample. A 1x1 opaque white
     * texel is substituted in those cases, so vertex colour comes through
     * unchanged -- the same fallback the OpenGL and Vulkan paths use.
     */
    [[nodiscard]] MTL::Texture* resolveSampledTexture(TextureHandle handle);

    std::unique_ptr<wma::IWindowManager> _windowManagerApi;

    std::unique_ptr<MtlDeviceManager> _deviceManager;
    std::unique_ptr<MtlLayerManager> _layerManager;
    std::unique_ptr<MtlDrawableManager> _drawableManager;
    std::unique_ptr<MtlShaderLibraryManager> _shaderLibrary;
    std::unique_ptr<MtlBufferManager> _bufferManager;
    std::unique_ptr<MtlVertexBufferManager> _vertexManager;
    std::unique_ptr<MtlIndexBufferManager> _indexManager;
    std::unique_ptr<MtlTextureManager> _textureManager;

    //! The lit 3D scene pipeline: depth-tested, back-face culled, no blending.
    std::unique_ptr<MtlPipelineManager> _scenePipeline;
    //! The unlit 2D overlay pipeline: no depth, alpha blended, no culling.
    std::unique_ptr<MtlPipelineManager> _overlayPipeline;

    /*
     * This frame's command buffer and encoder. Both are autoreleased by Metal
     * and both live only between beginFrame()/endFrame(), so they are held as
     * raw borrowed pointers kept alive by _framePool rather than being retained.
     */
    MTL::CommandBuffer* _commandBuffer = nullptr;
    MTL::RenderCommandEncoder* _encoder = nullptr;

    //! One pool per frame; see ScopedAutoreleasePool for what would otherwise leak.
    std::optional<ScopedAutoreleasePool> _framePool;

    using FrameSlots = std::counting_semaphore<MTL_MAX_FRAMES_IN_FLIGHT>;

    /*
     * Throttles the CPU to MTL_MAX_FRAMES_IN_FLIGHT frames ahead of the GPU.
     * Acquired in beginFrame() and released from the command buffer's completion
     * handler, which is what guarantees a frame slot's overlay buffers are no
     * longer being read before the next frame writes them. Vulkan reaches the
     * same guarantee with a per-frame fence.
     *
     * Held by shared_ptr so the completion handler can own a reference instead of
     * capturing `this`: the handler runs on a Metal-internal thread at an
     * unspecified time, and a frame committed just before switchBackend() destroys
     * this renderer would otherwise release a semaphore that no longer exists.
     */
    std::shared_ptr<FrameSlots> _frameSlots;

    template <typename T>
    using MtlFixedArray = std::array<T, MTL_MAX_FRAMES_IN_FLIGHT>;

    MtlFixedArray<NS::SharedPtr<MTL::Buffer>> _overlayVertexBuffers{};
    MtlFixedArray<NS::SharedPtr<MTL::Buffer>> _overlayIndexBuffers{};
    MtlFixedArray<size_t> _overlayVertexCapacity{};
    MtlFixedArray<size_t> _overlayIndexCapacity{};

    //! 1x1 opaque white, created on first use; see resolveSampledTexture().
    TextureHandle _fallbackTexture = INVALID_HANDLE;

    u32 _currentFrame = 0;
    bool _isInitialized = false;
    //! Whether this frame has a drawable and a command buffer to encode into.
    bool _frameBegun = false;
    bool _renderPassActive = false;

    VertexBufferHandle _currentVertexBuffer = INVALID_HANDLE;
    IndexBufferHandle _currentIndexBuffer = INVALID_HANDLE;
    TextureHandle _currentTexture = INVALID_HANDLE;

    f32 _clearR = 0.05f;
    f32 _clearG = 0.05f;
    f32 _clearB = 0.05f;
    f32 _clearA = 1.0f;
};

} // namespace mtl
} // namespace aura3d

#endif // METAL_RENDERER_H
