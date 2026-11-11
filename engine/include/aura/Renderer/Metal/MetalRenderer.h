#ifndef METAL_RENDERER_H
#define METAL_RENDERER_H

#pragma once

#include <array>
#include <atomic>
#include <deque>
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

namespace aura3d
{
namespace mtl
{

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
class MetalRenderer : public IRenderer
{
  public:
    MetalRenderer(const wma::WindowDetails &windowDetails);
    virtual ~MetalRenderer();

    void initialize(AuraSettings *settings, const JobSystem *jobs) override;
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

    void beginFrame() override;
    [[nodiscard]] bool frameBegun() const noexcept override
    {
        return _frameState != FrameState::Idle;
    }
    //! A resize is applied, and drawn, inside the next beginFrame(); nothing
    //! else here loses a presented frame.
    [[nodiscard]] bool needsFrame() const noexcept override
    {
        return resizePending();
    }
    void beginRenderPass() override;
    void beginRenderPass(RenderTargetHandle target) override;
    RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc) override;
    bool resizeRenderTarget(RenderTargetHandle target, u32 width, u32 height) override;
    [[nodiscard]] glm::uvec2 targetSize(RenderTargetHandle target) const override;
    [[nodiscard]] TextureHandle renderTargetTexture(RenderTargetHandle target) const override;
    bool readPixels(RenderTargetHandle target, std::span<u8> rgba) override;
    void endRenderPass() override;
    void endFrame() override;

    /*
     * setLights() is deliberately not overridden. The base implementation records
     * the lights in IRenderer::_lights, and that is already where bindDrawState()
     * reads it from to push the bytes at each draw -- there is no GPU-side buffer
     * to keep in step, so an override would have nothing to add.
     */
    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;
    void drawMeshInstanced(MeshHandle mesh, MaterialHandle material, std::span<const glm::mat4> models) override;
    BufferHandle createBuffer(usize bytes, std::span<const std::byte> initial = {}) override;
    bool updateBuffer(BufferHandle buffer, usize offset, std::span<const std::byte> bytes) override;
    bool readBuffer(BufferHandle buffer, usize offset, std::span<std::byte> out) override;
    ComputeHandle createCompute(const ComputeDesc &desc) override;
    void dispatch(ComputeHandle kernel, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                  std::span<const std::byte> constants = {}) override;
    void drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                   BufferHandle arguments, usize argumentOffset = 0) override;
    PipelineHandle createPipeline(const PipelineDesc &desc) override;
    [[nodiscard]] PipelineLimits pipelineLimits() const noexcept override;
    void drawPipeline(const PipelineDraw &draw) override;
    bool reloadShaders(const std::filesystem::path &directory) override;
    using IRenderer::drawBatch;
    void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices, TextureHandle texture,
                   gfx::BatchSpace space = gfx::BatchSpace::Screen) override;
    [[nodiscard]] glm::uvec2 renderTargetSize() const noexcept override;
#ifdef AURA_PROFILE_FRAME
    [[nodiscard]] GpuTimingStats gpuTiming() const noexcept override;
