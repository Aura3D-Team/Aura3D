#ifndef VKRENDERSYNCMANAGER_H
#define VKRENDERSYNCMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VkRenderSyncManager
{
public:
    VkRenderSyncManager(VkDevice* device, const uint32_t& framesInFlight);
    ~VkRenderSyncManager();

    void waitForFences(const uint32_t& fenceIndex);
    void resetFences(const uint32_t& fenceIndex);

    std::vector<VkSemaphore>& getImageAvailableSemaphores();
    std::vector<VkSemaphore>& getRenderFinishedSemaphores();
    std::vector<VkFence>& getInFlightFences();

private:
    VkDevice* _device;

    uint32_t _framesInFlight;
    std::vector<VkSemaphore> _imageAvailableSemaphores;
    std::vector<VkSemaphore> _renderFinishedSemaphores;
    std::vector<VkFence> _inFlightFences;
};

#endif // VKRENDERSYNCMANAGER_H
