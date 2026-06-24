#ifndef VULKANMEMORYMANAGER_H
#define VULKANMEMORYMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "aura/aura.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"

namespace aura3d {
namespace vk {

struct AllocatedBuffer {
    VkBuffer       buffer      = VK_NULL_HANDLE;
    VmaAllocation  allocation  = VK_NULL_HANDLE;
    void*          mappedData  = nullptr;
    VkDeviceSize   offset      = 0;
};

struct AllocatedImage {
    VkImage       image      = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

class VulkanMemoryManager {
public:
    struct Config {
        bool   bufferDeviceAddress          = true;
        bool   preferDeviceMemory           = true;
        bool   persistentlyMapUploadBuffers = true;
        u32    vulkanApiVersion             = kVulkanApiVersion;
        size_t preferredLargeHeapBlockSize  = 128u * 1024u * 1024u;
    };

    VulkanMemoryManager() = default;
    ~VulkanMemoryManager();

    VulkanMemoryManager(const VulkanMemoryManager&) = delete;
    VulkanMemoryManager& operator=(const VulkanMemoryManager&) = delete;

    [[nodiscard]] static Config loadConfig(AuraSettings* settings);

    void initialize(VkInstance instance,
                    VkPhysicalDevice physicalDevice,
                    VkDevice device,
                    const Config& config);

    void shutdown();

    [[nodiscard]] bool isInitialized() const noexcept { return _allocator != VK_NULL_HANDLE; }
    [[nodiscard]] VmaAllocator getAllocator() const noexcept { return _allocator; }

    [[nodiscard]] AllocatedBuffer createBuffer(VkDeviceSize size,
                                               VkBufferUsageFlags usage,
                                               VkSharingMode sharingMode,
                                               VmaMemoryUsage memoryUsage,
                                               VmaAllocationCreateFlags extraFlags = 0);

    [[nodiscard]] AllocatedBuffer createUploadBuffer(VkDeviceSize size, VkSharingMode sharingMode);
    [[nodiscard]] AllocatedBuffer createDeviceLocalBuffer(VkDeviceSize size,
                                                          VkBufferUsageFlags usage,
                                                          VkSharingMode sharingMode);

    void destroyBuffer(AllocatedBuffer& buffer);

    [[nodiscard]] AllocatedImage createImage(const VkImageCreateInfo& imageInfo, VmaMemoryUsage memoryUsage);
    void destroyImage(AllocatedImage& image);

    void* map(AllocatedBuffer& buffer);
    void unmap(AllocatedBuffer& buffer);

private:
    VmaAllocator _allocator = VK_NULL_HANDLE;
    Config       _config{};
};

} // namespace vk
} // namespace aura3d

#endif // VULKANMEMORYMANAGER_H
