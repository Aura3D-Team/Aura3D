#ifndef VKUNIFORMBUFFERMANAGER_H
#define VKUNIFORMBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "aura/aura.h"
#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"

namespace aura3d {
namespace vk {

/**
 * @class VkUniformBufferManager
 * @brief Manages uniform buffers for shader UBOs
 *
 * This class creates and maintains uniform buffers that hold transformation
 * data for shaders. It works with VkBufferManager and supports per-swapchain
 * image buffers for triple buffering.
 */
class VkUniformBufferManager {
public:
    VkUniformBufferManager(VulkanMemoryManager* memoryManager, VkDevice* vkDevice);
    ~VkUniformBufferManager();

    /**
     * @brief Creates uniform buffers (one per swapchain image)
     *
     * @param sharingMode Buffer sharing mode
     * @param count Number of buffers to create (typically matches swapchain image count)
     */
    void createUniformBuffers(
        VkSharingMode sharingMode,
        u32 count);

    /**
     * @brief Updates a uniform buffer with new transform data
     *
     * @param currentImage Index of the current swapchain image
     * @param ubo Transform UBO data to upload
     */
    void updateUniformBuffer(u32 currentImage, gfx::TransformUBO& ubo);

    /**
     * @brief Gets a uniform buffer handle
     *
     * @param index Buffer index (typically the current swapchain image index)
     * @return VkBuffer handle
     */
    VkBuffer getUniformBuffer(u32 index) const;

    /**
     * @brief Gets the size of the uniform buffer
     *
     * @return Size in bytes
     */
    VkDeviceSize getUniformBufferSize() const;

    /**
     * @brief Gets descriptor set layout binding for the uniform buffer
     *
     * @param binding Shader binding point
     * @return VkDescriptorSetLayoutBinding structure
     */
    VkDescriptorSetLayoutBinding getDescriptorSetLayoutBinding(u32 binding = 0) const;

    /**
     * @brief Gets descriptor buffer info for a specific uniform buffer
     *
     * @param index Buffer index
     * @return VkDescriptorBufferInfo structure
     */
    VkDescriptorBufferInfo getDescriptorBufferInfo(u32 index) const;

    /**
     * @brief Cleans up resources
     */
    void cleanup();

private:
    VulkanMemoryManager* _memoryManager;
    VkDevice* _vkDevice;
    std::vector<AllocatedBuffer> _buffers;
};

} // namespace vk
} // namespace aura3d

#endif // VKUNIFORMBUFFERMANAGER_H
