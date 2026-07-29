#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"

#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

void VkBufferManager::bufferCopy(VkDevice device,
                                 VkCommandPool commandPool,
                                 VkQueue queue,
                                 VkBuffer srcBuffer,
                                 VkBuffer dstBuffer,
                                 VkFence fence,
                                 VkDeviceSize size,
                                 VkDeviceSize srcOffset,
                                 VkDeviceSize dstOffset)
{
    executeImmediateCommand(device, commandPool, queue, [&](VkCommandBuffer commandBuffer) {
        const VkBufferCopy copyRegion{
            .srcOffset = srcOffset,
            .dstOffset = dstOffset,
            .size = size,
        };
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    }, fence);
}

} // namespace vk
} // namespace aura3d
