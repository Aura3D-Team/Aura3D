#include "aura/Renderer/Vulkan/VkAura/VkRenderSyncManager/VkRenderSyncManager.h"

#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkRenderSyncManager::VkRenderSyncManager(VkDevice* device) :
    _device(device)
{
    // Empty
}

void VkRenderSyncManager::create(u32 imageCount)
{
    cleanup();

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = nullptr;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = nullptr;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    //! Bound to the CPU's run-ahead: one per frame slot.
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_RESULT_CHECK(vkCreateSemaphore(*_device, &semaphoreInfo, nullptr, &_imageAvailableSemaphores[i]));
        VK_RESULT_CHECK(vkCreateFence(*_device, &fenceInfo, nullptr, &_inFlightFences[i]));
    }

    //! Bound to presentation instead: one per swapchain image.
    _renderFinishedSemaphores.resize(imageCount, VK_NULL_HANDLE);
    for (u32 i = 0; i < imageCount; ++i)
    {
        VK_RESULT_CHECK(vkCreateSemaphore(*_device, &semaphoreInfo, nullptr, &_renderFinishedSemaphores[i]));
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

std::vector<VkSemaphore>& VkRenderSyncManager::getRenderFinishedSemaphores()
{
    return _renderFinishedSemaphores;
}

VkFixedArray<VkFence>& VkRenderSyncManager::getInFlightFences()
{
    return _inFlightFences;
}

void VkRenderSyncManager::cleanup()
{
    // Handles are nulled after destroying (vkDestroy* are no-ops on
    // VK_NULL_HANDLE per spec) so a second cleanup() before create() runs
    // again e.g. a failed swapchain-recovery retry that never reaches
    // create() doesn't double-destroy the same semaphore/fence.
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(*_device, _imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(*_device, _inFlightFences[i], nullptr);

        _imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        _inFlightFences[i] = VK_NULL_HANDLE;
    }

    for (VkSemaphore& semaphore : _renderFinishedSemaphores)
    {
        vkDestroySemaphore(*_device, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    }
    //! Cleared, not just nulled: create() sizes this from the new swapchain's
    //! image count, which a rebuild can change.
    _renderFinishedSemaphores.clear();
}

}
}
