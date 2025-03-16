#ifndef VKRENDERSYNCMANAGER_H
#define VKRENDERSYNCMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <aura.hpp>
#include <VkAura/VkHostAllocator/VkHostAllocator.h>

namespace aura3d {

class VkRenderSyncManager
{
public:
    VkRenderSyncManager(VkHostAllocator* vkHostAllocator, VkDevice* device);
    ~VkRenderSyncManager();

    void create();

    void waitForFences(const uint32_t& fenceIndex);
    void resetFences(const uint32_t& fenceIndex);

    VkFixedArray<VkSemaphore>& getImageAvailableSemaphores();
    VkFixedArray<VkSemaphore>& getRenderFinishedSemaphores();
    VkFixedArray<VkFence>& getInFlightFences();

    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDevice* _device;

    VkFixedArray<VkSemaphore> _imageAvailableSemaphores;
    VkFixedArray<VkSemaphore> _renderFinishedSemaphores;
    VkFixedArray<VkFence> _inFlightFences;
};

}

#endif // VKRENDERSYNCMANAGER_H
