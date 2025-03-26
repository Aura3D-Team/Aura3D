#ifndef VKUNIFORMBUFFERMANAGER_H
#define VKUNIFORMBUFFERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "VkAura/VkAuraDefs.h"
#include <VkAura/VkHostAllocator/VkHostAllocator.h>
#include <VkAura/VkDeviceAllocator/VkDeviceAllocator.h>

namespace aura3d {

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
    /**
     * @brief Constructor
     * @param vkDevice Pointer to the Vulkan device
     */
    VkUniformBufferManager(VkHostAllocator* vkHostAllocator,
                           VkDeviceAllocator* vkDeviceAllocator,
                           VkDevice* vkDevice);

    /**
     * @brief Destructor - cleans up resources
     */
    ~VkUniformBufferManager();

    /**
     * @brief Creates uniform buffers (one per swapchain image)
     *
     * @param physicalDevice Physical device handle
     * @param sharingMode Buffer sharing mode
     * @param count Number of buffers to create (typically matches swapchain image count)
     */
    void createUniformBuffers(
        VkPhysicalDevice physicalDevice,
        VkSharingMode sharingMode,
        uint32_t count);

    /**
     * @brief Updates a uniform buffer with new transform data
     *
     * @param currentImage Index of the current swapchain image
     * @param ubo Transform UBO data to upload
     */
    void updateUniformBuffer(uint32_t currentImage, const TransformUBO& ubo);

    /**
     * @brief Gets a uniform buffer handle
     *
     * @param index Buffer index (typically the current swapchain image index)
     * @return VkBuffer handle
     */
    VkBuffer getUniformBuffer(uint32_t index) const;

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
    VkDescriptorSetLayoutBinding getDescriptorSetLayoutBinding(uint32_t binding = 0) const;

    /**
     * @brief Gets descriptor buffer info for a specific uniform buffer
     *
     * @param index Buffer index
     * @return VkDescriptorBufferInfo structure
     */
    VkDescriptorBufferInfo getDescriptorBufferInfo(uint32_t index) const;

    /**
     * @brief Cleans up resources
     */
    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDeviceAllocator* vkDeviceAllocator;
    VkDevice* _vkDevice; ///< Pointer to Vulkan device

    std::vector<VkBuffer> _uniformBuffers;    ///< Uniform buffer handles
    std::vector<VkDeviceAllocation> _allocations; ///< Memory allocations with mapping info
};

} // namespace aura3d
#endif // VKUNIFORMBUFFERMANAGER_H
