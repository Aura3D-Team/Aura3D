#ifndef VKVERTEXBUFFERMANAGER_H
#define VKVERTEXBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>
#include <vector>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VkDeviceAllocator/VkDeviceAllocator.h"

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
                            std::vector<gfx::Vertex2D>&& vertices2d,
                            bool persistentMapping = true);

    void createVertexBuffer(const std::string& name,
                            VkPhysicalDevice physicalDevice,
                            VkCommandPool commandPool,
                            VkSharingMode sharingMode,
                            VkQueue graphicsQueue,
                            std::vector<gfx::Vertex3D>&& vertices3d,
                            bool persistentMapping = true);

    void updateVertexBuffer(const std::string& name, std::vector<gfx::Vertex2D>&& vertices2d);
    void updateVertexBuffer(const std::string& name, std::vector<gfx::Vertex3D>&& vertices3d);

    // Get a specific vertex buffer by name
    VertexBufferInfo getVertexBuffer(const std::string& name);

    // Get vertex count for a specific buffer
    size_t getVertexCount(const std::string& name);

    static VkVertexInputBindingDescription getBindingDescription(bool is2d);
    static AttributeDescriptionArray<VkVertexInputAttributeDescription> getAttributeDescriptions(bool is2d);
    static u32 getAttributeDescriptionCount(bool is2d);

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
