#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#pragma once

#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Vulkan/VkAura/VkInstanceManager/VkInstanceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkDeviceManager/VkDeviceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSurfaceManager/VkSurfaceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSwapChainManager/VkSwapChainManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkImageViewsManager/VkImageViewsManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkRenderPassManager/VkRenderPassManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkFrameBuffersManager/VkFrameBuffersManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkDescriptorManager/VkDescriptorManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkTextureManager/VkTextureManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkVertexBufferManager/VkVertexBufferManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkIndexBufferManager/VkIndexBufferManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkUniformBufferManager/VkUniformBufferManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkCommandManager/VkCommandManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkRenderSyncManager/VkRenderSyncManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkCommandRecordingContext/VkCommandRecordingContext.h"

//! Forward-declared to keep ink/ThreadPool.h (and its <thread>/<mutex>
//! transitive includes) out of every translation unit that includes this
//! header -- same reasoning as CpuFrameBufferManager.
namespace ink { class ThreadPool; }

namespace aura3d {
namespace vk {

class VulkanRenderer : public IRenderer {
public:
    VulkanRenderer(const wma::WindowDetails& windowDetails);
    virtual ~VulkanRenderer();

    void initialize(AuraSettings* settings) override;
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
    void setLight(const gfx::LightUBO& light) override;
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
    void drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                     std::span<const u32> indices,
                     TextureHandle texture) override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;

    wma::IWindowManager* getWindowManager() override;
    RendererChoice getBackendType() const override;

    VkVertexBufferManager* getVertexBufferManager();
    VkIndexBufferManager* getIndexBufferManager();
    VkUniformBufferManager* getUniformBufferManager();
    VkDescriptorManager* getDescriptorManager();
    VkGraphicsPipelineManager* getGraphicsPipelineManager();
    VkSwapChainManager* getSwapChainManager();
    VkRenderPassManager* getRenderPassManager();
    VkFrameBuffersManager* getFrameBuffersManager();
    VkCommandManager* getCommandManager();
    VkRenderSyncManager* getRenderSyncManager();
    VkTextureManager* getTextureManager();
    VkDeviceManager* getDeviceManager();
    VulkanMemoryManager* getMemoryManager();

    const std::vector<aura3d::vk::QueueData*>& getQueues() const;
    void setupPipeline(const std::string& vertShaderPath, const std::string& fragShaderPath);
    VkFixedArray<VkCommandBuffer>& getCommandBuffers();
    u32 getCurrentFrame() const;
    void advanceFrame();

protected:
    void createWindow(const char* title, const wma::WindowBackend& wBackend) override;

private:
    void setupInput();
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
     * texture into the 3D and overlay bindless tables, or warns once and
     * leaves it on the fallback slot if the table is full.
     */
    void publishTexture(TextureHandle textureHandle);

    //! Writes @p textureHandle's view/sampler into the 3D pipeline's bindless
    //! texture array at textureArrayIndexOf(textureHandle). Allocation of the
    //! array itself (_bindlessTextureSet3D) happens once, in createDescriptorSets().
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

    /**
     * @brief Builds the unlit 2D overlay pipeline against the current render
     *        pass and swapchain extent.
     *
     * Called from createDescriptorSets(), so it is rebuilt alongside the 3D
     * pipeline whenever the swapchain is recreated.
     */
    void createOverlay2DPipeline();

    /**
     * @brief Writes @p handle's view/sampler into the overlay pipeline's own
     *        bindless texture array (_bindlessTextureSet2D) at
     *        textureArrayIndexOf(handle).
     *
     * Separate from updateTextureDescriptorSets(): the two pipelines have
     * different layouts, so a set allocated for one cannot be bound to the other.
     */
    void updateOverlay2DTextureDescriptorSets(TextureHandle handle);

