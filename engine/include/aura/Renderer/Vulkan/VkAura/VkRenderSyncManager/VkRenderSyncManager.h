#ifndef VKRENDERSYNCMANAGER_H
#define VKRENDERSYNCMANAGER_H

#pragma once

#include <vulkan/vulkan.h>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"

namespace aura3d {
namespace vk {

class VkRenderSyncManager
{
public:
    VkRenderSyncManager(VkDevice* device);
    ~VkRenderSyncManager();

    void create();

    void waitForFences(const u32& fenceIndex);
    void resetFences(const u32& fenceIndex);

    VkFixedArray<VkSemaphore>& getImageAvailableSemaphores();
    VkFixedArray<VkSemaphore>& getRenderFinishedSemaphores();
    VkFixedArray<VkFence>& getInFlightFences();

    void cleanup();

private:
    VkDevice* _device;

    VkFixedArray<VkSemaphore> _imageAvailableSemaphores;
    VkFixedArray<VkSemaphore> _renderFinishedSemaphores;
    VkFixedArray<VkFence> _inFlightFences;
};

}
}

#endif // VKRENDERSYNCMANAGER_H
