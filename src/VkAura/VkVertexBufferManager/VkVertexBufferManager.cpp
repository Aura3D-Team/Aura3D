#include "VkVertexBufferManager.h"
#include "VkAura/VkBufferManager/VkBufferManager.h"
#include "AuraException/AuraException.h"
#include <ink/ink.hpp>
#include <cstring>

namespace aura3d {

VkVertexBufferManager::VkVertexBufferManager(VkHostAllocator* vkHostAllocator,
                                             VkDeviceAllocator* vkDeviceAllocator,
                                             VkDevice* vkDevice) :
    vkHostAllocator(vkHostAllocator), vkDeviceAllocator(vkDeviceAllocator), _vkDevice(vkDevice)
{
    INK_INFO << "VkVertexBufferManager created";
}

VkVertexBufferManager::~VkVertexBufferManager()
{
    cleanup();
    INK_INFO << "VkVertexBufferManager destroyed";
}

void VkVertexBufferManager::createVertexBuffer(const std::string& name,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               const std::vector<Vertex2d>& vertices2d,
                                               bool persistentMapping)
{
    // First cleanup any existing buffer with the same name
    cleanup(name);

    VkDeviceSize bufferSize = sizeof(Vertex2d) * vertices2d.size();

    // Create a vertex buffer info structure
    VertexBufferInfo bufferInfo{};
    bufferInfo.buffer = VK_NULL_HANDLE;
    bufferInfo.allocationId = 0;
    bufferInfo.vertexCount = vertices2d.size();
    bufferInfo.is2d = true;
    bufferInfo.persistent = persistentMapping;

    if (persistentMapping) {
        // For persistent mapping, create directly in host-visible memory
        VkDeviceAllocation allocation;

        // Create the vertex buffer
        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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
        std::memcpy(data, vertices2d.data(), static_cast<size_t>(bufferSize));

        // For persistent mapping, we need to update the allocation state
        VkDeviceAllocation& storedAlloc = vkDeviceAllocator->getAllocation(allocation.allocationId);
        if (storedAlloc.allocationId == allocation.allocationId) {
            storedAlloc.mappingState = AllocationMappingState::PERSISTENTLY_MAPPED;
        }

        INK_DEBUG << "Created persistently mapped vertex buffer: " << name
                   << ", vertices: " << bufferInfo.vertexCount;
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
        std::memcpy(data, vertices2d.data(), static_cast<size_t>(bufferSize));
        vkDeviceAllocator->unmapMemory(stagingAllocation);

        // 3. Create device-local vertex buffer
        VkDeviceAllocation vertexAllocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      sharingMode,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      bufferInfo.buffer,
                                      vertexAllocation);

        // Store allocation ID
        bufferInfo.allocationId = vertexAllocation.allocationId;

        // 4. Copy from staging buffer to vertex buffer
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
                   << ", vertices: " << bufferInfo.vertexCount;
    }

    // Store the buffer info in our map
    _vertexBuffers[name] = bufferInfo;
}

void VkVertexBufferManager::createVertexBuffer(const std::string& name,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               const std::vector<Vertex3d>& vertices3d,
                                               bool persistentMapping)
{
    // First cleanup any existing buffer with the same name
    cleanup(name);

    VkDeviceSize bufferSize = sizeof(Vertex3d) * vertices3d.size();

    // Create a vertex buffer info structure
    VertexBufferInfo bufferInfo{};
    bufferInfo.buffer = VK_NULL_HANDLE;
    bufferInfo.allocationId = 0;
    bufferInfo.vertexCount = vertices3d.size();
    bufferInfo.is2d = false;
    bufferInfo.persistent = persistentMapping;

    if (persistentMapping) {
        // For persistent mapping, create directly in host-visible memory
        VkDeviceAllocation allocation;

        // Create the vertex buffer
        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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
        std::memcpy(data, vertices3d.data(), static_cast<size_t>(bufferSize));

        // For persistent mapping, we need to update the allocation state
        VkDeviceAllocation& storedAlloc = vkDeviceAllocator->getAllocation(allocation.allocationId);
        if (storedAlloc.allocationId == allocation.allocationId) {
            storedAlloc.mappingState = AllocationMappingState::PERSISTENTLY_MAPPED;
        }

        INK_DEBUG << "Created persistently mapped 3D vertex buffer: " << name
                   << ", vertices: " << bufferInfo.vertexCount;
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
        std::memcpy(data, vertices3d.data(), static_cast<size_t>(bufferSize));
        vkDeviceAllocator->unmapMemory(stagingAllocation);

        // 3. Create device-local vertex buffer
        VkDeviceAllocation vertexAllocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *_vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      sharingMode,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      bufferInfo.buffer,
                                      vertexAllocation);

        // Store allocation ID
        bufferInfo.allocationId = vertexAllocation.allocationId;

        // 4. Copy from staging buffer to vertex buffer
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

        INK_DEBUG << "Created device-local 3D vertex buffer: " << name
                   << ", vertices: " << bufferInfo.vertexCount;
    }

    // Store the buffer info in our map
    _vertexBuffers[name] = bufferInfo;
}

