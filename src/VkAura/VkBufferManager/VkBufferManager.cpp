#include "VkBufferManager.h"

#include "AuraException/AuraException.h"

#include <cstring>

namespace aura3d {

VkBufferManager::VkBufferManager()
{
    // Empty constructor - all methods are static
}

VkBufferManager::~VkBufferManager()
{
    // Empty destructor - all methods are static
}

void VkBufferManager::createBuffer(VkDevice device,
                                   VkPhysicalDevice physicalDevice,
                                   VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   VkSharingMode sharingMode,
                                   VkMemoryPropertyFlags properties,
                                   VkBuffer& buffer,
                                   VkDeviceMemory& bufferMemory,
                                   VkDeviceSize& bufferOffset,
                                   VkBufferMemoryAllocator* allocator)
{
    // Create buffer object
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = sharingMode;

    VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        throw AuraException("Failed to create buffer! Error code: " + std::to_string(result));
    }

    try {
        // Allocate memory from the allocator
        AllocationInfo allocation = allocator->allocate(buffer, properties);

        // Store the memory handle and offset
        bufferMemory = allocation.memory;
        bufferOffset = allocation.offset;

        // Note: We don't need to call vkBindBufferMemory here because
        // the allocator already does it for us
    }
    catch (const AuraException& e) {
        // Clean up if allocation fails
        vkDestroyBuffer(device, buffer, nullptr);
        throw; // Re-throw the exception
    }
}

void VkBufferManager::bufferCopy(VkDevice device,
                                 VkCommandPool commandPool,
                                 VkQueue queue,
                                 VkBuffer srcBuffer,
                                 VkBuffer dstBuffer,
                                 VkDeviceSize size,
                                 VkDeviceSize srcOffset,
                                 VkDeviceSize dstOffset)
{
    // Allocate a command buffer
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        throw AuraException("Failed to allocate command buffer! Error code: " + std::to_string(result));
    }

    // Begin command buffer recording
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw AuraException("Failed to begin command buffer! Error code: " + std::to_string(result));
    }

    // Record copy command
    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    // End command buffer recording
    result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw AuraException("Failed to end command buffer! Error code: " + std::to_string(result));
    }

    // Submit command buffer to queue
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw AuraException("Failed to submit command buffer! Error code: " + std::to_string(result));
    }

    // Wait for the queue to finish operations
    result = vkQueueWaitIdle(queue);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw AuraException("Failed to wait for queue idle! Error code: " + std::to_string(result));
    }

    // Free the command buffer
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void* VkBufferManager::mapBufferMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, VkDeviceSize offset)
{
    void* data = nullptr;
    VkResult result = vkMapMemory(device, memory, offset, size, 0, &data);

    if (result != VK_SUCCESS) {
        throw AuraException("Failed to map memory! Error code: " + std::to_string(result));
    }

    return data;
}

void VkBufferManager::unmapBufferMemory(VkDevice device, VkDeviceMemory memory)
{
    vkUnmapMemory(device, memory);
}

void VkBufferManager::destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
{
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
    }
}

uint32_t VkBufferManager::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    // Get memory properties of the physical device
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    // Find a suitable memory type that satisfies our requirements
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        bool typeMatch = (typeFilter & (1 << i)) != 0;
        bool propertyMatch = (memProperties.memoryTypes[i].propertyFlags & properties) == properties;

        if (typeMatch && propertyMatch) {
            return i;
        }
    }

    throw AuraException("Failed to find suitable memory type!");
}

void VkBufferManager::executeImmediateCommand(VkDevice device,
                                              VkCommandPool commandPool,
                                              VkQueue queue,
                                              void (*command)(VkCommandBuffer))
{
    // Allocate a command buffer
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        throw AuraException("Failed to allocate command buffer! Error code: " + std::to_string(result));
    }

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw AuraException("Failed to begin command buffer! Error code: " + std::to_string(result));
    }

    // Execute the command function, which should record commands to the command buffer
    command(commandBuffer);

    // End command buffer
    result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw AuraException("Failed to end command buffer! Error code: " + std::to_string(result));
    }

    // Submit command buffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw AuraException("Failed to submit command buffer! Error code: " + std::to_string(result));
    }

    // Wait for the queue to finish
    result = vkQueueWaitIdle(queue);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw AuraException("Failed to wait for queue idle! Error code: " + std::to_string(result));
    }

    // Free the command buffer
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

} // namespace aura3d
