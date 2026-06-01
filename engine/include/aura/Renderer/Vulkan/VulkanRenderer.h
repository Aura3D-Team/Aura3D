#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#pragma once

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

namespace aura3d {
namespace vk {

/**
 * @brief Vulkan-backed renderer. Implements IRenderer using the Vulkan API.
 *
 * Owns and coordinates all Vulkan sub-systems: instance, device, swapchain,
 * render pass, framebuffers, pipelines, resource managers, and synchronisation.
 * The rendering mode (2D / 3D) is selected at construction time via @p mode
 * and controls whether depth buffering is enabled.
 */
class VulkanRenderer : public IRenderer {
public:
    /**
     * @brief Constructs the renderer and records startup configuration.
     *
     * Does not allocate any Vulkan resources; call initialize() after the
     * window has been created via IRenderer::createWindow().
     *
     * @param windowDetails Window size, title, and display parameters.
     * @param mode          Rendering mode: MODE_2D disables depth, MODE_3D enables it.
     */
    VulkanRenderer(const wma::WindowDetails& windowDetails, RendererMode mode);

    /**
     * @brief Waits for the device to become idle, then calls cleanup().
     */
    virtual ~VulkanRenderer();

    /**
     * @brief Allocates all Vulkan resources and builds the initial swapchain.
     *
     * Must be called once after construction. Internally runs through four
     * private phases: allocators → core objects → resource managers →
     * swapchain resources.
     */
    void initialize() override;

    /**
     * @brief Recreates all swapchain-dependent resources after a resize or
     *        surface change. Safe to call from any thread after the device is idle.
     */
    void handleWindowChanges() override;

    /**
     * @brief Destroys all Vulkan resources and resets every sub-manager.
     *
     * Idempotent – may be called more than once without side effects.
     */
    void cleanup() override;

    /**
     * @brief Returns the underlying window manager (e.g. SDL2).
     * @return Pointer to the active IWindowManager; valid for the lifetime of this renderer.
     */
    wma::IWindowManager* getWindowManager() override { return _windowManagerApi.get(); }

    /**
     * @brief Identifies this renderer as the Vulkan backend.
     * @return RendererChoice::VULKAN.
     */
    RendererChoice getBackendType() const override { return RendererChoice::VULKAN; }

    /** @brief Returns the vertex buffer manager. */
    VkVertexBufferManager*     getVertexBufferManager()     { return _vkVertexBufferManager.get(); }

    /** @brief Returns the index buffer manager. */
    VkIndexBufferManager*      getIndexBufferManager()      { return _vkIndexBufferManager.get(); }

    /** @brief Returns the uniform buffer manager. */
    VkUniformBufferManager*    getUniformBufferManager()    { return _vkUniformBufferManager.get(); }

    /** @brief Returns the descriptor set manager. */
    VkDescriptorManager*       getDescriptorManager()       { return _vkDescriptorManager.get(); }

    /** @brief Returns the graphics pipeline manager. */
    VkGraphicsPipelineManager* getGraphicsPipelineManager() { return _vkGraphicsPipelineManager.get(); }

    /** @brief Returns the swapchain manager. */
    VkSwapChainManager*        getSwapChainManager()        { return _vkSwapChainManager.get(); }

    /** @brief Returns the render pass manager. */
    VkRenderPassManager*       getRenderPassManager()       { return _vkRenderPassManager.get(); }

    /** @brief Returns the framebuffer manager. */
    VkFrameBuffersManager*     getFrameBuffersManager()     { return _vkFrameBuffersManager.get(); }

    /** @brief Returns the command buffer manager. */
    VkCommandManager*          getCommandManager()          { return _vkCommandManager.get(); }

    /** @brief Returns the render synchronisation manager (semaphores / fences). */
    VkRenderSyncManager*       getRenderSyncManager()       { return _vkRenderSyncManager.get(); }

    /** @brief Returns the texture manager. */
    VkTextureManager*          getTextureManager()          { return _vkTextureManager.get(); }

    /** @brief Returns the logical and physical device manager. */
    VkDeviceManager*           getDeviceManager()           { return _vkDeviceManager.get(); }

    /** @brief Returns the GPU memory allocator. */
    VkDeviceAllocator*         getDeviceAllocator()         { return _vkDeviceAllocator.get(); }

