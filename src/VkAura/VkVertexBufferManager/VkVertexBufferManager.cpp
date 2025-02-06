#include "VkVertexBufferManager.h"

#include "VkAura/VkBufferManager/VkBufferManager.h"

VkVertexBufferManager::VkVertexBufferManager(VkDevice* vkDevice) :
    _vkDevice(vkDevice)
{
    // Empty
}

VkVertexBufferManager::~VkVertexBufferManager()
{
    if (_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(*_vkDevice, _vertexBuffer, nullptr);
    }
    if (_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(*_vkDevice, _vertexBufferMemory, nullptr);
    }
}

// void VkVertexBufferManager::createVertexBuffer(VkDevice device,
//                                                VkPhysicalDevice physicalDevice,
//                                                VkCommandPool commandPool,
//                                                VkSharingMode sharingMode,
//                                                VkQueue graphicsQueue)
// {
//     VkDeviceSize bufferSize /*= sizeof(vertices[0]) * vertices.size()*/;

//     VkBuffer stagingBuffer;
//     VkDeviceMemory stagingBufferMemory;

//     // Create a staging buffer (CPU-accessible)
//     VkBufferManager::createBuffer(device, physicalDevice, bufferSize,
//                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//                                   sharingMode,
//                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//                                   stagingBuffer, stagingBufferMemory);

//     void* data = VkBufferManager::mapBufferMemory(device, stagingBufferMemory, bufferSize);
//     memcpy(data, vertices.data(), (size_t)bufferSize);
//     vkUnmapMemory(device, stagingBufferMemory);

//     // Create a vertex buffer (GPU-only)
//     VkBufferManager::createBuffer(device, physicalDevice, bufferSize,
//                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
//                                   sharingMode,
//                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
//                                   _vertexBuffer, _vertexBufferMemory);

//     VkBufferManager::bufferCopy(device, commandPool, graphicsQueue, stagingBuffer, _vertexBuffer, bufferSize);

//     vkDestroyBuffer(device, stagingBuffer, nullptr);
//     vkFreeMemory(device, stagingBufferMemory, nullptr);
// }
