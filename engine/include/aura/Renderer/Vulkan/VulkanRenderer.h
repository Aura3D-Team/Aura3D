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
 * @brief Vulkan implementation of the Renderer interface.
 *
 * Owns and manages all Vulkan infrastructure (instance, device, swapchain,
 * render pass, framebuffers, command buffers, sync primitives and all
 * sub-manager objects).  Application-specific geometry, textures, descriptor
 * sets and the per-frame draw loop live in the consuming application (Sandbox).
 */
class VulkanRenderer : public IRenderer {
public:
    VulkanRenderer(const wma::WindowDetails& windowDetails);
    virtual ~VulkanRenderer();

    /**
     * @brief Initialise all Vulkan infrastructure.
     *
     * After this call the following are ready:
     *   - Vulkan instance, physical/logical device, surface
     *   - Swapchain, image views, render pass, framebuffers
     *   - Command pool/buffers, synchronisation primitives
     *   - All sub-manager objects (vertex/index/uniform/descriptor/texture)
     *
     * Shader paths and actual geometry/descriptor data are the
     * responsibility of the application.
     */
    void initialize() override;

    /**
     * @brief Recreate swapchain and its dependents after a window resize.
     */
    void handleWindowChanges() override;

    /**
     * @brief Destroy all Vulkan resources in the correct order.
     */
    void cleanup() override;

    // Window / loop
    wma::IWindowManager* getWindowManager() override { return _windowManagerApi.get(); }

    // -----------------------------------------------------------------------
    // Sub-manager accessors – use these from the application for resource
    // setup and per-frame command recording.
    // -----------------------------------------------------------------------

    VkVertexBufferManager*     getVertexBufferManager()    { return _vkVertexBufferManager.get(); }
    VkIndexBufferManager*      getIndexBufferManager()     { return _vkIndexBufferManager.get(); }
    VkUniformBufferManager*    getUniformBufferManager()   { return _vkUniformBufferManager.get(); }
    VkDescriptorManager*       getDescriptorManager()      { return _vkDescriptorManager.get(); }
    VkGraphicsPipelineManager* getGraphicsPipelineManager(){ return _vkGraphicsPipelineManager.get(); }
    VkSwapChainManager*        getSwapChainManager()       { return _vkSwapChainManager.get(); }
    VkRenderPassManager*       getRenderPassManager()      { return _vkRenderPassManager.get(); }
    VkFrameBuffersManager*     getFrameBuffersManager()    { return _vkFrameBuffersManager.get(); }
    VkCommandManager*          getCommandManager()         { return _vkCommandManager.get(); }
    VkRenderSyncManager*       getRenderSyncManager()      { return _vkRenderSyncManager.get(); }
    VkTextureManager*          getTextureManager()         { return _vkTextureManager.get(); }
    VkDeviceManager*           getDeviceManager()          { return _vkDeviceManager.get(); }
    VkDeviceAllocator*         getDeviceAllocator()        { return _vkDeviceAllocator.get(); }
    VkHostAllocator*           getHostAllocator()          { return _vkHostAllocator.get(); }

    const std::vector<aura3d::vk::QueueData*>& getQueues() const { return _queueDataFromExclusiveFlags; }

    /**
     * @brief Create the graphics pipeline for a given pair of SPIR-V shaders.
     *
     * Call once after initialize() and before the render loop.  Descriptor-set
     * layouts are created here; the application is then responsible for
     * allocating and updating individual descriptor sets.
     */
    void setupPipeline(const std::string& vertShaderPath,
                       const std::string& fragShaderPath);

    // Frame-management helpers
    VkFixedArray<VkCommandBuffer>& getCommandBuffers() { return _cmdBuffers; }
    u32  getCurrentFrame() const { return _currentFrame; }
    void advanceFrame() { _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

protected:
    void createWindow(const char* title) override;

private:
    void setupGraphicsPipeline();
    void setupCommandBuffers();

private:
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;

    std::unique_ptr<aura3d::vk::VkHostAllocator>            _vkHostAllocator;
    std::unique_ptr<aura3d::vk::VkDeviceAllocator>          _vkDeviceAllocator;
    std::unique_ptr<aura3d::vk::VkInstanceManager>          _vkInstance;
    std::unique_ptr<aura3d::vk::VkDeviceManager>            _vkDeviceManager;
    std::unique_ptr<aura3d::vk::VkSurfaceManager>           _vkSurfaceManager;
    std::unique_ptr<aura3d::vk::VkSwapChainManager>         _vkSwapChainManager;
    std::unique_ptr<aura3d::vk::VkImageViewsManager>        _vkImageViewsManager;
    std::unique_ptr<aura3d::vk::VkRenderPassManager>        _vkRenderPassManager;
    std::unique_ptr<aura3d::vk::VkFrameBuffersManager>      _vkFrameBuffersManager;
    std::unique_ptr<aura3d::vk::VkGraphicsPipelineManager>  _vkGraphicsPipelineManager;
    std::unique_ptr<aura3d::vk::VkTextureManager>           _vkTextureManager;
    std::unique_ptr<aura3d::vk::VkDescriptorManager>        _vkDescriptorManager;
    std::unique_ptr<aura3d::vk::VkVertexBufferManager>      _vkVertexBufferManager;
    std::unique_ptr<aura3d::vk::VkIndexBufferManager>       _vkIndexBufferManager;
    std::unique_ptr<aura3d::vk::VkUniformBufferManager>     _vkUniformBufferManager;
    std::unique_ptr<aura3d::vk::VkCommandManager>           _vkCommandManager;
    std::unique_ptr<aura3d::vk::VkRenderSyncManager>        _vkRenderSyncManager;

    std::vector<aura3d::vk::QueueData*> _queueDataFromExclusiveFlags;
    u32 _graphicsIndexFamily = 0;

    VkInstanceData _vkInstanceData;
    VkDeviceData   _vkDeviceData;
    ImageViewData  _vkImageViewData;

    VkFixedArray<VkCommandBuffer> _cmdBuffers;

    u32  _currentFrame  = 0;
};

}
} // namespace aura3d

#endif // VULKAN_RENDERER_H