    /**
     * @brief Grows this frame's overlay vertex/index buffers to fit a batch.
     *
     * The buffers are host-visible and persistently mapped, and there is one
     * pair per frame in flight so that writing this frame's geometry cannot
     * scribble over a batch the GPU is still reading. They only ever grow, so a
     * steady-state overlay stops allocating after the first few frames.
     */
    void ensureOverlay2DCapacity(u32 frame, VkDeviceSize vertexBytes, VkDeviceSize indexBytes);

    //! Destroys every per-frame overlay buffer.
    void destroyOverlay2DBuffers();

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

    /*
     * The unlit 2D overlay pipeline: its own shader modules, its own layout
     * (one combined image sampler in set 0, plus a 64-byte push-constant
     * projection) and its own fixed-function state (no depth, alpha blending,
     * no culling). Nothing about it is shared with the 3D pipeline above.
     */
    std::unique_ptr<aura3d::vk::VkGraphicsPipelineManager> _vkOverlay2DPipelineManager;
    VkFixedArray<AllocatedBuffer> _overlay2DVertexBuffers{};
    VkFixedArray<AllocatedBuffer> _overlay2DIndexBuffers{};
    VkFixedArray<VkDeviceSize> _overlay2DVertexCapacity{};
    VkFixedArray<VkDeviceSize> _overlay2DIndexCapacity{};
    //! Persistent bindless texture array bound at the overlay pipeline's set 0
    //! (see _bindlessTextureSet3D below for why this is a single set rather
    //! than one per texture).
    VkDescriptorSet _bindlessTextureSet2D = VK_NULL_HANDLE;
    //! 1x1 opaque white, at texture-array slot 0 (see textureArrayIndexOf()).
    //! Substituted when a batch asks for no texture, and shared with the 3D
    //! path as the fallback for bindTexture(INVALID_HANDLE).
    TextureHandle _fallbackTexture = INVALID_HANDLE;

    VkFixedArray<VkCommandBuffer> _cmdBuffers;

    /*
     * The render pass is begun with VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS,
     * so no draw may be recorded into the primary buffer -- Vulkan has no
     * mixed mode within a subpass instance. Draws go into these secondary
     * buffers instead, and endRenderPass() replays them into the primary with
     * a single vkCmdExecuteCommands, in this declaration order (scene first,
     * overlay composited on top).
     *
     * Scene and overlay are kept apart rather than sharing one buffer because
     * only the scene half is parallelizable: splitting it across worker
     * threads means several scene buffers and still exactly one overlay,
     * replayed last.
     */
    VkCommandBuffer _sceneCmd = VK_NULL_HANDLE;

    /*
     * Secondary buffers produced by drawMeshes()' worker threads this frame,
     * replayed by endRenderPass() between _sceneCmd and _overlayCmd. Kept as a
     * member and only ever cleared (never shrunk) so a steady-state frame
     * reuses the same allocation.
     */
    std::vector<VkCommandBuffer> _chunkCmds;

    //! Scratch for endRenderPass()' vkCmdExecuteCommands argument. A member
    //! purely so the per-frame replay costs no allocation.
    std::vector<VkCommandBuffer> _replayList;

    /*
     * One context per worker, reused for the renderer's lifetime. Held by
     * pointer because a context owns a bind cache that must not be copied or
     * moved while a worker is recording through it.
     */
    std::vector<std::unique_ptr<VkCommandRecordingContext>> _recordingContexts;

    //! Draw list for the batch being recorded: handles resolved once on the
    //! submitting thread so workers touch no renderer-owned lookup table.
    //! Retained across frames to keep the per-frame path allocation-free.
    std::vector<ResolvedDraw> _resolvedDraws;

    /*
     * Workers for drawMeshes(). Sized from graphics.cpu_threads, the same
     * setting (and same auto-detect-when-0 convention) the software renderer's
     * rasteriser uses -- only one backend is ever live per run, so the two
     * cannot contend for it.
     */
    std::unique_ptr<ink::ThreadPool> _recordPool;
    u32 _recordWorkerCount = 1;

