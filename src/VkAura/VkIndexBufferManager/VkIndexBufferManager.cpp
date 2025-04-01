#include "VkIndexBufferManager.h"

#include "VkAura/VkBufferManager/VkBufferManager.h"
#include "AuraException/AuraException.h"
#include <ink/ink.hpp>
#include <cstring>


namespace aura3d {

VkIndexBufferManager::VkIndexBufferManager(VkHostAllocator* vkHostAllocator,
                                             VkDeviceAllocator* vkDeviceAllocator,
                                             VkDevice* vkDevice) :
    vkHostAllocator(vkHostAllocator), vkDeviceAllocator(vkDeviceAllocator), _vkDevice(vkDevice)
{
    INK_INFO << "VkIndexBufferManager created";
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
                                               const std::vector<u16>& indices,
                                               bool persistentMapping)
{
    // First cleanup any existing buffer with the same name
    cleanup(name);

    VkDeviceSize bufferSize = sizeof(u16) * indices.size();

    // Create a vertex buffer info structure
    IndexBufferInfo bufferInfo{};
    bufferInfo.buffer = VK_NULL_HANDLE;
    bufferInfo.allocationId = 0;
    bufferInfo.persistent = persistentMapping;
    bufferInfo.indexCount = static_cast<u32>(indices.size());

    if (persistentMapping) {
        // For persistent mapping, create directly in host-visible memory
        VkDeviceAllocation allocation;

        // Create the vertex buffer
        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                      sharingMode,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      bufferInfo.buffer,
                                      allocation);

        // Store allocation ID
        bufferInfo.allocationId = allocation.allocationId;

        // Map the memory
        void* data = nullptr;
        VK_RESULT_CHECK(vkDeviceAllocator->mapMemory(allocation, 0, bufferSize, &data));

        // Copy data directly to mapped memory
        std::memcpy(data, indices.data(), static_cast<size_t>(bufferSize));

        // For persistent mapping, we need to update the allocation state
        VkDeviceAllocation& storedAlloc = vkDeviceAllocator->getAllocation(allocation.allocationId);
        if (storedAlloc.allocationId == allocation.allocationId) {
            storedAlloc.mappingState = AllocationMappingState::PERSISTENTLY_MAPPED;
        }

        INK_DEBUG << "Created persistently mapped vertex buffer: " << name
                  << ", indices: " << bufferInfo.indexCount;
    } else {
        // For non-persistent mapping, use the staging buffer approach

        // 1. Create staging buffer (CPU-accessible)
        VkBuffer stagingBuffer;
        VkDeviceAllocation stagingAllocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      sharingMode,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      stagingBuffer,
                                      stagingAllocation);

        // 2. Copy data to staging buffer
        void* data = nullptr;
        VK_RESULT_CHECK(vkDeviceAllocator->mapMemory(stagingAllocation, 0, bufferSize, &data));
        std::memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
        vkDeviceAllocator->unmapMemory(stagingAllocation);

        // 3. Create device-local index buffer
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

        // Store allocation ID
        bufferInfo.allocationId = indexAllocation.allocationId;

        // 4. Copy from staging buffer to index buffer
        VkBufferManager::bufferCopy(*_vkDevice,
                                    commandPool,
                                    graphicsQueue,
                                    stagingBuffer,
                                    bufferInfo.buffer,
                                    VK_NULL_HANDLE, // No fence
                                    bufferSize,
                                    0, // Source offset
                                    0); // Destination offset

        // 5. Clean up staging buffer and memory
        vkDestroyBuffer(*_vkDevice, stagingBuffer, vkHostAllocator->getCallbacks());
        vkDeviceAllocator->freeMemory(stagingAllocation);

        INK_DEBUG << "Created device-local vertex buffer: " << name
                  << ", indices: " << bufferInfo.indexCount;
    }

    // Store the buffer info in our map
    _indexBuffers[name] = bufferInfo;
}

void VkIndexBufferManager::updateIndexBuffer(const std::string& name, const std::vector<u16>& indices)
{
    auto it = _indexBuffers.find(name);
    if (it == _indexBuffers.end()) {
        INK_ERROR << "Failed to update buffer - buffer doesn't exist " << name;
        return;
    }

    IndexBufferInfo& bufferInfo = it->second;
    VkDeviceSize bufferSize = sizeof(u16) * indices.size();

    // Retrieve allocation from allocator
    VkDeviceAllocation& allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
    if (allocation.allocationId == 0) {
        INK_ERROR << "Failed to retrieve allocation for buffer: " << name;
        return;
    }

    // Check if the buffer is persistently mapped
    if (bufferInfo.persistent && allocation.mappedData) {
        // Direct update to the mapped memory
        std::memcpy(allocation.mappedData, indices.data(), static_cast<size_t>(bufferSize));
    } else {
        // Map, update, and unmap
        void* data = nullptr;
        if (vkDeviceAllocator->mapMemory(allocation, 0, bufferSize, &data) == VK_SUCCESS) {
            // Copy data to the mapped memory
            std::memcpy(data, indices.data(), static_cast<size_t>(bufferSize));

            // Unmap the memory
            vkDeviceAllocator->unmapMemory(allocation);
        }
    }
}

IndexBufferInfo VkIndexBufferManager::getIndexBuffer(const std::string& name)
{
    auto it = _indexBuffers.find(name);
    if (it != _indexBuffers.end()) {
        return it->second;
    }

    INK_ERROR << "Invalid vertex buffer name: " << name;
    throw AuraException("Invalid VertexBuffer Name");
}

void VkIndexBufferManager::cleanup(const std::string& name)
{
    auto it = _indexBuffers.find(name);
    if (it == _indexBuffers.end()) {
        return;
    }

    IndexBufferInfo& bufferInfo = it->second;

    // Destroy the buffer
    if (bufferInfo.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(*_vkDevice, bufferInfo.buffer, vkHostAllocator->getCallbacks());
        bufferInfo.buffer = VK_NULL_HANDLE;
    }

    // Free the memory through the allocator - this will handle unmapping if needed
    if (bufferInfo.allocationId != 0) {
        auto allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
        vkDeviceAllocator->freeMemory(allocation);
    }

    // Remove from our map
    _indexBuffers.erase(it);

    INK_DEBUG << "Cleaned up vertex buffer: " << name;
}

void VkIndexBufferManager::cleanup()
{
    // Make a copy of buffer names to avoid iterator invalidation
    std::vector<std::string> bufferNames;
    for (const auto& pair : _indexBuffers) {
        bufferNames.push_back(pair.first);
    }

    // Clean up each buffer
    for (const auto& name : bufferNames) {
        cleanup(name);
    }

    // Clear the map (should already be empty)
    _indexBuffers.clear();

    INK_INFO << "Cleaned up all vertex buffers";
}

} // namespace aura3d
