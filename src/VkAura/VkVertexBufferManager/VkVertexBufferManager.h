#ifndef VKVERTEXBUFFERMANAGER_H
#define VKVERTEXBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>
#include <vector>

#include "VkAura/VkAuraCore.h"
#include "VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h"
#include "VkAura/VkMemory/VkDeviceAllocator/VkDeviceAllocator.h"

namespace aura3d {
namespace vk {

class VkVertexBufferManager
{
public:
    VkVertexBufferManager(VkHostAllocator* vkHostAllocator,
                          VkDeviceAllocator* vkDeviceAllocator,
                          VkDevice* vkDevice);
    ~VkVertexBufferManager();

    // Create a named vertex buffer with 2D vertices
    void createVertexBuffer(const std::string& name,
                            VkPhysicalDevice physicalDevice,
                            VkCommandPool commandPool,
                            VkSharingMode sharingMode,
                            VkQueue graphicsQueue,
                            const std::vector<Vertex2d>& vertices2d,
                            bool persistentMapping = false);

    // Create a named vertex buffer with 3D vertices
    void createVertexBuffer(const std::string& name,
                            VkPhysicalDevice physicalDevice,
                            VkCommandPool commandPool,
                            VkSharingMode sharingMode,
                            VkQueue graphicsQueue,
                            const std::vector<Vertex3d>& vertices3d,
                            bool persistentMapping = false);

    // Update an existing buffer with new vertex data
    void updateVertexBuffer(const std::string& name, const std::vector<Vertex2d>& vertices2d);
    void updateVertexBuffer(const std::string& name, const std::vector<Vertex3d>& vertices3d);

    // Get a specific vertex buffer by name
    VertexBufferInfo getVertexBuffer(const std::string& name);

    // Get vertex count for a specific buffer
    size_t getVertexCount(const std::string& name);

    // Static helper methods for pipeline setup
    static VkVertexInputBindingDescription getBindingDescription(bool is2d);
    static AttributeDescriptionArray<VkVertexInputAttributeDescription> getAttributeDescriptions(bool is2d);

    // Clean up a specific buffer or all buffers
    void cleanup(const std::string& name);
    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDeviceAllocator* vkDeviceAllocator;
    VkDevice* _vkDevice;

    std::unordered_map<std::string, VertexBufferInfo> _vertexBuffers;
};

}
} // namespace aura3d
#endif // VKVERTEXBUFFERMANAGER_H
