#include "VkRenderSyncManager.h"

#include "VkAura/VkException/VkException.h"

#include <plog/Log.h>

namespace aura3d {

VkRenderSyncManager::VkRenderSyncManager(VkDevice* device) :
    _device(device)
{
    // Empty
}

void VkRenderSyncManager::create()
{
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = nullptr;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = nullptr;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_RESULT_CHECK(vkCreateSemaphore(*_device, &semaphoreInfo, nullptr, &_imageAvailableSemaphores[i]));
        VK_RESULT_CHECK(vkCreateSemaphore(*_device, &semaphoreInfo, nullptr, &_renderFinishedSemaphores[i]));
        VK_RESULT_CHECK(vkCreateFence(*_device, &fenceInfo, nullptr, &_inFlightFences[i]));
    }
}

VkRenderSyncManager::~VkRenderSyncManager()
{
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(*_device, _imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(*_device, _renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(*_device, _inFlightFences[i], nullptr);
    }

    _device = nullptr;
}

void VkRenderSyncManager::waitForFences(const uint32_t& fenceIndex)
{
    vkWaitForFences(*_device, 1, &_inFlightFences[fenceIndex], VK_TRUE, UINT64_MAX);
}

void VkRenderSyncManager::resetFences(const uint32_t& fenceIndex)
{
    vkResetFences(*_device, 1, &_inFlightFences[fenceIndex]);
}

VkFixedArray<VkSemaphore>& VkRenderSyncManager::getImageAvailableSemaphores()
{
    return _imageAvailableSemaphores;
}

VkFixedArray<VkSemaphore>& VkRenderSyncManager::getRenderFinishedSemaphores()
{
    return _renderFinishedSemaphores;
}

VkFixedArray<VkFence>& VkRenderSyncManager::getInFlightFences()
{
    return _inFlightFences;
}

void VkRenderSyncManager::cleanup()
{
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(*_device, _imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(*_device, _renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(*_device, _inFlightFences[i], nullptr);
    }
}

}
