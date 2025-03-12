#ifndef VKVERTEXBUFFERMANAGER_H
#define VKVERTEXBUFFERMANAGER_H

#include <vector>
#pragma once

#include <aura.hpp>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace aura3d {

struct Vertex3d
{
    glm::vec3 pos; // (x, y, z)
    glm::vec2 texCoord; // (u, v)
    glm::vec4 color; // (r, g, b, a)
};

struct Vertex2d {
    glm::vec2 pos;  // (x, y)
    glm::vec2 texCoord;  // (u, v)
    glm::vec4 color;     // (r, g, b, a)
};

class VkVertexBufferManager
{
public:
    VkVertexBufferManager(VkDevice* vkDevice);
    ~VkVertexBufferManager();

    static VkVertexInputBindingDescription getBindingDescription(bool is2d);
    static AttributeDescriptionArray<VkVertexInputAttributeDescription> getAttributeDescriptions(bool is2d);

    void createVertexBuffer(VkDevice device,
                            VkPhysicalDevice physicalDevice,
                            VkCommandPool commandPool,
                            VkSharingMode sharingMode,
                            VkQueue graphicsQueue,
                            std::vector<Vertex2d> vertices2d);

    void createVertexBuffer(VkDevice device,
                            VkPhysicalDevice physicalDevice,
                            VkCommandPool commandPool,
                            VkSharingMode sharingMode,
                            VkQueue graphicsQueue,
                            std::vector<Vertex3d> vertices3d);

    void cleanup();

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
