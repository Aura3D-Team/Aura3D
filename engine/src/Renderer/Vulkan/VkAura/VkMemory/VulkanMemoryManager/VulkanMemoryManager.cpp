#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <charconv>
#include <format>
#include <string_view>

#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

namespace {

[[nodiscard]] constexpr u32 parseVulkanApiVersion(std::string_view version) noexcept
{
    const auto dot = version.find('.');
    if (dot == std::string_view::npos) {
        return kVulkanApiVersion;
    }

    int major = 1;
    int minor = 4;
    std::from_chars(version.data(), version.data() + dot, major);
    std::from_chars(version.data() + dot + 1, version.data() + version.size(), minor);
    return VK_MAKE_API_VERSION(0, major, minor, 0);
}

[[nodiscard]] constexpr VmaAllocationCreateFlags uploadAllocationFlags(bool persistentlyMapped) noexcept
{
    VmaAllocationCreateFlags flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if (persistentlyMapped) {
        flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    return flags;
}

} // namespace

VulkanMemoryManager::Config VulkanMemoryManager::loadConfig(const AuraSettings* settings)
{
    Config cfg;

    if (!settings) {
        return cfg;
    }

    ink::EnhancedJson* json = settings->getSettings();
    cfg.bufferDeviceAddress = json->getPath<bool>("memory/vma/buffer_device_address", true);
    cfg.preferDeviceMemory = json->getPath<bool>("memory/vma/prefer_device_memory", true);
    cfg.persistentlyMapUploadBuffers = json->getPath<bool>("memory/vma/persistently_map_upload_buffers", true);

    const u32 blockMb = json->getPath<u32>("memory/vma/preferred_large_heap_block_size_mb", 128);
    cfg.preferredLargeHeapBlockSize = static_cast<size_t>(blockMb) * 1024u * 1024u;

    const std::string apiVersion = json->getPath<std::string>("memory/vma/vulkan_api_version", "1.4");
    cfg.vulkanApiVersion = parseVulkanApiVersion(apiVersion);

    return cfg;
}

void VulkanMemoryManager::initialize(VkInstance instance,
                                     VkPhysicalDevice physicalDevice,
                                     VkDevice device,
                                     const Config& config)
{
    if (_allocator != VK_NULL_HANDLE) {
        return;
    }

    _config = config;

    VmaAllocatorCreateInfo allocatorInfo{
        .flags = config.bufferDeviceAddress ? VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT : 0u,
        .physicalDevice = physicalDevice,
        .device = device,
        .preferredLargeHeapBlockSize = config.preferredLargeHeapBlockSize,
        .instance = instance,
        .vulkanApiVersion = config.vulkanApiVersion,
    };

    VK_RESULT_CHECK(vmaCreateAllocator(&allocatorInfo, &_allocator));
    INK_INFO << std::format(
        "VMA allocator initialized (Vulkan {}.{})",
        VK_VERSION_MAJOR(config.vulkanApiVersion),
        VK_VERSION_MINOR(config.vulkanApiVersion));
}

void VulkanMemoryManager::shutdown()
{
    if (_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(_allocator);
        _allocator = VK_NULL_HANDLE;
    }
}

AllocatedBuffer VulkanMemoryManager::createBuffer(VkDeviceSize size,
                                                  VkBufferUsageFlags usage,
                                                  VkSharingMode sharingMode,
                                                  VmaMemoryUsage memoryUsage,
                                                  VmaAllocationCreateFlags extraFlags)
{
    AllocatedBuffer result;

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = sharingMode,
    };

    const VmaAllocationCreateInfo allocInfo{
        .flags = extraFlags,
        .usage = memoryUsage,
    };

    VmaAllocationInfo allocationInfo{};
    VK_RESULT_CHECK(vmaCreateBuffer(_allocator,
                                    &bufferInfo,
                                    &allocInfo,
                                    &result.buffer,
                                    &result.allocation,
                                    &allocationInfo));

    result.mappedData = allocationInfo.pMappedData;
    result.offset = allocationInfo.offset;
    return result;
}

AllocatedBuffer VulkanMemoryManager::createUploadBuffer(VkDeviceSize size, VkSharingMode sharingMode)
{
    return createBuffer(size,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        sharingMode,
                        VMA_MEMORY_USAGE_AUTO,
                        uploadAllocationFlags(_config.persistentlyMapUploadBuffers));
}

AllocatedBuffer VulkanMemoryManager::createDeviceLocalBuffer(VkDeviceSize size,
                                                             VkBufferUsageFlags usage,
                                                             VkSharingMode sharingMode)
{
    const VmaMemoryUsage memoryUsage = _config.preferDeviceMemory
        ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        : VMA_MEMORY_USAGE_AUTO;

    return createBuffer(size,
                        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        sharingMode,
                        memoryUsage,
                        0);
}

void VulkanMemoryManager::destroyBuffer(AllocatedBuffer& buffer)
{
    if (buffer.allocation != VK_NULL_HANDLE || buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

AllocatedImage VulkanMemoryManager::createImage(const VkImageCreateInfo& imageInfo, VmaMemoryUsage memoryUsage)
{
    AllocatedImage result;

    const VmaAllocationCreateInfo allocInfo{
        .usage = memoryUsage,
    };

    VK_RESULT_CHECK(vmaCreateImage(_allocator,
                                   &imageInfo,
                                   &allocInfo,
                                   &result.image,
                                   &result.allocation,
                                   nullptr));

    return result;
}

void VulkanMemoryManager::destroyImage(AllocatedImage& image)
{
    if (image.allocation != VK_NULL_HANDLE || image.image != VK_NULL_HANDLE) {
        vmaDestroyImage(_allocator, image.image, image.allocation);
    }
    image = {};
}

void* VulkanMemoryManager::map(AllocatedBuffer& buffer)
{
    void* data = nullptr;
    VK_RESULT_CHECK(vmaMapMemory(_allocator, buffer.allocation, &data));
    buffer.mappedData = data;
    return data;
}

void VulkanMemoryManager::unmap(AllocatedBuffer& buffer)
{
    vmaUnmapMemory(_allocator, buffer.allocation);
    buffer.mappedData = nullptr;
}

VulkanMemoryManager::~VulkanMemoryManager()
{
    shutdown();
}

} // namespace vk
} // namespace aura3d
