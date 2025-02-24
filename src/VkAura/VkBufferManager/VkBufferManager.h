#ifndef VKBUFFERMANAGER_H
#define VKBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>

namespace aura3d {
    class VkBufferManager
    {
    public:
        VkBufferManager();
        ~VkBufferManager();

        static void createBuffer(VkDevice device,
                                 VkPhysicalDevice physicalDevice,
                                 VkDeviceSize size,
                                 VkBufferUsageFlags usage,
                                 VkSharingMode sharingMode,
                                 VkMemoryPropertyFlags properties,
                                 VkBuffer& buffer,
                                 VkDeviceMemory& bufferMemory);

        static void bufferCopy(VkDevice device,
                               VkCommandPool commandPool,
                               VkQueue queue,
                               VkBuffer srcBuffer,
                               VkBuffer dstBuffer,
                               VkDeviceSize size);

        static void* mapBufferMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize size);

        static void unmapBufferMemory(VkDevice device, VkDeviceMemory memory);

        static void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);

        static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
    };


#endif // VKBUFFERMANAGER_H

}
