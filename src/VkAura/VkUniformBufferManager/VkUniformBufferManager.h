#ifndef VKUNIFORMBUFFERMANAGER_H
#define VKUNIFORMBUFFERMANAGER_H
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include "VkAura/VkBufferMemoryAllocator/VkBufferMemoryAllocator.h"

namespace aura3d {

/**
 * @struct TransformUBO
 * @brief Uniform buffer object structure matching the shader UBO
 */
struct TransformUBO {
    glm::mat4 transform; // Transformation matrix for UI elements
};

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
    VkUniformBufferManager(VkDevice* vkDevice);

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
        uint32_t count,
        VkBufferMemoryAllocator* allocator);

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
    VkDevice* _vkDevice;                      ///< Pointer to Vulkan device
    std::vector<VkBuffer> _uniformBuffers;    ///< Uniform buffer handles
    std::vector<VkDeviceMemory> _uniformBuffersMemory; ///< Memory for uniform buffers
    std::vector<VkDeviceSize> _bufferOffsets; ///< Buffer offsets for uniform buffers
    std::vector<void*> _mappedMemory;         ///< Pointers to mapped memory
};

} // namespace aura3d

#endif // VKUNIFORMBUFFERMANAGER_H
