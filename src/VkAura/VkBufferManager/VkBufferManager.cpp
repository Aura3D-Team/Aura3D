#include "VkBufferManager.h"

#include "VkAura/VkException/VkException.h"

namespace aura3d {

VkBufferManager::VkBufferManager()
{
    // Empty
}

VkBufferManager::~VkBufferManager()
{
    // Empty
}

void VkBufferManager::createBuffer(VkDevice device,
                  VkPhysicalDevice physicalDevice,
                  VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkSharingMode sharingMode,
                  VkMemoryPropertyFlags properties,
                  VkBuffer& buffer,
                  VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = sharingMode;

    VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
    VK_RESULT_CHECK(result);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = VkBufferManager::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    result = vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory);
    VK_RESULT_CHECK(result);

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void VkBufferManager::bufferCopy(VkDevice device,
                VkCommandPool commandPool,
                VkQueue queue,
                VkBuffer srcBuffer,
                VkBuffer dstBuffer,
                VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkResult result = VK_NOT_READY;

    VkCommandBuffer commandBuffer;
    result = vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    VK_RESULT_CHECK(result);

    VkCommandBufferBeginInfo cmdBufferInfo = {};
    cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(commandBuffer, &cmdBufferInfo);
    VK_RESULT_CHECK(result);

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    result = vkEndCommandBuffer(commandBuffer);
    VK_RESULT_CHECK(result);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    VK_RESULT_CHECK(result);

    result = vkQueueWaitIdle(queue);
    VK_RESULT_CHECK(result);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void* VkBufferManager::mapBufferMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize size)
{
    void* data;
    VkResult result = vkMapMemory(device, memory, 0, size, VK_MEMORY_MAP_PLACED_BIT_EXT, &data);
    VK_RESULT_CHECK(result);

    return data;
}

void VkBufferManager::unmapBufferMemory(VkDevice device, VkDeviceMemory memory) {
    vkUnmapMemory(device, memory);
}

void VkBufferManager::destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory) {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

uint32_t VkBufferManager::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties deviceMemProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemProperties);

    for (uint32_t i = 0; i < deviceMemProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (deviceMemProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    throw aura3d::VkException("Failed to find suitable memory type!");
}

}
