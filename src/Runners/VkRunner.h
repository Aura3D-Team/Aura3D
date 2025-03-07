#ifndef VKRUNNER_H
#define VKRUNNER_H

#pragma once

#include <GlfwAura/GlfwWindowManager/GlfwWindowManager.h>
#include <VkAura/VkInstanceManager/VkInstanceManager.h>
#include <VkAura/VkDeviceManager/VkDeviceManager.h>
#include <VkAura/VkSurfaceManager/VkSurfaceManager.h>
#include <VkAura/VkSwapChainManager/VkSwapChainManager.h>
#include <VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h>
#include <VkAura/VkImageViewsManager/VkImageViewsManager.h>
#include <VkAura/VkRenderPassManager/VkRenderPassManager.h>
#include <VkAura/VkCommandManager/VkCommandManager.h>
#include <VkAura/VkFrameBuffersManager/VkFrameBuffersManager.h>
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
private:
    std::unique_ptr<aura3d::GlfwWindowManager> _glfwWindowManager;

    std::unique_ptr<aura3d::VkInstanceManager> _vkInstance;
    std::unique_ptr<aura3d::VkDeviceManager> _vkDeviceManager;
    std::unique_ptr<aura3d::VkSurfaceManager> _vkSurfaceManager;
    std::unique_ptr<aura3d::VkSwapChainManager> _vkSwapChainManager;
    std::unique_ptr<aura3d::VkImageViewsManager> _vkImageViewsManager;
    std::unique_ptr<aura3d::VkRenderPassManager> _vkRenderPassManager;
    std::unique_ptr<aura3d::VkFrameBuffersManager> _vkFrameBuffersManager;
    std::unique_ptr<aura3d::VkGraphicsPipelineManager> _vkGraphicsPipelineManager;
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
