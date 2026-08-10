#ifndef VKRENDERSYNCMANAGER_H
#define VKRENDERSYNCMANAGER_H

#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"

namespace aura3d {
namespace vk {

/**
 * @class VkRenderSyncManager
 * @brief Owns the per-frame and per-image synchronization primitives.
 *
 *  - **Per frame in flight** (@c imageAvailable, @c inFlight): these bound the
 *    CPU's run-ahead. The fence is what makes it safe to overwrite frame slot
 *    N's command buffers and uniform buffers, so it is indexed the same way
 *    they are.
 *
 *  - **Per swapchain image** (@c renderFinished): this one is signalled by
 *    rendering and waited on by *vkQueuePresentKHR*, whose completion the
 *    frame fence says nothing about. A presentation engine may hold a frame
 *    long after the render that produced it retired, so a semaphore keyed on
 *    the frame slot can come back around while its previous present is still
 *    outstanding -- signalling an already-signalled binary semaphore, which
 *    is what VUID-vkQueueSubmit-pSignalSemaphores-00067 forbids. Keying it on
 *    the image ties its lifetime to the thing that actually releases it: the
 *    image cannot be acquired again until its present is done.
 */
class VkRenderSyncManager
{
public:
    VkRenderSyncManager(VkDevice* device);
    ~VkRenderSyncManager();

    /**
     * @brief Creates the sync objects.
     * @param imageCount Swapchain image count, sizing the render-finished
     *                   semaphores. Must be re-called if the swapchain is
     *                   rebuilt with a different image count.
     */
    void create(u32 imageCount);

    void waitForFences(const u32& fenceIndex);
    void resetFences(const u32& fenceIndex);

    VkFixedArray<VkSemaphore>& getImageAvailableSemaphores();

    //! Indexed by *swapchain image*, not frame in flight -- see the class note.
    std::vector<VkSemaphore>& getRenderFinishedSemaphores();

    VkFixedArray<VkFence>& getInFlightFences();

    void cleanup();

private:
    VkDevice* _device;

    /*
     * Value-initialized to VK_NULL_HANDLE. std::array of a handle type is
     * otherwise left indeterminate, and cleanup() runs before the first
     * create() (it is called from there to keep create() idempotent across
     * swapchain rebuilds) -- so without this it would hand vkDestroy* a stack
     * garbage handle, which validation catches as
     * VUID-vkDestroySemaphore-semaphore-parameter.
     */
    VkFixedArray<VkSemaphore> _imageAvailableSemaphores{};
    std::vector<VkSemaphore> _renderFinishedSemaphores;
    VkFixedArray<VkFence> _inFlightFences{};
};

}
}

#endif // VKRENDERSYNCMANAGER_H
