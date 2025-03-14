#ifndef VKRUNNER_H
#define VKRUNNER_H

#pragma once

#ifdef SDL_WINDOW_MANAGER
#include <AuraWindowManagers/SDLAuraWindowManager/SDLAuraWindowManager.h>
#else
#include <AuraWindowManagers/GlfwWindowManager/GlfwAuraWindowManager.h>
#endif

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
#include <VkAura/VkBufferMemoryAllocator/VkBufferMemoryAllocator.h>
#include <VkAura/VkVertexBufferManager/VkVertexBufferManager.h>
#include <VkAura/VkUniformBufferManager/VkUniformBufferManager.h>
#include <VkAura/VkCommandManager/VkCommandManager.h>
#include <VkAura/VkRenderSyncManager/VkRenderSyncManager.h>

namespace aura3d {

class VkRunner
{
public:
    VkRunner(WindowDetails windowDetails,
             VkInstanceData vkInstanceData,
             VkDeviceData vkDeviceData,
             ImageViewData vkImageViewData);

    ~VkRunner();

    void run();

    void handleWindowChanges();

    void cleanup();
private:
#ifdef SDL_WINDOW_MANAGER
    std::unique_ptr<aura3d::SDLAuraWindowManager> _windowManagerApi;
#else
    std::unique_ptr<aura3d::GlfwAuraWindowManager> _windowManagerApi;
#endif

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
    std::unique_ptr<aura3d::VkBufferMemoryAllocator> _vkBufferMemoryAllocator;
    std::unique_ptr<aura3d::VkVertexBufferManager> _vkVertexBufferManager;
    std::unique_ptr<aura3d::VkUniformBufferManager> _vkUniformBufferManager;
    std::unique_ptr<aura3d::VkCommandManager> _vkCommandManager;
    std::unique_ptr<aura3d::VkRenderSyncManager> _vkRenderSyncManager;

    // Queues
    std::vector<aura3d::QueueData*> _queueDataFromExclusiveFlags;
    uint32_t _graphicsIndexFamily;

    // ImageViews
    ImageViewData _vkImageViewData;
};

}

#endif // VKRUNNER_H
