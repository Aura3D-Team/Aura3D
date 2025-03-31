#include "VkRenderSyncManager.h"

#include "AuraException/AuraException.h"

#include <ink/ink.hpp>

namespace aura3d {

VkRenderSyncManager::VkRenderSyncManager(VkHostAllocator* vkHostAllocator, VkDevice* device) :
    vkHostAllocator(vkHostAllocator), _device(device)
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

    auto vkCallbacks = vkHostAllocator->getCallbacks();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_RESULT_CHECK(vkCreateSemaphore(*_device, &semaphoreInfo, vkCallbacks, &_imageAvailableSemaphores[i]));
        VK_RESULT_CHECK(vkCreateSemaphore(*_device, &semaphoreInfo, vkCallbacks, &_renderFinishedSemaphores[i]));
        VK_RESULT_CHECK(vkCreateFence(*_device, &fenceInfo, vkCallbacks, &_inFlightFences[i]));
    }
}

VkRenderSyncManager::~VkRenderSyncManager()
{
    cleanup();

    _device = nullptr;
}

void VkRenderSyncManager::waitForFences(const u32& fenceIndex)
{
    vkWaitForFences(*_device, 1, &_inFlightFences[fenceIndex], VK_TRUE, UINT64_MAX);
}

void VkRenderSyncManager::resetFences(const u32& fenceIndex)
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
    auto vkCallbacks = vkHostAllocator->getCallbacks();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(*_device, _imageAvailableSemaphores[i], vkCallbacks);
        vkDestroySemaphore(*_device, _renderFinishedSemaphores[i], vkCallbacks);
        vkDestroyFence(*_device, _inFlightFences[i], vkCallbacks);
    }
}

}