#endif

    [[nodiscard]] RendererCaps caps() const noexcept override;
    RendererChoice getBackendType() const override
    {
        return RendererChoice::METAL;
    }

  protected:
    void createWindow(const char *title, const wma::WindowBackend &wBackend) override;

  private:
    //! Builds the scene and batch pipelines against the layer's formats.
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
     * @brief Grows this frame's batch vertex/index buffers to fit a batch.
     *
     * There is one pair per frame in flight so that writing this frame's geometry
     * cannot scribble over a batch the GPU is still reading. They only ever grow,
     * so steady-state batches stop allocating after the first few frames -- the
     * same arrangement as VulkanRenderer::uploadBatch().
     *
     * @return false when either allocation failed, in which case the batch is
     *         skipped rather than drawn from a buffer that is too small.
     */
    [[nodiscard]] bool ensureBatchCapacity(size_t vertexBytes, size_t indexBytes);

    /**
     * @brief The texture a draw should sample, substituting the fallback when
     *        nothing usable is bound.
     *
     * IRenderer lets a caller draw with no texture and lets drawBatch() be
     * handed an invalid handle, but the shaders always sample. A 1x1 opaque white
     * texel is substituted in those cases, so vertex colour comes through
     * unchanged -- the same fallback the OpenGL and Vulkan paths use.
     */
    [[nodiscard]] MTL::Texture *resolveSampledTexture(TextureHandle handle);

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
    std::unique_ptr<MtlPipelineManager> _batchPipeline;
    std::unique_ptr<MtlPipelineManager> _instancedPipeline;
    //! What the open pass binds: the drawable's pipelines, or its target's format's.
    MtlPipelineManager *_activeScene = nullptr;
    MtlPipelineManager *_activeBatch = nullptr;
    MtlPipelineManager *_activeInstanced = nullptr;
    //! Scene and batch pipelines for one target colour format and depth choice.
    struct TargetPipelines
    {
        MTL::PixelFormat colour = MTL::PixelFormatInvalid;
        bool depth = false;
        std::unique_ptr<MtlPipelineManager> scene;
        std::unique_ptr<MtlPipelineManager> batch;
        std::unique_ptr<MtlPipelineManager> instanced;
    };
    //! A deque: targets point into it, and emplace_back never moves what is already there.
    std::deque<TargetPipelines> _targetPipelines;
    struct RenderTarget
    {
        TextureHandle colour;
        NS::SharedPtr<MTL::Texture> depth;
        u32 width = 0;
        u32 height = 0;
        TargetPipelines *pipelines = nullptr;
    };
    std::vector<RenderTarget> _renderTargets; //! handle - 1 == index
    u32 _passTarget = 0;
    [[nodiscard]] TargetPipelines &pipelinesFor(MTL::PixelFormat colour, bool depth);
    //! The scene and batch options every pass's pipelines share, for @p colour and @p depth.
    void buildPipelines(MTL::PixelFormat colour, MTL::PixelFormat depth, std::unique_ptr<MtlPipelineManager> &scene,
                        std::unique_ptr<MtlPipelineManager> &batch,
                        std::unique_ptr<MtlPipelineManager> &instanced) const;
    //! What the encoder has bound, so a run of draws of one kind binds once.
    const MtlPipelineManager *_boundPipeline = nullptr;
    void usePipeline(MtlPipelineManager *pipeline);
    //! The lighting block, the bound material's uniforms and its maps, for a scene draw.
    void bindSceneMaterial();
    std::vector<InstanceUniforms> _instanceScratch;
    //! createBuffer()'s shared-storage buffers and createCompute()'s pipelines; handle - 1 == index.
    std::vector<NS::SharedPtr<MTL::Buffer>> _buffers;
    [[nodiscard]] MTL::Buffer *bufferOf(BufferHandle handle) const noexcept;
    struct Kernel
    {
        NS::SharedPtr<MTL::ComputePipelineState> pipeline;
        u32 buffers = 0;
        glm::uvec3 threads{64, 1, 1};
    };
    std::vector<Kernel> _kernels;

    //! createPipeline()'s pipelines; handle - 1 == index. A pipeline is built for each attachment format it draws
    //! into, as the built-in ones are.
    struct CustomPipeline
    {
        struct Variant
        {
            MTL::PixelFormat colour;
            bool depth;
            std::unique_ptr<MtlPipelineManager> pipeline;
        };
        std::unique_ptr<MtlShaderLibraryManager> vertex, fragment;
        NS::SharedPtr<MTL::VertexDescriptor> vertexDescriptor; //! VertexInput::Mesh only.
        std::string vertexEntry, fragmentEntry;
        PipelineState state = PipelineState::None;
        VertexInput input = VertexInput::Mesh;
        u32 textures = 0, buffers = 0, constantBytes = 0;
        std::vector<Variant> variants;
    };
    std::vector<CustomPipeline> _customPipelines;
    std::vector<std::byte> _customConstants;
    //! The open pass's attachments: which variant of a custom pipeline to draw with.
    MTL::PixelFormat _passColour = kColorFormat;
    bool _passDepth = true;
    //! Builds the variant for a pass of this format if it is new; throws AuraException as MtlPipelineManager does.
    MtlPipelineManager &customVariant(CustomPipeline &pipeline, MTL::PixelFormat colour, bool depth) const;
    //! Blocks until every committed frame has completed: Metal's vkDeviceWaitIdle.
    void waitForFrames();
    //! Private Depth32Float, for a target's depth.
    [[nodiscard]] NS::SharedPtr<MTL::Texture> newDepthTexture(u32 width, u32 height, MTL::TextureUsage usage) const;

    /*
     * This frame's command buffer and encoder. Both are autoreleased by Metal
     * and both live only between beginFrame()/endFrame(), so they are held as
     * raw borrowed pointers kept alive by _framePool rather than being retained.
     */
    MTL::CommandBuffer *_commandBuffer = nullptr;
    MTL::RenderCommandEncoder *_encoder = nullptr;

    //! One pool per frame; see ScopedAutoreleasePool for what would otherwise leak.
    std::optional<ScopedAutoreleasePool> _framePool;

    using FrameSlots = std::counting_semaphore<MTL_MAX_FRAMES_IN_FLIGHT>;

    /*
     * Throttles the CPU to MTL_MAX_FRAMES_IN_FLIGHT frames ahead of the GPU.
     * Acquired in beginFrame() and released from the command buffer's completion
     * handler, which is what guarantees a frame slot's batch buffers are no
     * longer being read before the next frame writes them. Vulkan reaches the
     * same guarantee with a per-frame fence.
     *
     * Held by shared_ptr so the completion handler can own a reference instead of
     * capturing `this`: the handler runs on a Metal-internal thread at an
     * unspecified time, and a frame committed just before switchBackend() destroys
     * this renderer would otherwise release a semaphore that no longer exists.
     */
    std::shared_ptr<FrameSlots> _frameSlots;

