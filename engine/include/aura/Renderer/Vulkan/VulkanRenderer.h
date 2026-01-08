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
 * @brief Vulkan implementation of the Renderer interface
 */
class VulkanRenderer : public IRenderer {
public:
    /**
     * @brief Construct a Vulkan renderer
     *
     * @param windowDetails Window configuration
     */
    VulkanRenderer(const wma::WindowDetails& windowDetails);

    /**
     * @brief Destroy the Vulkan renderer
     */
    virtual ~VulkanRenderer();

    /**
     * @brief Initialize the Vulkan renderer
     */
    void initialize() override;

    /**
     * @brief Run the main rendering loop
     */
    void run() override;

    /**
     * @brief Handle window changes (resize, etc.)
     */
    void handleWindowChanges() override;

    /**
     * @brief Clean up Vulkan resources
     */
    void cleanup() override;

protected:
    /**
     * @brief Create the window with Vulkan support
     *
     * @param title Window title
     */
    void createWindow(const char* title) override;

    /**
     * @brief Set up the Vulkan graphics pipeline
     */
    void setupGraphicsPipeline();

    /**
     * @brief Create vertex buffers for rendering
     */
    void createVertexBuffers();

    /**
     * @brief Create uniform buffers for shader parameters
     */
    void createUniformBuffers();

    /**
     * @brief Create descriptor sets for binding resources to shaders
     */
    void createDescriptorSets();

    /**
     * @brief Set up command buffers for rendering
     */
    void setupCommandBuffers();

private:
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;

    std::unique_ptr<aura3d::vk::VkHostAllocator> _vkHostAllocator;
    std::unique_ptr<aura3d::vk::VkDeviceAllocator> _vkDeviceAllocator;
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
    std::unique_ptr<aura3d::vk::VkCommandManager> _vkCommandManager;
    std::unique_ptr<aura3d::vk::VkRenderSyncManager> _vkRenderSyncManager;

    // Queues
    std::vector<aura3d::vk::QueueData*> _queueDataFromExclusiveFlags;
    u32 _graphicsIndexFamily;

    // Configuration data
    VkInstanceData _vkInstanceData;
    VkDeviceData _vkDeviceData;
    ImageViewData _vkImageViewData;

    VkFixedArray<VkCommandBuffer> _cmdBuffers;

    std::vector<VkDescriptorSet> _descriptorSets;
    std::vector<VkDescriptorSet> _textureDescriptorSets;

    // Rendering state
    u32 _currentFrame = 0;
    bool _isInitialized = false;
};

}
} // namespace aura3d

#endif // VULKAN_RENDERER_H
