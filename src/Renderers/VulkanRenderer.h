#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#pragma once

#include "Renderers/Renderer.h"

#include <VkAura/VkInstanceManager/VkInstanceManager.h>
#include <VkAura/VkDeviceManager/VkDeviceManager.h>
#include <VkAura/VkSurfaceManager/VkSurfaceManager.h>
#include <VkAura/VkSwapChainManager/VkSwapChainManager.h>
#include <VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h>
#include <VkAura/VkImageViewsManager/VkImageViewsManager.h>
#include <VkAura/VkRenderPassManager/VkRenderPassManager.h>
#include <VkAura/VkFrameBuffersManager/VkFrameBuffersManager.h>
#include <VkAura/VkDescriptorManager/VkDescriptorManager.h>
#include <VkAura/VkTextureManager/VkTextureManager.h>
#include <VkAura/VkVertexBufferManager/VkVertexBufferManager.h>
#include <VkAura/VkIndexBufferManager/VkIndexBufferManager.h>
#include <VkAura/VkUniformBufferManager/VkUniformBufferManager.h>
#include <VkAura/VkCommandManager/VkCommandManager.h>
#include <VkAura/VkRenderSyncManager/VkRenderSyncManager.h>

namespace aura3d {

/**
 * @brief Vulkan implementation of the Renderer interface
 */
class VulkanRenderer : public Renderer {
public:
    /**
     * @brief Construct a Vulkan renderer
     *
     * @param windowDetails Window configuration
     * @param vkInstanceData Vulkan instance configuration
     * @param vkDeviceData Vulkan device configuration
     * @param vkImageViewData Image view configuration
     */
    VulkanRenderer(
        const wma::WindowDetails& windowDetails,
        const VkInstanceData& vkInstanceData,
        const VkDeviceData& vkDeviceData,
        const ImageViewData& vkImageViewData
    );

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

    std::unique_ptr<aura3d::VkHostAllocator> _vkHostAllocator;
    std::unique_ptr<aura3d::VkDeviceAllocator> _vkDeviceAllocator;
    std::unique_ptr<aura3d::VkInstanceManager> _vkInstance;
    std::unique_ptr<aura3d::VkDeviceManager> _vkDeviceManager;
    std::unique_ptr<aura3d::VkSurfaceManager> _vkSurfaceManager;
    std::unique_ptr<aura3d::VkSwapChainManager> _vkSwapChainManager;
    std::unique_ptr<aura3d::VkImageViewsManager> _vkImageViewsManager;
    std::unique_ptr<aura3d::VkRenderPassManager> _vkRenderPassManager;
    std::unique_ptr<aura3d::VkFrameBuffersManager> _vkFrameBuffersManager;
    std::unique_ptr<aura3d::VkGraphicsPipelineManager> _vkGraphicsPipelineManager;
    std::unique_ptr<aura3d::VkTextureManager> _vkTextureManager;
    std::unique_ptr<aura3d::VkDescriptorManager> _vkDescriptorManager;
    std::unique_ptr<aura3d::VkVertexBufferManager> _vkVertexBufferManager;
    std::unique_ptr<aura3d::VkIndexBufferManager> _vkIndexBufferManager;
    std::unique_ptr<aura3d::VkUniformBufferManager> _vkUniformBufferManager;
    std::unique_ptr<aura3d::VkCommandManager> _vkCommandManager;
    std::unique_ptr<aura3d::VkRenderSyncManager> _vkRenderSyncManager;

    // Queues
    std::vector<aura3d::QueueData*> _queueDataFromExclusiveFlags;
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

} // namespace aura3d

#endif // VULKAN_RENDERER_H
