#include "VkVertexBufferManager.h"

#include "VkAura/VkBufferManager/VkBufferManager.h"
#include "AuraException/AuraException.h"

#include <cstring>

namespace aura3d {

VkVertexBufferManager::VkVertexBufferManager(VkDevice* vkDevice) :
    _vkDevice(vkDevice)
{
    // Empty constructor
}

VkVertexBufferManager::~VkVertexBufferManager()
{
    cleanup();
}

void VkVertexBufferManager::createVertexBuffer(const std::string& name,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               VkBufferMemoryAllocator* allocator,
                                               const std::vector<Vertex2d>& vertices2d,
                                               bool persistentMapping)
{
    // First cleanup any existing buffer with the same name
    cleanup(name);

    VkDeviceSize bufferSize = sizeof(Vertex2d) * vertices2d.size();

    // Create a staging buffer (CPU-accessible)
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkDeviceSize stagingOffset = 0;

    VkBufferManager::createBuffer(*_vkDevice, physicalDevice, bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  sharingMode,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  stagingBuffer, stagingBufferMemory, stagingOffset, allocator);

    // Get mapped memory from allocator
    AllocationInfo allocInfo = allocator->getAllocationInfo(stagingBufferMemory, stagingOffset);
    void* data = allocInfo.mappedData;

    // Copy data directly to mapped memory
    std::memcpy(data, vertices2d.data(), (size_t)bufferSize);

    // Create a vertex buffer with appropriate memory properties
    VertexBufferInfo bufferInfo{};
    bufferInfo.buffer = VK_NULL_HANDLE;
    bufferInfo.memory = VK_NULL_HANDLE;
    bufferInfo.offset = 0;         // Initialize the offset
    bufferInfo.mappedMemory = nullptr;
    bufferInfo.vertexCount = 0;
    bufferInfo.is2d = true;
    bufferInfo.persistent = false;

    // Memory property flags - persistent mapping requires HOST_VISIBLE
    VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (persistentMapping) {
        memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    // Create the actual vertex buffer
    VkBufferManager::createBuffer(*_vkDevice, physicalDevice, bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  sharingMode,
                                  memFlags,
                                  bufferInfo.buffer, bufferInfo.memory, bufferInfo.offset, allocator);

    // If not using persistent mapping, copy from staging buffer
    if (!persistentMapping) {
        VkBufferManager::bufferCopy(*_vkDevice, commandPool, graphicsQueue,
                                    stagingBuffer, bufferInfo.buffer, bufferSize);
    }
    // If using persistent mapping, copy directly to mapped memory
    else {
        AllocationInfo allocInfo = allocator->getAllocationInfo(bufferInfo.memory, bufferInfo.offset);
        bufferInfo.mappedMemory = allocInfo.mappedData;

        // Copy data from CPU to mapped GPU memory
        std::memcpy(bufferInfo.mappedMemory, vertices2d.data(), (size_t)bufferSize);
    }

    // Store the buffer info in our map
    _vertexBuffers[name] = bufferInfo;

    // Clean up staging buffer
    vkDestroyBuffer(*_vkDevice, stagingBuffer, nullptr);
    // Let the allocator handle freeing the memory
    allocator->free(stagingBufferMemory, stagingOffset);
}

void VkVertexBufferManager::createVertexBuffer(const std::string& name,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               VkBufferMemoryAllocator* allocator,
                                               const std::vector<Vertex3d>& vertices3d,
                                               bool persistentMapping)
{
    // First cleanup any existing buffer with the same name
    cleanup(name);

    VkDeviceSize bufferSize = sizeof(Vertex3d) * vertices3d.size();

    // Create a staging buffer (CPU-accessible)
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkDeviceSize stagingOffset = 0;

    VkBufferManager::createBuffer(*_vkDevice, physicalDevice, bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  sharingMode,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  stagingBuffer, stagingBufferMemory, stagingOffset, allocator);

    // Get mapped memory from allocator
    AllocationInfo allocInfo = allocator->getAllocationInfo(stagingBufferMemory, stagingOffset);
    void* data = allocInfo.mappedData;

    // Copy data directly to mapped memory
    std::memcpy(data, vertices3d.data(), (size_t)bufferSize);

    // Create a vertex buffer with appropriate memory properties
    VertexBufferInfo bufferInfo{};
    bufferInfo.buffer = VK_NULL_HANDLE;
    bufferInfo.memory = VK_NULL_HANDLE;
    bufferInfo.offset = 0;         // Initialize the offset
    bufferInfo.mappedMemory = nullptr;
    bufferInfo.vertexCount = 0;
    bufferInfo.is2d = true;
    bufferInfo.persistent = false;

    // Memory property flags - persistent mapping requires HOST_VISIBLE
    VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (persistentMapping) {
        memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    // Create the actual vertex buffer
    VkBufferManager::createBuffer(*_vkDevice, physicalDevice, bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  sharingMode,
                                  memFlags,
                                  bufferInfo.buffer, bufferInfo.memory, bufferInfo.offset, allocator);

    // If not using persistent mapping, copy from staging buffer
    if (!persistentMapping) {
        VkBufferManager::bufferCopy(*_vkDevice, commandPool, graphicsQueue,
                                    stagingBuffer, bufferInfo.buffer, bufferSize);
    }
    // If using persistent mapping, copy directly to mapped memory
    else {
        AllocationInfo allocInfo = allocator->getAllocationInfo(bufferInfo.memory, bufferInfo.offset);
        bufferInfo.mappedMemory = allocInfo.mappedData;

        // Copy data from CPU to mapped GPU memory
        std::memcpy(bufferInfo.mappedMemory, vertices3d.data(), (size_t)bufferSize);
    }

    // Store the buffer info in our map
    _vertexBuffers[name] = bufferInfo;

    // Clean up staging buffer
    vkDestroyBuffer(*_vkDevice, stagingBuffer, nullptr);
    // Let the allocator handle freeing the memory
    allocator->free(stagingBufferMemory, stagingOffset);
}

void VkVertexBufferManager::updateVertexBuffer(const std::string& name, const std::vector<Vertex2d>& vertices2d)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end() || !it->second.is2d) {
        // Buffer doesn't exist or is not 2D
        return;
    }

    VertexBufferInfo& bufferInfo = it->second;
    VkDeviceSize bufferSize = sizeof(Vertex2d) * vertices2d.size();

    // Check if the buffer is persistently mapped
    if (bufferInfo.persistent && bufferInfo.mappedMemory) {
        // Direct update to the mapped memory
        std::memcpy(bufferInfo.mappedMemory, vertices2d.data(), (size_t)bufferSize);
    } else {
        // Need to map, update, and unmap
        void* data = VkBufferManager::mapBufferMemory(*_vkDevice, bufferInfo.memory, bufferSize);
        std::memcpy(data, vertices2d.data(), (size_t)bufferSize);
        VkBufferManager::unmapBufferMemory(*_vkDevice, bufferInfo.memory);
    }

    // Update the vertex count
    bufferInfo.vertexCount = vertices2d.size();
}

void VkVertexBufferManager::updateVertexBuffer(const std::string& name, const std::vector<Vertex3d>& vertices3d)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end() || it->second.is2d) {
        // Buffer doesn't exist or is not 3D
        return;
    }

    VertexBufferInfo& bufferInfo = it->second;
    VkDeviceSize bufferSize = sizeof(Vertex3d) * vertices3d.size();

    // Check if the buffer is persistently mapped
    if (bufferInfo.persistent && bufferInfo.mappedMemory) {
        // Direct update to the mapped memory
        std::memcpy(bufferInfo.mappedMemory, vertices3d.data(), (size_t)bufferSize);
    } else {
        // Need to map, update, and unmap
        void* data = VkBufferManager::mapBufferMemory(*_vkDevice, bufferInfo.memory, bufferSize);
        std::memcpy(data, vertices3d.data(), (size_t)bufferSize);
        VkBufferManager::unmapBufferMemory(*_vkDevice, bufferInfo.memory);
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

void VkVertexBufferManager::unmapMemory(const std::string& name)
{
    auto it = _vertexBuffers.find(name);
    if (it != _vertexBuffers.end() && it->second.persistent && it->second.mappedMemory) {
        vkUnmapMemory(*_vkDevice, it->second.memory);
        it->second.mappedMemory = nullptr;
    }
}

void VkVertexBufferManager::cleanup(const std::string& name)
{
    auto it = _vertexBuffers.find(name);
    if (it != _vertexBuffers.end()) {
        VertexBufferInfo& bufferInfo = it->second;

        // Unmap memory if persistently mapped
        if (bufferInfo.persistent && bufferInfo.mappedMemory) {
            vkUnmapMemory(*_vkDevice, bufferInfo.memory);
        }

        // Destroy the buffer and free memory
        if (bufferInfo.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(*_vkDevice, bufferInfo.buffer, nullptr);
        }

        if (bufferInfo.memory != VK_NULL_HANDLE) {
            vkFreeMemory(*_vkDevice, bufferInfo.memory, nullptr);
        }

        // Remove from the map
        _vertexBuffers.erase(it);
    }
}

void VkVertexBufferManager::cleanup()
{
    // Clean up all buffers
    for (auto& pair : _vertexBuffers) {
        VertexBufferInfo& bufferInfo = pair.second;

        // Unmap memory if persistently mapped
        if (bufferInfo.persistent && bufferInfo.mappedMemory) {
            vkUnmapMemory(*_vkDevice, bufferInfo.memory);
        }

        // Destroy the buffer and free memory
        if (bufferInfo.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(*_vkDevice, bufferInfo.buffer, nullptr);
        }

        if (bufferInfo.memory != VK_NULL_HANDLE) {
            vkFreeMemory(*_vkDevice, bufferInfo.memory, nullptr);
        }
    }

    // Clear the map
    _vertexBuffers.clear();
}

VkVertexInputBindingDescription VkVertexBufferManager::getBindingDescription(bool is2d)
{
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    if (is2d)
        bindingDescription.stride = sizeof(Vertex2d);
    else
        bindingDescription.stride = sizeof(Vertex3d);
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

    // Texture Coordinate (vec2)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;

    // Color (vec4)
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;

    if (is2d)
    {
        attributeDescriptions[0].offset = offsetof(Vertex2d, pos);
        attributeDescriptions[1].offset = offsetof(Vertex2d, texCoord);
        attributeDescriptions[2].offset = offsetof(Vertex2d, color);
    }
    else
    {
        attributeDescriptions[0].offset = offsetof(Vertex3d, pos);
        attributeDescriptions[1].offset = offsetof(Vertex3d, texCoord);
        attributeDescriptions[2].offset = offsetof(Vertex3d, color);
    }

    return attributeDescriptions;
}

} // namespace aura3d
