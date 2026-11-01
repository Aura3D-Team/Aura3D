#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Vulkan/VkAura/VkCommandManager/VkCommandManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkCommandRecordingContext/VkCommandRecordingContext.h"
#include "aura/Renderer/Vulkan/VkAura/VkDescriptorManager/VkDescriptorManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkDeviceManager/VkDeviceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkFrameBuffersManager/VkFrameBuffersManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkImageViewsManager/VkImageViewsManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkIndexBufferManager/VkIndexBufferManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkInstanceManager/VkInstanceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkPipelineCache/VkPipelineCacheFile.h"
#include "aura/Renderer/Vulkan/VkAura/VkRenderPassManager/VkRenderPassManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkRenderSyncManager/VkRenderSyncManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSurfaceManager/VkSurfaceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSwapChainManager/VkSwapChainManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkTextureManager/VkTextureManager.h"
#ifdef AURA_PROFILE_FRAME
#include "aura/Renderer/Vulkan/VkAura/VkTimestampQuery/VkTimestampQuery.h"
#endif
#include "aura/Renderer/Vulkan/VkAura/VkUniformBufferManager/VkUniformBufferManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkVertexBufferManager/VkVertexBufferManager.h"

#ifdef AURA_ENABLE_DEBUG_MODE
#include "aura/Renderer/Vulkan/VkAura/VkDebugMode/VkDebugMetrics.h"
#endif

//! Forward-declared to keep ink/ParallelProcessor.h (and its <thread>/<mutex>
//! transitive includes) out of every translation unit that includes this
//! header -- same reasoning as CpuFrameBufferManager.
namespace ink
{
class ParallelProcessor;
}

namespace aura3d
{
namespace vk
{

class VulkanRenderer : public IRenderer
{
  public:
    VulkanRenderer(const wma::WindowDetails &windowDetails);
    virtual ~VulkanRenderer();

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
    void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels) override;
    TextureHandle createCoverageTexture(u32 width, u32 height) override;
    void updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                     const u8 *coverage) override;

    void beginFrame() override;
    [[nodiscard]] bool frameBegun() const noexcept override
    {
        return _frameState != FrameState::Idle;
    }
    [[nodiscard]] bool needsFrame() const noexcept override;
    void beginRenderPass() override;
    void beginRenderPass(RenderTargetHandle target) override;
    void endRenderPass() override;
    RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc) override;
    bool resizeRenderTarget(RenderTargetHandle target, u32 width, u32 height) override;
    [[nodiscard]] glm::uvec2 targetSize(RenderTargetHandle target) const override;
    [[nodiscard]] TextureHandle renderTargetTexture(RenderTargetHandle target) const override;
    bool readPixels(RenderTargetHandle target, std::span<u8> rgba) override;
    void endFrame() override;

    void setTransform(const gfx::TransformUBO &ubo) override;
    void setLights(std::span<const gfx::Light> lights) override;
    [[nodiscard]] RendererCaps caps() const noexcept override;
    MaterialHandle createMaterial(const Material &material) override;
    bool reloadShaders(const std::filesystem::path &directory) override;
    BufferHandle createBuffer(usize bytes, std::span<const std::byte> initial = {}) override;
    bool updateBuffer(BufferHandle buffer, usize offset, std::span<const std::byte> bytes) override;
    bool readBuffer(BufferHandle buffer, usize offset, std::span<std::byte> out) override;
    ComputeHandle createCompute(const ComputeDesc &desc) override;
    PipelineHandle createPipeline(const PipelineDesc &desc) override;
    [[nodiscard]] PipelineLimits pipelineLimits() const noexcept override;
    void drawPipeline(const PipelineDraw &draw) override;
    void dispatch(ComputeHandle kernel, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                  std::span<const std::byte> constants = {}) override;
    void drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                   BufferHandle arguments, usize argumentOffset = 0) override;
    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;

    /**
     * @brief Records @p items across worker threads, each into its own
     *        secondary command buffer.
     *
     * Produces exactly the draw order the base implementation would: the list
     * is split into contiguous chunks, and endRenderPass() replays the chunks'
     * buffers in order. Falls back to recording inline on the calling thread
     * for batches too small for the hand-off to pay for itself.
     */
    void drawMeshes(std::span<const DrawItem> items) override;
    void drawMeshInstanced(MeshHandle mesh, MaterialHandle material, std::span<const glm::mat4> models) override;
    using IRenderer::drawBatch;
    void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices, TextureHandle texture,
                   gfx::BatchSpace space = gfx::BatchSpace::Screen) override;
    [[nodiscard]] glm::uvec2 renderTargetSize() const noexcept override;

    RendererChoice getBackendType() const override;

    void setupPipeline(const std::string &vertShaderPath, const std::string &fragShaderPath);
    u32 getCurrentFrame() const;
    void advanceFrame();

