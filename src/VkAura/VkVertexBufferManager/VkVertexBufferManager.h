#ifndef VKVERTEXBUFFERMANAGER_H
#define VKVERTEXBUFFERMANAGER_H

#pragma once

#include <VkAura/VkDeviceAllocator/VkDeviceAllocator.h>

#include <aura.hpp>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>


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

// Structure to hold vertex buffer information
struct VertexBufferInfo {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize offset;     // Add this to store the memory offset
    void* mappedMemory;      // For persistent mapping
    size_t vertexCount;
    bool is2d;              // Whether the buffer contains 2D or 3D vertices
    bool persistent;        // Whether the buffer is persistently mapped
};

class VkVertexBufferManager
{
public:
    VkVertexBufferManager(VkDevice* vkDevice);
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
    VkDevice* _vkDevice;
    std::unordered_map<std::string, VertexBufferInfo> _vertexBuffers;

    // Helper to unmap memory if needed
    void unmapMemory(const std::string& name);
};

} // namespace aura3d
#endif // VKVERTEXBUFFERMANAGER_H
