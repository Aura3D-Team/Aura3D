#ifndef VKVERTEXBUFFERMANAGER_H
#define VKVERTEXBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace aura3d {

struct Vertex3d
{
    glm::vec3 pos;
    glm::vec3 color;
};

struct Vertex2d
{
    glm::vec2 pos;
    glm::vec3 color;
};

class VkVertexBufferManager
{
public:
    VkVertexBufferManager(VkDevice* vkDevice);
    ~VkVertexBufferManager();

    void createVertexBuffer(VkDevice device,
                            VkPhysicalDevice physicalDevice,
                            VkCommandPool commandPool,
                            VkSharingMode sharingMode,
                            VkQueue graphicsQueue);

private:
    VkDevice* _vkDevice;
    // VkPhysicalDevice* _vkPhysicalDevice;
    // VkCommandPool* _commandPool;
    // VkQueue* _graphicsQueue;

    VkBuffer _vertexBuffer;
    VkDeviceMemory _vertexBufferMemory;
};

}

#endif // VKVERTEXBUFFERMANGER_H
