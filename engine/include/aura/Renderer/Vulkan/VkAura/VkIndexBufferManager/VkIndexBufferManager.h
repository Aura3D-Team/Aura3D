#ifndef VKINDEXBUFFERMANAGER_H
#define VKINDEXBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <vector>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"

namespace aura3d {
namespace vk {

class VkIndexBufferManager
{
public:
    VkIndexBufferManager(VulkanMemoryManager* memoryManager, VkDevice* vkDevice);
    ~VkIndexBufferManager();

    /**
     * @brief Uploads 16-bit indices (VK_INDEX_TYPE_UINT16).
     */
    void createIndexBuffer(const std::string& name,
                           VkCommandPool commandPool,
                           VkSharingMode sharingMode,
                           VkQueue graphicsQueue,
                           std::vector<u16>&& indices,
                           bool persistentMapping = true);

    /**
     * @brief Uploads 32-bit indices (VK_INDEX_TYPE_UINT32).
     *
     * Kept as a distinct upload path rather than narrowing to u16, so meshes
     * with more than 65535 vertices are not silently corrupted.
     */
    void createIndexBuffer(const std::string& name,
                           VkCommandPool commandPool,
                           VkSharingMode sharingMode,
                           VkQueue graphicsQueue,
                           std::vector<u32>&& indices,
                           bool persistentMapping = true);

    void updateIndexBuffer(const std::string& name, std::vector<u16>&& indices);
    void updateIndexBuffer(const std::string& name, std::vector<u32>&& indices);
    IndexBufferInfo getIndexBuffer(const std::string& name);

    void cleanup(const std::string& name);
    void cleanup();

private:
    //! Shared upload path for both index widths; @p indexType is recorded on
    //! the resulting IndexBufferInfo for later vkCmdBindIndexBuffer calls.
    template <typename IndexT>
    void createIndexBufferImpl(const std::string& name,
                               VkCommandPool commandPool,
                               VkSharingMode sharingMode,
                               VkQueue graphicsQueue,
                               std::vector<IndexT>&& indices,
                               bool persistentMapping,
                               VkIndexType indexType);

    template <typename IndexT>
    void updateIndexBufferImpl(const std::string& name, std::vector<IndexT>&& indices);

    VulkanMemoryManager* _memoryManager;
    VkDevice* _vkDevice;
    std::unordered_map<std::string, IndexBufferInfo> _indexBuffers;
};

} // namespace vk
} // namespace aura3d

#endif // VKINDEXBUFFERMANAGER_H
