#include "aura/Renderer/Vulkan/VkAura/VkIndexBufferManager/VkIndexBufferManager.h"

#include <cstring>

#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

namespace {

VkMemoryPropertyFlags hostVisibleUploadFlags()
{
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

void uploadToPersistentBuffer(VkDeviceAllocator* allocator,
                              VkDeviceAllocation& allocation,
                              AuraBufferInfo& bufferInfo,
                              const void* src,
                              VkDeviceSize size)
{
    void* dst = nullptr;
    VK_RESULT_CHECK(allocator->mapMemory(allocation, 0, size, &dst));

    VkDeviceAllocation& storedAlloc = allocator->getAllocation(allocation.allocationId);
    bufferInfo.mappedPointer = storedAlloc.mappedData;
    storedAlloc.mappingState = AllocationMappingState::PERSISTENTLY_MAPPED;
    bufferInfo.persistent = true;

    if (src && size > 0) {
        std::memcpy(dst, src, static_cast<size_t>(size));
    }
}

} // namespace

VkIndexBufferManager::VkIndexBufferManager(VkHostAllocator* vkHostAllocator,
                                             VkDeviceAllocator* vkDeviceAllocator,
                                             VkDevice* vkDevice)
    : vkHostAllocator(vkHostAllocator), vkDeviceAllocator(vkDeviceAllocator), _vkDevice(vkDevice)
{
}

VkIndexBufferManager::~VkIndexBufferManager()
{
    cleanup();
    INK_INFO << "VkIndexBufferManager destroyed";
}

void VkIndexBufferManager::createIndexBuffer(const std::string& name,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               std::vector<u16>&& indices,
                                               bool persistentMapping)
{
    cleanup(name);

    if (indices.empty()) {
        INK_WARN << "createIndexBuffer: empty index data for " << name;
        return;
    }

    const VkDeviceSize bufferSize = sizeof(u16) * indices.size();

    IndexBufferInfo bufferInfo{};
    bufferInfo.indexCount = static_cast<u32>(indices.size());

    if (persistentMapping) {
        VkDeviceAllocation allocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                      sharingMode,
                                      hostVisibleUploadFlags(),
                                      bufferInfo.buffer,
                                      allocation);

        bufferInfo.allocationId = allocation.allocationId;
        uploadToPersistentBuffer(vkDeviceAllocator, allocation, bufferInfo, indices.data(), bufferSize);

        INK_DEBUG << "Created persistently mapped index buffer: " << name
                  << ", indices: " << bufferInfo.indexCount;
    } else {
        VkBuffer stagingBuffer;
        VkDeviceAllocation stagingAllocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      sharingMode,
                                      hostVisibleUploadFlags(),
                                      stagingBuffer,
                                      stagingAllocation);

        void* stagingData = nullptr;
        VK_RESULT_CHECK(vkDeviceAllocator->mapMemory(stagingAllocation, 0, bufferSize, &stagingData));
        std::memcpy(stagingData, indices.data(), static_cast<size_t>(bufferSize));
        vkDeviceAllocator->unmapMemory(stagingAllocation);

        VkDeviceAllocation indexAllocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                      sharingMode,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      bufferInfo.buffer,
                                      indexAllocation);

        bufferInfo.allocationId = indexAllocation.allocationId;
        bufferInfo.persistent = false;
        bufferInfo.mappedPointer = nullptr;

        VkBufferManager::bufferCopy(*_vkDevice,
                                    commandPool,
                                    graphicsQueue,
                                    stagingBuffer,
                                    bufferInfo.buffer,
                                    VK_NULL_HANDLE,
                                    bufferSize,
                                    0,
                                    0);

        vkDestroyBuffer(*_vkDevice, stagingBuffer, vkHostAllocator->getCallbacks());
        vkDeviceAllocator->freeMemory(stagingAllocation);

        INK_DEBUG << "Created device-local index buffer: " << name
                  << ", indices: " << bufferInfo.indexCount;
    }

    _indexBuffers[name] = bufferInfo;
}

void VkIndexBufferManager::updateIndexBuffer(const std::string& name, std::vector<u16>&& indices)
{
    auto it = _indexBuffers.find(name);
    if (it == _indexBuffers.end()) {
        INK_ERROR << "Failed to update buffer - buffer doesn't exist " << name;
        return;
    }

    IndexBufferInfo& bufferInfo = it->second;
    const VkDeviceSize bufferSize = sizeof(u16) * indices.size();

    VkDeviceAllocation& allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
    if (allocation.allocationId == 0) {
        INK_ERROR << "Failed to retrieve allocation for buffer: " << name;
        return;
    }

    if (bufferInfo.persistent && allocation.mappedData) {
        std::memcpy(allocation.mappedData, indices.data(), static_cast<size_t>(bufferSize));
    } else {
        void* data = nullptr;
        if (vkDeviceAllocator->mapMemory(allocation, 0, bufferSize, &data) == VK_SUCCESS) {
            std::memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
            vkDeviceAllocator->unmapMemory(allocation);
        }
    }

    bufferInfo.indexCount = static_cast<u32>(indices.size());
}

IndexBufferInfo VkIndexBufferManager::getIndexBuffer(const std::string& name)
{
    auto it = _indexBuffers.find(name);
    if (it != _indexBuffers.end()) {
        return it->second;
    }

    INK_ERROR << "Invalid index buffer name: " << name;
    throw AuraException("Invalid IndexBuffer Name");
}

void VkIndexBufferManager::cleanup(const std::string& name)
{
    auto it = _indexBuffers.find(name);
    if (it == _indexBuffers.end()) {
        return;
    }

    IndexBufferInfo& bufferInfo = it->second;

    if (bufferInfo.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(*_vkDevice, bufferInfo.buffer, vkHostAllocator->getCallbacks());
        bufferInfo.buffer = VK_NULL_HANDLE;
    }

    if (bufferInfo.allocationId != 0) {
        auto allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
        vkDeviceAllocator->freeMemory(allocation);
    }

    _indexBuffers.erase(it);

    INK_DEBUG << "Cleaned up index buffer: " << name;
}

void VkIndexBufferManager::cleanup()
{
    std::vector<std::string> bufferNames;
    for (const auto& pair : _indexBuffers) {
        bufferNames.push_back(pair.first);
    }

    for (const auto& name : bufferNames) {
        cleanup(name);
    }

    _indexBuffers.clear();

    INK_INFO << "Cleaned up all index buffers";
}

} // namespace vk
} // namespace aura3d