#ifdef AURA_PROFILE_FRAME
    [[nodiscard]] GpuTimingStats gpuTiming() const noexcept override;
#endif

#ifdef AURA_ENABLE_DEBUG_MODE
    /**
     * @brief This backend's GPU counters, for aura3d::DebugMode's report.
     *
     * The only backend that answers this today. See VkDebugMetrics for what
     * each of the two sources behind it actually measures.
     */
    [[nodiscard]] const IGpuDebugSource *gpuDebugSource() const noexcept override
    {
        return &_debugMetrics;
    }
#endif

  protected:
    void createWindow(const char *title, const wma::WindowBackend &wBackend) override;

  private:
    void createCoreObjects(bool enableValidation);
    void recreateSurfaceAndSwapchain();
    void createResourceManagers();
    void buildSwapchainResources();
    //! The window's details with the size swapped for the framebuffer's: what the swapchain is sized from.
    [[nodiscard]] wma::WindowDetails drawableDetails() const;
    [[nodiscard]] bool swapchainMatchesSurface();
    void destroySwapchainResources();
    void createDepthResources();
    void destroyDepthResources();
    //! No-op when _msaaSamples == VK_SAMPLE_COUNT_1_BIT (MSAA disabled).
    void createMsaaColorResources();
    void destroyMsaaColorResources();
    void setupCommandBuffers();
    void createUniformBuffers();
    void createDescriptorSets();

    /**
     * @brief Makes a newly created texture samplable by both pipelines.
     *
     * The single entry point every texture-creation path uses: writes the
     * texture into the bindless table, or warns once and leaves it on the
     * fallback slot if the table is full.
     */
    void publishTexture(TextureHandle textureHandle);

    //! Writes @p textureHandle's view/sampler into _bindlessTextureSet at
    //! textureArrayIndexOf(textureHandle).
    void updateTextureDescriptorSets(TextureHandle textureHandle);
    void updateLightUniformBuffers();
    //! Records the per-draw state (transform push constants, UBO/light/texture
    //! descriptor sets) shared by drawIndexed() and draw().
    void bindDrawState(VkCommandBuffer cmd);

    /**
     * @brief Snapshots the descriptor sets, pipeline and extent every draw in
     *        the current frame shares.
     *
     * Taken once per batch and handed to the workers by const reference: the
     * whole set is fixed for the frame, so nothing here can change under a
     * thread that is mid-recording.
     */
    [[nodiscard]] SceneBindings sceneBindings() const;

    void createBatchPipeline();

    //! Appends a batch to this frame's mapped buffers; returns its first vertex and index, or nullopt.
    [[nodiscard]] std::optional<std::pair<u32, u32>> uploadBatch(std::span<const gfx::BatchVertex> vertices,
                                                                 std::span<const u32> indices);

    //! Writes the camera to the next slot of this frame's transform ring.
    void publishTransform();

    void destroyBatchBuffers();
    //! Grows one of this frame's batch ring buffers to @p needed; draws already recorded keep the old one.
    void growBatchBuffer(AllocatedBuffer &buffer, VkDeviceSize &capacity, VkDeviceSize needed, VkDeviceSize minimum,
                         VkBufferUsageFlags usage);
    //! Writes @p instances (instance data, or vertices for updateVertexBuffer() to copy) into this frame's batch
    //! vertex ring; returns their offset, or nullopt.
    [[nodiscard]] std::optional<VkDeviceSize> uploadTransient(std::span<const std::byte> instances);
    //! The batch vertex ring is drawn from and, for updateVertexBuffer(), copied from.
    static constexpr VkBufferUsageFlags kVertexRingUsage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    template <typename IndexT> IndexBufferHandle createIndexBufferOf(std::vector<IndexT> &&indices);

    std::unique_ptr<VulkanMemoryManager> _memoryManager;
    std::unique_ptr<aura3d::vk::VkInstanceManager> _vkInstance;
    std::unique_ptr<aura3d::vk::VkDeviceManager> _vkDeviceManager;
    std::unique_ptr<aura3d::vk::VkSurfaceManager> _vkSurfaceManager;
    std::unique_ptr<aura3d::vk::VkSwapChainManager> _vkSwapChainManager;
    std::unique_ptr<aura3d::vk::VkImageViewsManager> _vkImageViewsManager;
    std::unique_ptr<aura3d::vk::VkRenderPassManager> _vkRenderPassManager;
    std::unique_ptr<aura3d::vk::VkFrameBuffersManager> _vkFrameBuffersManager;
    std::unique_ptr<aura3d::vk::VkGraphicsPipelineManager> _vkGraphicsPipelineManager;
    std::unique_ptr<aura3d::vk::VkTextureManager> _vkTextureManager;
    std::unique_ptr<aura3d::vk::VkDescriptorManager> _vkDescriptorManager;
    std::unique_ptr<aura3d::vk::VkVertexBufferManager> _vkVertexBufferManager;
    std::unique_ptr<aura3d::vk::VkIndexBufferManager> _vkIndexBufferManager;
    std::unique_ptr<aura3d::vk::VkUniformBufferManager> _vkUniformBufferManager;
    std::unique_ptr<aura3d::vk::VkUniformBufferManager> _vkLightUniformBufferManager;
    std::unique_ptr<aura3d::vk::VkCommandManager> _vkCommandManager;
    std::unique_ptr<aura3d::vk::VkRenderSyncManager> _vkRenderSyncManager;

    std::unique_ptr<aura3d::vk::VkGraphicsPipelineManager> _vkBatchPipelineManager;
    //! The scene pipeline's interface, with the model per instance: see drawMeshInstanced().
    std::unique_ptr<aura3d::vk::VkGraphicsPipelineManager> _vkInstancedPipelineManager;
    std::vector<InstanceData> _instanceScratch;
    //! Every material's MaterialUniforms, read by the fragment shader at the push-constant index.
    static constexpr u32 kMaterialCapacity = 4096;
    AllocatedBuffer _materialTable{};
    void writeMaterial(u32 index, const Material &material);
    [[nodiscard]] u32 materialIndexOf(MaterialHandle handle) const noexcept;
    std::unique_ptr<aura3d::vk::VkPipelineCacheFile> _pipelineCache;
    VkFixedArray<AllocatedBuffer> _batchVertexBuffers;
    VkFixedArray<AllocatedBuffer> _batchIndexBuffers;
    VkFixedArray<VkDeviceSize> _batchVertexCapacity;
    VkFixedArray<VkDeviceSize> _batchIndexCapacity;
    VkFixedArray<VkDeviceSize> _batchVertexUsed;
    VkFixedArray<VkDeviceSize> _batchIndexUsed;
    //! Replaced buffers stay alive until this frame slot's fence completes.
    VkFixedArray<std::vector<AllocatedBuffer>> _batchRetiredBuffers;
    //! Opaque white in slot zero supplies untextured draws and invalid handles.
    TextureHandle _fallbackTexture;

    //! Cameras this frame has written to its slot of the transform ring, of _transformSlots.
    u32 _transformSlotsUsed = 0;
    u32 _transformSlots = 0;

    VkFixedArray<VkCommandBuffer> _cmdBuffers;

    //! Serial draws and worker chunks share one ordered secondary-buffer stream.
    VkCommandBuffer _sceneCmd = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> _chunkCmds;

    //! Draw list for the batch being recorded: handles resolved once on the
    //! submitting thread so workers touch no renderer-owned lookup table.
    //! Retained across frames to keep the per-frame path allocation-free.
    std::vector<ResolvedDraw> _resolvedDraws;

    /*
     * Workers for drawMeshes(). Sized from renderer.cpu_threads, the same
     * setting (and same auto-detect-when-0 convention) the software renderer's
     * rasteriser uses -- only one backend is ever live per run, so the two
     * cannot contend for it. Built by the first drawMeshes() that fans out.
     */
    std::unique_ptr<ink::ParallelProcessor> _recordPool;
    u32 _recordWorkerCount = 1;
    size_t _minDrawsToThread = 0;
    size_t _minDrawsPerChunk = 1;

    u32 _currentFrame = 0;
    u32 _currentImageIndex = 0;
    u32 _imagesCount = 0;
    bool _isInitialized = false;
    FrameState _frameState = FrameState::Idle;
    //! The swapchain holds no presented frame: nothing yet, or it was rebuilt
    //! since. Cleared by the next present.
    bool _targetStale = true;
    bool _pipelineReady = false;

    VulkanMemoryManager::Config _vmaConfig{};