    /*
     * Join handles for the chunk tasks drawMeshes() fans out. A member, and
     * only ever cleared, so a steady-state frame reuses the allocation rather
     * than building a fresh vector of futures per frame. Never outlives the
     * drawMeshes() call that fills it -- every future is waited on before that
     * function returns.
     */
    std::vector<std::future<void>> _recordFutures;
    //! Begun on the frame's first drawBatch2D(), so a frame without an overlay
    //! costs nothing.
    VkCommandBuffer _overlayCmd = VK_NULL_HANDLE;
    //! Whether the overlay pipeline and its texture set are already bound in
    //! _overlayCmd. Unlike _recorded this needs no invalidation from the scene
    //! path: the two record into different command buffers and cannot disturb
    //! each other's bindings.
    bool _overlayStateBound = false;

    u32 _currentFrame = 0;
    u32 _currentImageIndex = 0;
    u32 _imagesCount = 0;
    bool _isInitialized = false;
    bool _frameBegun = false;
    bool _renderPassActive = false;
    bool _pipelineReady = false;

    VulkanMemoryManager::Config _vmaConfig{};

    VkInstanceData _vkInstanceData;
    VkDeviceData _vkDeviceData;
    ImageViewData _vkImageViewData;
    std::vector<aura3d::vk::QueueData*> _queueDataFromExclusiveFlags;
    u32 _graphicsIndexFamily = 0;
    DepthResources _depth;
    MsaaColorResources _msaaColor;
    VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    VertexBufferHandle _nextVbHandle = 1;
    IndexBufferHandle _nextIbHandle = 1;

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

    std::vector<VkDescriptorSet> _descSets; //! set 0: transform, per image
    std::vector<VkDescriptorSet> _lightDescSets; //! set 2: light, per image

    /*
     * set 1: a single bindless combined-image-sampler array (MAX_BINDLESS_TEXTURES
     * elements), bound once and never rebuilt per-texture or per-resize. A
     * texture is selected per-draw via a push-constant array index (see
     * bindDrawState()/textureArrayIndexOf()) instead of swapping which
     * descriptor set is bound -- this is what let the old one-set-per-texture-
     * per-image-per-pipeline scheme (which exhausted a 256-descriptor pool
     * around the 42nd texture) go away entirely.
     */
    VkDescriptorSet _bindlessTextureSet3D = VK_NULL_HANDLE;

    //! Slots in each bindless table, resolved from the device's
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
    [[nodiscard]] const VertexBufferInfo* vertexBufferOf(VertexBufferHandle handle) const noexcept
    {
        const size_t index = static_cast<size_t>(handle) - 1;
        return (handle != INVALID_HANDLE && index < _vbByHandle.size()) ? &_vbByHandle[index] : nullptr;
    }

    [[nodiscard]] const IndexBufferInfo* indexBufferOf(IndexBufferHandle handle) const noexcept
    {
        const size_t index = static_cast<size_t>(handle) - 1;
        return (handle != INVALID_HANDLE && index < _ibByHandle.size()) ? &_ibByHandle[index] : nullptr;
    }

    /**
     * @brief Maps a TextureHandle to its slot in the bindless texture table.
     *
     * Slot 0 is _fallbackTexture, written before any draw can happen (see
     * initialize()), and is returned for two distinct cases that must both
     * stay in-bounds rather than sampling an arbitrary element:
     *  - INVALID_HANDLE: bindTexture() was never called, or drawBatch2D() was
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

        const u32 slot = static_cast<u32>(handle) - 1u;
        return (slot < _bindlessTextureCapacity) ? slot : 0u;
    }

    VertexBufferHandle _currentVertexBuffer = INVALID_HANDLE;
    IndexBufferHandle _currentIndexBuffer = INVALID_HANDLE;
    TextureHandle _currentTexture = INVALID_HANDLE;

    f32 _clearR = 0.05f;
    f32 _clearG = 0.05f;
    f32 _clearB = 0.05f;
    f32 _clearA = 1.0f;
};

} // namespace vk
} // namespace aura3d

#endif // VULKAN_RENDERER_H