    /** @brief Returns the CPU-side (host) allocator used for Vulkan callbacks. */
    VkHostAllocator*           getHostAllocator()           { return _vkHostAllocator.get(); }

    /**
     * @brief Returns all queue handles that were created with exclusive queue flags.
     * @return Read-only reference to the queue list.
     */
    const std::vector<aura3d::vk::QueueData*>& getQueues() const { return _queueDataFromExclusiveFlags; }

    /**
     * @brief Creates the graphics pipeline from compiled SPIR-V shader files.
     *
     * Must be called by the application after initialize() and before the first
     * frame is recorded.
     *
     * @param vertShaderPath Path to the SPIR-V vertex shader (.spv).
     * @param fragShaderPath Path to the SPIR-V fragment shader (.spv).
     */
    void setupPipeline(const std::string& vertShaderPath,
                       const std::string& fragShaderPath);

    /**
     * @brief Returns the per-frame command buffer array (size: MAX_FRAMES_IN_FLIGHT).
     * @return Reference to the fixed-size command buffer array.
     */
    VkFixedArray<VkCommandBuffer>& getCommandBuffers() { return _cmdBuffers; }

    /**
     * @brief Returns the index of the frame currently being recorded (0-based).
     * @return Current frame index in range [0, MAX_FRAMES_IN_FLIGHT).
     */
    u32 getCurrentFrame() const { return _currentFrame; }

    /**
     * @brief Advances the frame index, wrapping around at MAX_FRAMES_IN_FLIGHT.
     */
    void advanceFrame() { _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

protected:
    /**
     * @brief Creates the OS window and attaches it to the Vulkan surface.
     * @param title Window title string.
     */
    void createWindow(const char* title) override;

private:
    void createAllocators(bool enableValidationLayers);
    void createCoreObjects(bool enableValidationLayers);
    void createResourceManagers();

    void buildSwapchainResources();
    void destroySwapchainResources();

    void createDepthResources();
    void destroyDepthResources();

    void setupCommandBuffers();

private:
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;

    std::unique_ptr<aura3d::vk::VkHostAllocator>   _vkHostAllocator;
    std::unique_ptr<aura3d::vk::VkDeviceAllocator> _vkDeviceAllocator;

    std::unique_ptr<aura3d::vk::VkInstanceManager>  _vkInstance;
    std::unique_ptr<aura3d::vk::VkDeviceManager>    _vkDeviceManager;
    std::unique_ptr<aura3d::vk::VkSurfaceManager>   _vkSurfaceManager;

    std::unique_ptr<aura3d::vk::VkSwapChainManager>        _vkSwapChainManager;
    std::unique_ptr<aura3d::vk::VkImageViewsManager>       _vkImageViewsManager;
    std::unique_ptr<aura3d::vk::VkRenderPassManager>       _vkRenderPassManager;
    std::unique_ptr<aura3d::vk::VkFrameBuffersManager>     _vkFrameBuffersManager;
    std::unique_ptr<aura3d::vk::VkGraphicsPipelineManager> _vkGraphicsPipelineManager;

    std::unique_ptr<aura3d::vk::VkTextureManager>       _vkTextureManager;
    std::unique_ptr<aura3d::vk::VkDescriptorManager>    _vkDescriptorManager;
    std::unique_ptr<aura3d::vk::VkVertexBufferManager>  _vkVertexBufferManager;
    std::unique_ptr<aura3d::vk::VkIndexBufferManager>   _vkIndexBufferManager;
    std::unique_ptr<aura3d::vk::VkUniformBufferManager> _vkUniformBufferManager;

    std::unique_ptr<aura3d::vk::VkCommandManager>    _vkCommandManager;
    std::unique_ptr<aura3d::vk::VkRenderSyncManager> _vkRenderSyncManager;

    VkFixedArray<VkCommandBuffer> _cmdBuffers;
    u32  _currentFrame  = 0;
    bool _isInitialized = false;

    VkInstanceData _vkInstanceData;
    VkDeviceData   _vkDeviceData;
    ImageViewData  _vkImageViewData;

    std::vector<aura3d::vk::QueueData*> _queueDataFromExclusiveFlags;
    u32 _graphicsIndexFamily = 0;

    DepthResources _depth;
};

}
} // namespace aura3d

#endif // VULKAN_RENDERER_H
