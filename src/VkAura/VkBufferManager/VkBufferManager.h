#ifndef VKBUFFERMANAGER_H
#define VKBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>

#include <VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h>
#include <VkAura/VkMemory/VkDeviceAllocator/VkDeviceAllocator.h>

namespace aura3d {

class VkBufferManager {
public:
    VkBufferManager();
    ~VkBufferManager();

    /**
     * Creates a Vulkan buffer using the VkDeviceAllocator
     *
     * @param device Vulkan logical device
     * @param physicalDevice Vulkan physical device (used for error reporting only)
     * @param size Size of the buffer in bytes
     * @param usage Buffer usage flags
     * @param sharingMode Buffer sharing mode (exclusive or concurrent)
     * @param properties Memory property flags (e.g., HOST_VISIBLE, DEVICE_LOCAL)
     * @param buffer Output parameter for the created buffer handle
     * @param allocation Output parameter for the device allocation
     */
    static void createBuffer(VkHostAllocator* vkHostAllocator,
                             VkDeviceAllocator* vkDeviceAllocator,
                             VkDevice device,
                             VkPhysicalDevice physicalDevice,
                             VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkSharingMode sharingMode,
                             VkMemoryPropertyFlags properties,
                             VkBuffer& buffer,
                             VkDeviceAllocation& allocation);
    /**
     * Copies data between buffers using a command buffer
     *
     * @param device Vulkan logical device
     * @param commandPool Command pool to allocate command buffer from
     * @param queue Queue to submit the copy command to
     * @param srcBuffer Source buffer handle
     * @param dstBuffer Destination buffer handle
     * @param size Size of data to copy in bytes
     * @param srcOffset Offset in source buffer (default: 0)
     * @param dstOffset Offset in destination buffer (default: 0)
     */
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
     * Destroys a buffer and frees its memory using the VkDeviceAllocator
     *
     * @param device Vulkan logical device
     * @param buffer Buffer handle to destroy
     * @param allocation Device allocation to free
     */
    static void destroyBuffer(VkHostAllocator* vkHostAllocator,
                              VkDeviceAllocator* vkDeviceAllocator,
                              VkDevice device,
                              VkBuffer buffer,
                              VkDeviceAllocation& allocation);

    /**
     * Executes a single command in a command buffer
     *
     * @param device Vulkan logical device
     * @param commandPool Command pool to allocate from
     * @param queue Queue to submit to
     * @param command Function that takes a command buffer and records commands
     */
    static void executeImmediateCommand(VkDevice device,
                                        VkCommandPool commandPool,
                                        VkQueue queue,
                                        void (*command)(VkCommandBuffer commandBuffer));
};

} // namespace aura3d

#endif // VKBUFFERMANAGER_H
