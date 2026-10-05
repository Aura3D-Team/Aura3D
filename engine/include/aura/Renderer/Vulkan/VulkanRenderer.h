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
#include "aura/Renderer/Vulkan/VkAura/VkRenderPassManager/VkRenderPassManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkRenderSyncManager/VkRenderSyncManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSurfaceManager/VkSurfaceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSwapChainManager/VkSwapChainManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkTextureManager/VkTextureManager.h"
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
    IndexBufferHandle createIndexBuffer(std::vector<u16> &&indices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u32> &&indices) override;
    TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255) override;
    TextureHandle createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height) override;
    TextureHandle createDynamicTexture(u32 width, u32 height) override;
    void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels) override;
    TextureHandle createCoverageTexture(u32 width, u32 height) override;
    void updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                     const u8 *coverage) override;

    void beginFrame() override;
    [[nodiscard]] bool frameBegun() const noexcept override
    {
        return _frameBegun;
    }
    [[nodiscard]] bool needsFrame() const noexcept override;
    void beginRenderPass() override;
    void endRenderPass() override;
    void endFrame() override;

    void setTransform(const gfx::TransformUBO &ubo) override;
    void setLight(const gfx::LightUBO &light) override;
    void bindVertexBuffer(VertexBufferHandle handle) override;
    void bindIndexBuffer(IndexBufferHandle handle) override;
    void bindTexture(TextureHandle handle) override;
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
    using IRenderer::drawBatch;
    void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices, TextureHandle texture,
                   gfx::BatchSpace space = gfx::BatchSpace::Screen) override;
    [[nodiscard]] glm::uvec2 renderTargetSize() const noexcept override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;

    wma::IWindowManager *getWindowManager() override;
    RendererChoice getBackendType() const override;

    VkVertexBufferManager *getVertexBufferManager();
    VkIndexBufferManager *getIndexBufferManager();
    VkUniformBufferManager *getUniformBufferManager();
    VkDescriptorManager *getDescriptorManager();
    VkGraphicsPipelineManager *getGraphicsPipelineManager();
    VkSwapChainManager *getSwapChainManager();
    VkRenderPassManager *getRenderPassManager();
    VkFrameBuffersManager *getFrameBuffersManager();
    VkCommandManager *getCommandManager();
    VkRenderSyncManager *getRenderSyncManager();
    VkTextureManager *getTextureManager();
    VkDeviceManager *getDeviceManager();
    VulkanMemoryManager *getMemoryManager();

    const std::vector<aura3d::vk::QueueData *> &getQueues() const;
    void setupPipeline(const std::string &vertShaderPath, const std::string &fragShaderPath);
    VkFixedArray<VkCommandBuffer> &getCommandBuffers();
    u32 getCurrentFrame() const;
    void advanceFrame();

#ifdef AURA_ENABLE_DEBUG_MODE
    /**
     * @brief This backend's GPU counters, for aura3d::DebugMode's report.
     *
     * The only backend that answers this today. See VkDebugMetrics for what
     * each of the three sources behind it actually measures.
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

    std::unique_ptr<wma::IWindowManager> _windowManagerApi;
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

    //! Cameras this frame has written to its slot of the transform ring.
    u32 _transformSlotsUsed = 0;

    VkFixedArray<VkCommandBuffer> _cmdBuffers;

    //! Serial draws and worker chunks share one ordered secondary-buffer stream.
    VkCommandBuffer _sceneCmd = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> _chunkCmds;

    //! Draw list for the batch being recorded: handles resolved once on the
    //! submitting thread so workers touch no renderer-owned lookup table.
    //! Retained across frames to keep the per-frame path allocation-free.
    std::vector<ResolvedDraw> _resolvedDraws;

    /*
     * Workers for drawMeshes(). Sized from graphics.cpu_threads, the same
     * setting (and same auto-detect-when-0 convention) the software renderer's
     * rasteriser uses -- only one backend is ever live per run, so the two
     * cannot contend for it. Built by the first drawMeshes() that fans out.
     */
    std::unique_ptr<ink::ParallelProcessor> _recordPool;
    u32 _recordWorkerCount = 1;

    u32 _currentFrame = 0;
    u32 _currentImageIndex = 0;
    u32 _imagesCount = 0;
    bool _isInitialized = false;
    bool _frameBegun = false;
    //! The swapchain holds no presented frame: nothing yet, or it was rebuilt
    //! since. Cleared by the next present.
    bool _targetStale = true;
    bool _renderPassActive = false;
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

    VkInstanceData _vkInstanceData;
    VkDeviceData _vkDeviceData;
    ImageViewData _vkImageViewData;
    std::vector<aura3d::vk::QueueData *> _queueDataFromExclusiveFlags;
    u32 _graphicsIndexFamily = 0;
    DepthResources _depth;
    MsaaColorResources _msaaColor;
    VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    VertexBufferHandle _nextVbHandle{1};
    IndexBufferHandle _nextIbHandle{1};

    /*
     * Buffer managers are still keyed by a synthetic "vb_N"/"ib_N" string.
     * These maps hold that name so creation and cleanup can reach it; nothing
     * on the draw path may touch them -- see _vbByHandle/_ibByHandle below,
     * which is what a draw actually resolves through. Textures no longer need
     * an equivalent: VkTextureManager issues dense ids directly, and the
     * renderer adopts the id as the public TextureHandle.
     */
    std::unordered_map<VertexBufferHandle, std::string> _vbNames;
    std::unordered_map<IndexBufferHandle, std::string> _ibNames;

    /*
     * Handle-indexed mirrors of the *Names maps above (handle - 1 == index).
     * The maps stay the naming authority used by creation and swapchain
     * rebuilds, but the draw path must not touch them: resolving a buffer
     * through them costs an integer hash lookup to obtain a std::string, then
     * a second, string-hashing lookup inside the buffer manager -- per draw,
     * per buffer. These vectors hold the already-resolved handles so a draw is
     * a bounds check and an indexed load.
     */
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

    VertexBufferHandle _currentVertexBuffer;
    IndexBufferHandle _currentIndexBuffer;
    TextureHandle _currentTexture;

    f32 _clearR = 0.05f;
    f32 _clearG = 0.05f;
    f32 _clearB = 0.05f;
    f32 _clearA = 1.0f;
};

} // namespace vk
} // namespace aura3d

#endif // VULKAN_RENDERER_H
