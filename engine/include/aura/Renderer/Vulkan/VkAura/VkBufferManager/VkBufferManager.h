#ifndef VKBUFFERMANAGER_H
#define VKBUFFERMANAGER_H

#pragma once

#include <concepts>
#include <functional>
#include <utility>

#include <vulkan/vulkan.h>

#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

class VkBufferManager {
public:
    static void bufferCopy(VkDevice device,
                           VkCommandPool commandPool,
                           VkQueue queue,
                           VkBuffer srcBuffer,
                           VkBuffer dstBuffer,
                           VkFence fence,
                           VkDeviceSize size,
                           VkDeviceSize srcOffset = 0,
                           VkDeviceSize dstOffset = 0);

    /**
     * @brief Records @p command into a throwaway primary buffer, submits it,
     *        and blocks until it has retired.
     *
     * @param fence Optional caller-owned fence to signal on completion. When
     *              VK_NULL_HANDLE, a transient one is created for the wait --
     *              either way the wait is scoped to *this* submission rather
     *              than draining the whole queue, so unrelated work already in
     *              flight on @p queue keeps running.
     */
    template<std::invocable<VkCommandBuffer> F>
    static void executeImmediateCommand(VkDevice device,
                                        VkCommandPool commandPool,
                                        VkQueue queue,
                                        F&& command,
                                        VkFence fence = VK_NULL_HANDLE);

private:
    /**
     * @brief RAII owner for a single-use Vulkan handle freed by a lambda.
     *
     * executeImmediateCommand() has three throw points after it has acquired a
     * command buffer (VK_RESULT_CHECK on end/submit/wait, plus whatever the
     * caller's own recording lambda throws). A trailing free call leaks the
     * buffer on every one of them; a destructor cannot.
     */
    template<typename Handle, typename Deleter>
    class ScopedHandle {
    public:
        ScopedHandle(Handle handle, Deleter deleter) noexcept
            : _handle(handle), _deleter(std::move(deleter)) {}

        ~ScopedHandle() { if (_handle != VK_NULL_HANDLE) _deleter(_handle); }

        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;

        [[nodiscard]] Handle get() const noexcept { return _handle; }

    private:
        Handle _handle;
        Deleter _deleter;
    };
};

template<std::invocable<VkCommandBuffer> F>
void VkBufferManager::executeImmediateCommand(VkDevice device,
                                              VkCommandPool commandPool,
                                              VkQueue queue,
                                              F&& command,
                                              VkFence fence)
{
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer rawCommandBuffer = VK_NULL_HANDLE;
    VK_RESULT_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &rawCommandBuffer));

    ScopedHandle commandBuffer(rawCommandBuffer, [device, commandPool](VkCommandBuffer buffer) noexcept {
        vkFreeCommandBuffers(device, commandPool, 1, &buffer);
    });

    /*
     * A fence is what makes the wait specific to this submission. When the
     * caller supplied one we signal theirs and wait on it; otherwise this owns
     * a transient one for the duration. Either way ownedFence is only non-null
     * when we created it, which is exactly when it must be destroyed.
     */
    VkFence rawOwnedFence = VK_NULL_HANDLE;
    if (fence == VK_NULL_HANDLE)
    {
        //! Created unsignalled, which is the state vkQueueSubmit requires.
        const VkFenceCreateInfo fenceInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VK_RESULT_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &rawOwnedFence));
        fence = rawOwnedFence;
    }
    else
    {
        //! A caller-owned fence may arrive signalled from its last use. Both
        //! the submit (which rejects a signalled fence) and the wait below
        //! (which would return instantly) depend on clearing it first.
        VK_RESULT_CHECK(vkResetFences(device, 1, &fence));
    }

    ScopedHandle ownedFence(rawOwnedFence, [device](VkFence handle) noexcept {
        vkDestroyFence(device, handle, nullptr);
    });

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_RESULT_CHECK(vkBeginCommandBuffer(commandBuffer.get(), &beginInfo));

    std::invoke(std::forward<F>(command), commandBuffer.get());

    VK_RESULT_CHECK(vkEndCommandBuffer(commandBuffer.get()));

    const VkCommandBuffer submitted = commandBuffer.get();
    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &submitted,
    };
    VK_RESULT_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
    VK_RESULT_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
}

} // namespace vk
} // namespace aura3d

#endif // VKBUFFERMANAGER_H
