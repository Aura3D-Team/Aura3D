#ifndef VKINDEXBUFFERMANAGER_H
#define VKINDEXBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>
#include <vector>

#include "aura.hpp"
#include "VkAura/VkAuraDefs.h"
#include "VkAura/VkHostAllocator/VkHostAllocator.h"
#include "VkAura/VkDeviceAllocator/VkDeviceAllocator.h"

namespace aura3d {

class VkIndexBufferManager
{
public:
    VkIndexBufferManager(VkHostAllocator* vkHostAllocator,
                         VkDeviceAllocator* vkDeviceAllocator,
                         VkDevice* vkDevice);
    ~VkIndexBufferManager();

    // Create a named Index buffer
    void createIndexBuffer(const std::string& name,
                           VkPhysicalDevice physicalDevice,
                           VkCommandPool commandPool,
                           VkSharingMode sharingMode,
                           VkQueue graphicsQueue,
                           const std::vector<u16>& indices,
                           bool persistentMapping = false);

    // Update an existing buffer with new vertex data
    void updateIndexBuffer(const std::string& name, const std::vector<u16>& indices);

    // Get a specific vertex buffer by name
    IndexBufferInfo getIndexBuffer(const std::string& name);

    // Clean up a specific buffer or all buffers
    void cleanup(const std::string& name);
    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDeviceAllocator* vkDeviceAllocator;
    VkDevice* _vkDevice;

    std::unordered_map<std::string, IndexBufferInfo> _indexBuffers;
};

}

#endif // VKINDEXBUFFERMANAGER_H
