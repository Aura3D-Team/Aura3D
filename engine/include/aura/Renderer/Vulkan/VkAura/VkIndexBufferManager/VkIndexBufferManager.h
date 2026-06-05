#ifndef VKINDEXBUFFERMANAGER_H
#define VKINDEXBUFFERMANAGER_H

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

class VkIndexBufferManager
{
public:
    VkIndexBufferManager(VkHostAllocator* vkHostAllocator,
                         VkDeviceAllocator* vkDeviceAllocator,
                         VkDevice* vkDevice);
    ~VkIndexBufferManager();

    void createIndexBuffer(const std::string& name,
                           VkPhysicalDevice physicalDevice,
                           VkCommandPool commandPool,
                           VkSharingMode sharingMode,
                           VkQueue graphicsQueue,
                           std::vector<u16>&& indices,
                           bool persistentMapping = true);

    void updateIndexBuffer(const std::string& name, std::vector<u16>&& indices);

    IndexBufferInfo getIndexBuffer(const std::string& name);

    void cleanup(const std::string& name);
    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDeviceAllocator* vkDeviceAllocator;
    VkDevice* _vkDevice;

    std::unordered_map<std::string, IndexBufferInfo> _indexBuffers;
};

}
}

#endif // VKINDEXBUFFERMANAGER_H
