#ifndef VKBUFFERMANAGER_H
#define VKBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <VkAura/VkBufferMemoryAllocator/VkBufferMemoryAllocator.h>

namespace aura3d {
class VkBufferManager
{
public:
    VkBufferManager();
    ~VkBufferManager();

    /**
         * Creates a Vulkan buffer
         *
         * @param device Vulkan logical device
         * @param physicalDevice Vulkan physical device
         * @param size Size of the buffer in bytes
         * @param usage Buffer usage flags
         * @param sharingMode Buffer sharing mode (exclusive or concurrent)
         * @param properties Memory property flags (e.g., HOST_VISIBLE, DEVICE_LOCAL)
         * @param buffer Output parameter for the created buffer handle
         * @param bufferMemory Output parameter for the allocated memory handle
         */
    static void createBuffer(VkDevice device,
                              VkPhysicalDevice physicalDevice,
                              VkDeviceSize size,
                              VkBufferUsageFlags usage,
                              VkSharingMode sharingMode,
                              VkMemoryPropertyFlags properties,
                              VkBuffer& buffer,
                              VkDeviceMemory& bufferMemory,
                              VkDeviceSize& bufferOffset,
                              VkBufferMemoryAllocator* allocator);

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
                           VkDeviceSize size,
                           VkDeviceSize srcOffset = 0,
                           VkDeviceSize dstOffset = 0);

    /**
         * Maps memory for host access
         *
         * @param device Vulkan logical device
         * @param memory Memory handle to map
         * @param size Size to map (can be VK_WHOLE_SIZE)
         * @param offset Offset into the memory (default: 0)
         * @return Pointer to mapped memory
         */
    static void* mapBufferMemory(VkDevice device,
                                 VkDeviceMemory memory,
                                 VkDeviceSize size,
                                 VkDeviceSize offset = 0);

    /**
         * Unmaps previously mapped memory
         *
         * @param device Vulkan logical device
         * @param memory Memory handle to unmap
         */
    static void unmapBufferMemory(VkDevice device, VkDeviceMemory memory);

    /**
         * Destroys a buffer and frees its memory
         *
         * @param device Vulkan logical device
         * @param buffer Buffer handle to destroy
         * @param memory Memory handle to free
         */
    static void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);

    /**
         * Finds a memory type index that satisfies requirements
         *
         * @param physicalDevice Vulkan physical device
         * @param typeFilter Memory type bits from vkGetBufferMemoryRequirements
         * @param properties Desired memory properties
         * @return Memory type index
         */
    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                   uint32_t typeFilter,
                                   VkMemoryPropertyFlags properties);

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
}
#endif // VKBUFFERMANAGER_H
