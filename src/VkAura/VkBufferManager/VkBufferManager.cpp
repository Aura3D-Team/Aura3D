#include "VkBufferManager.h"
#include <aura.hpp>
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
                                   VkDeviceAllocation& allocation)
{
    // Create buffer object
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = sharingMode;

    VK_RESULT_CHECK(vkCreateBuffer(device, &bufferInfo, allocationCallbacks, &buffer));

    try {
        // Get the device allocator singleton
        VkDeviceAllocator& deviceAllocator = VkDeviceAllocator::getInstance();

        // Allocate memory for the buffer
        VK_RESULT_CHECK(deviceAllocator.allocateMemoryForBuffer(buffer, properties, allocation));

        // Bind the memory to the buffer
        VK_RESULT_CHECK(deviceAllocator.bindBufferMemory(buffer, allocation));
    }
    catch (const std::exception& e) {
        // Clean up if allocation fails
        vkDestroyBuffer(device, buffer, allocationCallbacks);
        buffer = VK_NULL_HANDLE;
        throw AuraException("Buffer creation failed: " + std::string(e.what()));
    }
}

void VkBufferManager::createBufferLegacy(VkDevice device,
                                         VkPhysicalDevice physicalDevice,
                                         VkDeviceSize size,
                                         VkBufferUsageFlags usage,
                                         VkSharingMode sharingMode,
                                         VkMemoryPropertyFlags properties,
                                         VkBuffer& buffer,
                                         VkDeviceMemory& bufferMemory,
                                         VkDeviceSize& bufferOffset)
{
    // Use the new implementation
    VkDeviceAllocation allocation;
    createBuffer(device, physicalDevice, size, usage, sharingMode, properties, buffer, allocation);

    // Extract legacy values for compatibility
    bufferMemory = allocation.memory;
    bufferOffset = allocation.offset;
}

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

    result = vkQueueSubmit(queue, 1, &submitInfo, fence);
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

void* VkBufferManager::mapBufferMemory(VkDeviceAllocation& allocation,
                                       VkDeviceSize offset,
                                       VkDeviceSize size)
{
    void* data = nullptr;

    VK_RESULT_CHECK(VkDeviceAllocator::getInstance().mapMemory(allocation, offset, size, &data));

    return data;
}

void VkBufferManager::unmapBufferMemory(VkDeviceAllocation& allocation)
{
    VkDeviceAllocator::getInstance().unmapMemory(allocation);
}

void VkBufferManager::destroyBuffer(VkDevice device,
                                    VkBuffer buffer,
                                    VkDeviceAllocation& allocation)
{
    if (buffer != VK_NULL_HANDLE) {
        // Get the device allocator singleton
        VkDeviceAllocator& deviceAllocator = VkDeviceAllocator::getInstance();

        // Destroy the buffer
        vkDestroyBuffer(device, buffer, allocationCallbacks);

        // Free the memory using the allocator
        deviceAllocator.freeMemory(allocation);
    }
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
