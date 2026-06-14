#ifndef VKVERTEXBUFFERMANAGER_H
#define VKVERTEXBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <vector>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"

namespace aura3d {
namespace vk {

class VkVertexBufferManager
{
public:
    VkVertexBufferManager(VulkanMemoryManager* memoryManager, VkDevice* vkDevice);
    ~VkVertexBufferManager();

    void createVertexBuffer(const std::string& name,
                            VkCommandPool commandPool,
                            VkSharingMode sharingMode,
                            VkQueue graphicsQueue,
                            std::vector<gfx::Vertex3D>&& vertices3d,
                            bool persistentMapping = true);

    void updateVertexBuffer(const std::string& name, std::vector<gfx::Vertex3D>&& vertices3d);

    VertexBufferInfo getVertexBuffer(const std::string& name);
    size_t getVertexCount(const std::string& name);

    static VkVertexInputBindingDescription getBindingDescription();
    static AttributeDescriptionArray<VkVertexInputAttributeDescription> getAttributeDescriptions();
    static u32 getAttributeDescriptionCount();

    void cleanup(const std::string& name);
    void cleanup();

private:
    VulkanMemoryManager* _memoryManager;
    VkDevice* _vkDevice;
    std::unordered_map<std::string, VertexBufferInfo> _vertexBuffers;
};

} // namespace vk
} // namespace aura3d

#endif // VKVERTEXBUFFERMANAGER_H
