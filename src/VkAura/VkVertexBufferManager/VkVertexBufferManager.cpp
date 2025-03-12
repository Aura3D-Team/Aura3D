#include "VkVertexBufferManager.h"

#include <cstring>
#include "VkAura/VkBufferManager/VkBufferManager.h"

namespace aura3d {

VkVertexBufferManager::VkVertexBufferManager(VkDevice* vkDevice) :
    _vkDevice(vkDevice)
{
    // Empty
}

VkVertexBufferManager::~VkVertexBufferManager()
{
    cleanup();
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

    // Position (vec2)
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;

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

void VkVertexBufferManager::createVertexBuffer(VkDevice device,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               std::vector<Vertex2d> vertices2d)
{
    VkDeviceSize bufferSize = sizeof(vertices2d[0]) * vertices2d.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    // Create a staging buffer (CPU-accessible)
    VkBufferManager::createBuffer(device, physicalDevice, bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  sharingMode,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  stagingBuffer, stagingBufferMemory);

    void* data = VkBufferManager::mapBufferMemory(device, stagingBufferMemory, bufferSize);
    std::memcpy(data, vertices2d.data(), (size_t)bufferSize);
    VkBufferManager::unmapBufferMemory(device, stagingBufferMemory);

    // Create a vertex buffer (GPU-only)
    VkBufferManager::createBuffer(device, physicalDevice, bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  sharingMode,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  _vertexBuffer, _vertexBufferMemory);

    VkBufferManager::bufferCopy(device, commandPool, graphicsQueue, stagingBuffer, _vertexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void VkVertexBufferManager::createVertexBuffer(VkDevice device,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               std::vector<Vertex3d> vertices3d)
{
    VkDeviceSize bufferSize = sizeof(vertices3d[0]) * vertices3d.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    // Create a staging buffer (CPU-accessible)
    VkBufferManager::createBuffer(device, physicalDevice, bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  sharingMode,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  stagingBuffer, stagingBufferMemory);

    void* data = VkBufferManager::mapBufferMemory(device, stagingBufferMemory, bufferSize);
    std::memcpy(data, vertices3d.data(), (size_t)bufferSize);
    VkBufferManager::unmapBufferMemory(device, stagingBufferMemory);

    // Create a vertex buffer (GPU-only)
    VkBufferManager::createBuffer(device, physicalDevice, bufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  sharingMode,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  _vertexBuffer, _vertexBufferMemory);

    VkBufferManager::bufferCopy(device, commandPool, graphicsQueue, stagingBuffer, _vertexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void VkVertexBufferManager::cleanup()
{
    if (_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(*_vkDevice, _vertexBuffer, nullptr);
    }
    if (_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(*_vkDevice, _vertexBufferMemory, nullptr);
    }
}

}
