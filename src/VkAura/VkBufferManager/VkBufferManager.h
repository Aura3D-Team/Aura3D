#ifndef VKBUFFERMANAGER_H
#define VKBUFFERMANAGER_H
#pragma once
#include <vulkan/vulkan.h>
#include <VkAura/VkDeviceAllocator/VkDeviceAllocator.h>

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
    static void createBuffer(VkDevice device,
                             VkPhysicalDevice physicalDevice,
                             VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkSharingMode sharingMode,
                             VkMemoryPropertyFlags properties,
                             VkBuffer& buffer,
                             VkDeviceAllocation& allocation);

    /**
     * Creates a buffer and returns a legacy-format memory handle and offset
     * (Transitional API for backward compatibility)
     *
     * @param device Vulkan logical device
     * @param physicalDevice Vulkan physical device
     * @param size Size of the buffer in bytes
     * @param usage Buffer usage flags
     * @param sharingMode Buffer sharing mode (exclusive or concurrent)
     * @param properties Memory property flags (e.g., HOST_VISIBLE, DEVICE_LOCAL)
     * @param buffer Output parameter for the created buffer handle
     * @param bufferMemory Output parameter for the memory handle (legacy format)
     * @param bufferOffset Output parameter for the memory offset (legacy format)
     */
    static void createBufferLegacy(VkDevice device,
                                   VkPhysicalDevice physicalDevice,
                                   VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   VkSharingMode sharingMode,
                                   VkMemoryPropertyFlags properties,
                                   VkBuffer& buffer,
                                   VkDeviceMemory& bufferMemory,
                                   VkDeviceSize& bufferOffset);

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
     * Maps memory for host access using the VkDeviceAllocator
     *
     * @param allocation Device allocation to map
     * @param offset Offset into the allocation (default: 0)
     * @param size Size to map (can be VK_WHOLE_SIZE)
     * @return Pointer to mapped memory
     */
    static void* mapBufferMemory(VkDeviceAllocation& allocation,
                                 VkDeviceSize offset = 0,
                                 VkDeviceSize size = VK_WHOLE_SIZE);

    /**
     * Unmaps previously mapped memory using the VkDeviceAllocator
     *
     * @param allocation Device allocation to unmap
     */
    static void unmapBufferMemory(VkDeviceAllocation& allocation);

    /**
     * Destroys a buffer and frees its memory using the VkDeviceAllocator
     *
     * @param device Vulkan logical device
     * @param buffer Buffer handle to destroy
     * @param allocation Device allocation to free
     */
    static void destroyBuffer(VkDevice device,
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