#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * By value and for the renderer's whole lifetime, which is what Vulkan's
     * allocator rule requires: a handle created with a pAllocator must be
     * destroyed with a compatible one, so the allocator inside this cannot go
     * away while any such handle is still alive.
     */
    VkDebugMetrics _debugMetrics;
#endif
#ifdef AURA_PROFILE_FRAME
    VkTimestampQuery _gpuTimer;
    //! The device cannot timestamp on the graphics family; stops a retry every frame.
    bool _gpuTimerUnsupported = false;
#endif

    VkInstanceData _vkInstanceData;
    VkDeviceData _vkDeviceData;
    ImageViewData _vkImageViewData;
    std::vector<aura3d::vk::QueueData *> _queueDataFromExclusiveFlags;
    u32 _graphicsIndexFamily = 0;
    DepthResources _depth;

    //! Opens @p pass on @p framebuffer and the scene's secondary buffer for it; the pipelines follow. @p depthOnly:
    //! the pass has a depth attachment and nothing else.
    void beginPass(VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D extent, bool clearDepth,
                   bool depthOnly = false);
    //! VK_FORMAT_UNDEFINED as @p colour is the depth-only pass of a TextureFormat::Depth target.
    [[nodiscard]] VkRenderPass targetRenderPass(VkFormat colour, bool depth);
    [[nodiscard]] VkRenderPass buildColourPass(VkFormat colour, bool depth);
    [[nodiscard]] VkRenderPass buildDepthOnlyPass();
    void destroyRenderTargets() noexcept;

    struct RenderTarget
    {
        RenderTargetDesc desc{};
        VkExtent2D extent{};
        TextureHandle colour;
        AllocatedImage depth{};
        VkImageView depthView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass pass = VK_NULL_HANDLE;
    };
    std::vector<RenderTarget> _renderTargets; //! handle - 1 == index

    //! createBuffer()'s buffers: host-visible, mapped, and usable as uniform, storage, instance and draw-argument data.
    struct ComputeBuffer
    {
        AllocatedBuffer buffer{};
        VkDeviceSize size = 0;
    };
    std::vector<ComputeBuffer> _computeBuffers; //! handle - 1 == index
    [[nodiscard]] const ComputeBuffer *computeBufferOf(BufferHandle handle) const noexcept;
    struct ComputeKernel
    {
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        u32 buffers = 0;
    };
    std::vector<ComputeKernel> _computeKernels; //! handle - 1 == index
    //! The dispatches' descriptor sets, a pool per frame in flight, reset when its slot comes round again.
    std::vector<VkDescriptorPool> _computePools;
    void resetComputePool();
    void destroyKernel(ComputeKernel &kernel) noexcept;
    void destroyCompute() noexcept;

    //! createPipeline()'s pipelines: a manager each, with a variant per render pass.
    struct CustomPipeline
    {
        std::unique_ptr<aura3d::vk::VkGraphicsPipelineManager> manager;
        u32 textures = 0;
        u32 buffers = 0;
        u32 constantBytes = 0;
        VertexInput input = VertexInput::Mesh;
        std::vector<VkVertexInputBindingDescription> bindings;
        AttributeDescriptionArray<VkVertexInputAttributeDescription> attributes{};
        u32 attributeCount = 0;
        PipelineOptions options{};
    };
    std::vector<CustomPipeline> _customPipelines; //! handle - 1 == index
    //! The pools the draws' descriptor sets come from, per frame in flight: another is added when one runs out, and
    //! all are reset when the frame's slot comes round again.
    std::vector<std::vector<VkDescriptorPool>> _customPools;
    //! The last draw's sets (textures at set 0, buffers at set 1), reused by the next draw with the same bindings.
    struct CustomSet
    {
        u32 pipeline = 0;
        std::vector<u32> handles; //! The texture handles, then the buffer handles.
        std::array<VkDescriptorSet, 2> sets{};
    } _lastCustomSet;
    std::vector<std::byte> _customConstants; //! drawPipeline()'s constants, padded to the pipeline's size.
    //! (Re)creates @p pipeline for the window's pass, and its variants for every target's.
    void buildCustomPipeline(CustomPipeline &pipeline);
    //! False when a set could not be allocated.
    [[nodiscard]] bool customSetsFor(const CustomPipeline &pipeline, u32 index, const PipelineDraw &draw);
    [[nodiscard]] VkDescriptorSet allocateCustomSet(VkDescriptorSetLayout layout, u32 descriptors);
    void resetCustomPool();
    void destroyCustomPipelines() noexcept;
    //! @p target's colour (in its old slot, if it has one), depth and framebuffer for its desc.
    bool buildRenderTarget(RenderTarget &target);
    //! A TextureFormat::Depth target: its depth image is RenderTarget::colour, and the pass has nothing else.
    bool buildDepthTarget(RenderTarget &target);
    void destroyTargetAttachments(RenderTarget &target) noexcept;
    //! One render pass per colour format and depth choice, shared by every target that has them.
    struct TargetPass
    {
        VkFormat colour = VK_FORMAT_UNDEFINED;
        bool depth = false;
        VkRenderPass pass = VK_NULL_HANDLE;
    };
    std::vector<TargetPass> _targetPasses;
    //! The pass being recorded, the window's or a target's: what draws bind against and size by.
    VkRenderPass _passRenderPass = VK_NULL_HANDLE;
    VkFramebuffer _passFramebuffer = VK_NULL_HANDLE;
    VkExtent2D _passExtent{};
    bool _windowPassRecorded = false; //! This frame has begun the swapchain's pass.

    MsaaColorResources _msaaColor;
    VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    VertexBufferHandle _nextVbHandle{1};
    IndexBufferHandle _nextIbHandle{1};

    //! Buffers resolved at creation (handle - 1 == index), so a draw is a bounds check and a load
    //! instead of a string-keyed lookup in the buffer manager.
    std::vector<VertexBufferInfo> _vbByHandle;
    std::vector<IndexBufferInfo> _ibByHandle;

    std::vector<VkDescriptorSet> _descSets;      //! set 0: transform, per image
    std::vector<VkDescriptorSet> _lightDescSets; //! set 2: light, per image

    /*
     * Every texture in one bindless combined-image-sampler array, shared by the
     * scene (set 1) and batch (set 0) pipelines and never rebuilt per texture
     * or per resize. A draw selects its texture by a push-constant index (see
     * textureArrayIndexOf()) rather than by binding another set.
     */
    VkDescriptorSet _bindlessTextureSet = VK_NULL_HANDLE;

    //! Slots in the bindless table, resolved from the device's
    //! update-after-bind limits at createResourceManagers() time
    //! (VkDeviceManager::maxBindlessTextures()). Both pipeline layouts, the
    //! descriptor pool and textureArrayIndexOf()'s bounds check all read this
    //! one value, so they cannot drift apart.
    u32 _bindlessTextureCapacity = 0;

    //! Bind cache for _sceneCmd, the buffer the immediate-mode draw path
    //! records into. Defined in VkCommandRecordingContext.h so the batched path
    //! uses the identical type, one instance per command buffer.
    RecordedState _recorded;

    /*
     * Handle -> resolved-record accessors. Handles are dense 1-based counters,
     * so the lookup is a bounds check plus an indexed load; each returns
     * nullptr for a handle that was never created (or was created and failed),
     * which is exactly the "skip this bind" case the draw path already had.
     */
    [[nodiscard]] const VertexBufferInfo *vertexBufferOf(VertexBufferHandle handle) const noexcept
    {
        const size_t index = static_cast<size_t>(handle.value()) - 1;
        return (isValidHandle(handle) && index < _vbByHandle.size()) ? &_vbByHandle[index] : nullptr;
    }

    [[nodiscard]] const IndexBufferInfo *indexBufferOf(IndexBufferHandle handle) const noexcept
    {
        const size_t index = static_cast<size_t>(handle.value()) - 1;
        return (isValidHandle(handle) && index < _ibByHandle.size()) ? &_ibByHandle[index] : nullptr;
    }

    /**
     * @brief Maps a TextureHandle to its slot in the bindless texture table.
     *
     * Slot 0 is _fallbackTexture, written before any draw can happen (see
     * initialize()), and is returned for two distinct cases that must both
     * stay in-bounds rather than sampling an arbitrary element:
     *  - An invalid handle: bindTexture() was never called, or drawBatch() was
     *    handed no texture.
     *  - A handle beyond _bindlessTextureCapacity: the scene created more
     *    textures than this device's table can hold. Sampling out of range is
     *    undefined behaviour in the shader, so such a draw is rendered with
     *    the fallback texture instead. createTextureFromPixels() already logs
     *    the overflow once at creation time, which is where it is actionable.
     */
    [[nodiscard]] u32 textureArrayIndexOf(TextureHandle handle) const noexcept
    {
        if (!isValidHandle(handle))
            return 0u;

        const u32 slot = handle.value() - 1u;
        return (slot < _bindlessTextureCapacity) ? slot : 0u;
    }
};

} // namespace vk
} // namespace aura3d

#endif // VULKAN_RENDERER_H
