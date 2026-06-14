#ifndef VKBUFFERMANAGER_H
#define VKBUFFERMANAGER_H

#pragma once

#include <concepts>
#include <vulkan/vulkan.h>

#include <utility>

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

    template<std::invocable<VkCommandBuffer> F>
    static void executeImmediateCommand(VkDevice device,
                                        VkCommandPool commandPool,
                                        VkQueue queue,
                                        F&& command,
                                        VkFence fence = VK_NULL_HANDLE);
};

template<std::invocable<VkCommandBuffer> F>
void VkBufferManager::executeImmediateCommand(VkDevice device,
                                              VkCommandPool commandPool,
                                              VkQueue queue,
                                              F&& command,
                                              VkFence fence)
{
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VK_RESULT_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

    const auto releaseCommandBuffer = [&]() noexcept {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    };

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_RESULT_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    std::invoke(std::forward<F>(command), commandBuffer);

    VK_RESULT_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    VK_RESULT_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
    VK_RESULT_CHECK(vkQueueWaitIdle(queue));

    releaseCommandBuffer();
}

} // namespace vk
} // namespace aura3d

#endif // VKBUFFERMANAGER_H
