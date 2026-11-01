#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"

#include <cstring>

#include "aura/Core/AuraException/AuraException.h"

namespace aura3d
{
namespace vk
{

void VkBufferManager::bufferCopy(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer srcBuffer,
                                 VkBuffer dstBuffer, VkFence fence, VkDeviceSize size, VkDeviceSize srcOffset,
                                 VkDeviceSize dstOffset)
{
    executeImmediateCommand(
        device, commandPool, queue,
        [&](VkCommandBuffer commandBuffer)
        {
            const VkBufferCopy copyRegion{
                .srcOffset = srcOffset,
                .dstOffset = dstOffset,
                .size = size,
            };
            vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
        },
        fence);
}

AllocatedBuffer VkBufferManager::uploadDeviceLocal(VulkanMemoryManager &memory, VkDevice device,
                                                   VkCommandPool commandPool, VkQueue queue, VkSharingMode sharingMode,
                                                   std::span<const std::byte> bytes, VkBufferUsageFlags usage)
{
    const VkDeviceSize size = bytes.size();
    AllocatedBuffer staging = memory.createUploadBuffer(size, sharingMode);
    INK_DEFER
    {
        memory.destroyBuffer(staging);
    };

    const bool manuallyMapped = staging.mappedData == nullptr;
    std::memcpy(manuallyMapped ? memory.map(staging) : staging.mappedData, bytes.data(), bytes.size());
    const VkResult flushResult = vmaFlushAllocation(memory.getAllocator(), staging.allocation, 0, size);
    if (manuallyMapped)
        memory.unmap(staging);
    VK_RESULT_CHECK(flushResult);

    AllocatedBuffer gpu = memory.createDeviceLocalBuffer(size, usage, sharingMode);
    bufferCopy(device, commandPool, queue, staging.buffer, gpu.buffer, VK_NULL_HANDLE, size);
    return gpu;
}

} // namespace vk
} // namespace aura3d