void VkVertexBufferManager::updateVertexBuffer(const std::string& name, const std::vector<Vertex2d>& vertices2d)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end() || !it->second.is2d) {
        INK_ERROR << "Failed to update buffer - buffer doesn't exist or is not 2D: " << name;
        return;
    }

    VertexBufferInfo& bufferInfo = it->second;
    VkDeviceSize bufferSize = sizeof(Vertex2d) * vertices2d.size();

    // Retrieve allocation from allocator
    VkDeviceAllocation& allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
    if (allocation.allocationId == 0) {
        INK_ERROR << "Failed to retrieve allocation for buffer: " << name;
        return;
    }

    // Check if the buffer is persistently mapped
    if (bufferInfo.persistent && allocation.mappedData) {
        // Direct update to the mapped memory
        std::memcpy(allocation.mappedData, vertices2d.data(), static_cast<size_t>(bufferSize));
    } else {
        // Map, update, and unmap
        void* data = nullptr;
        if (vkDeviceAllocator->mapMemory(allocation, 0, bufferSize, &data) == VK_SUCCESS) {
            // Copy data to the mapped memory
            std::memcpy(data, vertices2d.data(), static_cast<size_t>(bufferSize));

            // Unmap the memory
            vkDeviceAllocator->unmapMemory(allocation);
        }
    }

    // Update the vertex count
    bufferInfo.vertexCount = vertices2d.size();
}

void VkVertexBufferManager::updateVertexBuffer(const std::string& name, const std::vector<Vertex3d>& vertices3d)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end() || it->second.is2d) {
        INK_ERROR << "Failed to update buffer - buffer doesn't exist or is not 3D: " << name;
        return;
    }

    VertexBufferInfo& bufferInfo = it->second;
    VkDeviceSize bufferSize = sizeof(Vertex3d) * vertices3d.size();

    // Retrieve allocation from allocator
    VkDeviceAllocation& allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
    if (allocation.allocationId == 0) {
        INK_ERROR << "Failed to retrieve allocation for buffer: " << name;
        return;
    }

    // Check if the buffer is persistently mapped
    if (bufferInfo.persistent && allocation.mappedData) {
        // Direct update to the mapped memory
        std::memcpy(allocation.mappedData, vertices3d.data(), static_cast<size_t>(bufferSize));
    } else {
        // Map, update, and unmap
        void* data = nullptr;
        if (vkDeviceAllocator->mapMemory(allocation, 0, bufferSize, &data) == VK_SUCCESS) {
            // Copy data to the mapped memory
            std::memcpy(data, vertices3d.data(), static_cast<size_t>(bufferSize));

            // Unmap the memory
            vkDeviceAllocator->unmapMemory(allocation);
        }
    }

    // Update the vertex count
    bufferInfo.vertexCount = vertices3d.size();
}

VertexBufferInfo VkVertexBufferManager::getVertexBuffer(const std::string& name)
{
    auto it = _vertexBuffers.find(name);
    if (it != _vertexBuffers.end()) {
        return it->second;
    }

    INK_ERROR << "Invalid vertex buffer name: " << name;
    throw AuraException("Invalid VertexBuffer Name");
}

size_t VkVertexBufferManager::getVertexCount(const std::string& name)
{
    auto it = _vertexBuffers.find(name);
    if (it != _vertexBuffers.end()) {
        return it->second.vertexCount;
    }
    return 0;
}

void VkVertexBufferManager::cleanup(const std::string& name)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end()) {
        return;
    }

    VertexBufferInfo& bufferInfo = it->second;

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
    _vertexBuffers.erase(it);

    INK_DEBUG << "Cleaned up vertex buffer: " << name;
}

void VkVertexBufferManager::cleanup()
{
    // Make a copy of buffer names to avoid iterator invalidation
    std::vector<std::string> bufferNames;
    for (const auto& pair : _vertexBuffers) {
        bufferNames.push_back(pair.first);
    }

    // Clean up each buffer
    for (const auto& name : bufferNames) {
        cleanup(name);
    }

    // Clear the map (should already be empty)
    _vertexBuffers.clear();

    INK_INFO << "Cleaned up all vertex buffers";
}

VkVertexInputBindingDescription VkVertexBufferManager::getBindingDescription(bool is2d)
{
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = is2d ? sizeof(Vertex2d) : sizeof(Vertex3d);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    return bindingDescription;
}

AttributeDescriptionArray<VkVertexInputAttributeDescription> VkVertexBufferManager::getAttributeDescriptions(bool is2d)
{
    AttributeDescriptionArray<VkVertexInputAttributeDescription> attributeDescriptions = {};

    // Position (vec2 for 2D, vec3 for 3D)
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = is2d ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;  // Will be updated below

    // Texture Coordinate (vec2)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = 0;  // Will be updated below

    // Color (vec4)
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[2].offset = 0;  // Will be updated below

    if (is2d) {
        attributeDescriptions[0].offset = offsetof(Vertex2d, pos);
        attributeDescriptions[1].offset = offsetof(Vertex2d, texCoord);
        attributeDescriptions[2].offset = offsetof(Vertex2d, color);
    } else {
        attributeDescriptions[0].offset = offsetof(Vertex3d, pos);
        attributeDescriptions[1].offset = offsetof(Vertex3d, texCoord);
        attributeDescriptions[2].offset = offsetof(Vertex3d, color);
    }

    return attributeDescriptions;
}

} // namespace aura3d