#ifdef AURA_PROFILE_FRAME
    //! The last finished command buffer's GPU time, written from the completion handler on a
    //! Metal thread; shared rather than captured through `this` for the reason _frameSlots is.
    struct GpuClock
    {
        std::atomic<i64> frameNanos{0};
        std::atomic<u64> resolved{0};
        std::atomic<u64> dropped{0};
    };
    std::shared_ptr<GpuClock> _gpuClock;
    //! GpuClock::resolved as last handed to the frame profiler.
    u64 _gpuReported = 0;
#endif

    template <typename T> using MtlFixedArray = std::array<T, MTL_MAX_FRAMES_IN_FLIGHT>;

    MtlFixedArray<NS::SharedPtr<MTL::Buffer>> _batchVertexBuffers{};
    MtlFixedArray<NS::SharedPtr<MTL::Buffer>> _batchIndexBuffers{};
    MtlFixedArray<size_t> _batchVertexCapacity{};
    MtlFixedArray<size_t> _batchIndexCapacity{};

    /*
     * Bytes of this frame's batch buffers already spoken for. Every drawBatch()
     * in a frame encodes into the same render command encoder and nothing
     * executes until the command buffer is committed, so a batch that wrote at
     * offset 0 would be read back as whatever the frame's last batch left
     * there. Each batch appends here and binds at its own offset -- the same
     * fix the Vulkan backend carries, for the same reason.
     */
    MtlFixedArray<size_t> _batchVertexUsed{};
    MtlFixedArray<size_t> _batchIndexUsed{};

    //! 1x1 opaque white, created on first use; see resolveSampledTexture().
    TextureHandle _fallbackTexture;

    u32 _currentFrame = 0;
    bool _isInitialized = false;
    //! Recording or InPass: this frame has a drawable and a command buffer to encode into.
    FrameState _frameState = FrameState::Idle;
};

} // namespace mtl
} // namespace aura3d

#endif // METAL_RENDERER_H
