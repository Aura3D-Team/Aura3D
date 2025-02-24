#ifndef VKRENDERSYNCMANAGER_H
#define VKRENDERSYNCMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <VkAura/VkCommon.h>

namespace aura3d {

class VkRenderSyncManager
{
public:
    VkRenderSyncManager(VkDevice* device);
    ~VkRenderSyncManager();

    void waitForFences(const uint32_t& fenceIndex);
    void resetFences(const uint32_t& fenceIndex);

    VkFixedArray<VkSemaphore>& getImageAvailableSemaphores();
    VkFixedArray<VkSemaphore>& getRenderFinishedSemaphores();
    VkFixedArray<VkFence>& getInFlightFences();

private:
    VkDevice* _device;

    VkFixedArray<VkSemaphore> _imageAvailableSemaphores;
    VkFixedArray<VkSemaphore> _renderFinishedSemaphores;
    VkFixedArray<VkFence> _inFlightFences;
};

}

#endif // VKRENDERSYNCMANAGER_H
