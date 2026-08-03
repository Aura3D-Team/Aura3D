#ifndef VKDESCRIPTORMANAGER_H
#define VKDESCRIPTORMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "aura/aura.h"

namespace aura3d {
namespace vk {

/**
 * @class VkDescriptorManager
 *
 * @brief This class manages Vulkan descriptor pools and sets.
 *
 * Descriptor Sets in Vulkan:
 * - Descriptor sets are objects that connect shader resources (like buffers, images) with the pipeline
 * - They act as an interface between shader programs and their referenced resources
 * - Think of them as "binding tables" that tell shaders where to find their resources
 *
 * Descriptor Pools in Vulkan:
 * - Descriptor pools are memory allocators for descriptor sets
 * - They manage a pool of memory from which descriptor sets can be allocated
 * - They specify the maximum number and types of descriptors that can be allocated
 */
class VkDescriptorManager
{
public:
    /**
     * Constructor - initializes the descriptor pool
     * @param vkDevice Pointer to the Vulkan logical device
     * @param bindlessTextureCapacity Slots in each of the two persistent
     *        bindless texture-array sets (3D + overlay). Must be the value
     *        VkDeviceManager::maxBindlessTextures() resolved against the real
     *        device, never kDesiredBindlessTextures directly -- the pool has
     *        to reserve exactly what the descriptor set layouts declare.
     * @param pool_size Uniform-buffer descriptors to reserve (the transform
     *        and light UBO sets, one pair per swapchain image, freed and
     *        reallocated on every resize -- see VulkanRenderer::destroySwapchainResources).
     *        Texture count does not scale this pool at all anymore.
     */
    VkDescriptorManager(VkDevice* vkDevice, u32 bindlessTextureCapacity, i32 pool_size = 256);

    /**
     * Destructor - cleans up resources
     */
    ~VkDescriptorManager();

    /**
     * Allocates a descriptor set from the pool
     * @param dSetLayout Descriptor set layout that defines the set's structure
     * @return The allocated descriptor set
     */
    VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout dSetLayout);

    /**
     * Updates a descriptor set with buffer information
     * @param descriptorSet The descriptor set to update
     * @param binding The binding point in the shader
     * @param buffer The uniform buffer to bind
     * @param size Size of the data in the buffer
     * @param offset Offset into the buffer
     */
    void updateDescriptorSet(VkDescriptorSet descriptorSet, u32 binding,
                             VkBuffer buffer, VkDeviceSize size,
                             VkDeviceSize offset = 0);

    void updateCombinedImageSamplerDescriptorSet(VkDescriptorSet descriptorSet,
                                                 u32 binding,
                                                 VkImageView imageView,
                                                 VkSampler sampler);

    /**
     * Writes one element of a bindless combined-image-sampler array binding
     * (VulkanRenderer's set 1 texture array), rather than element 0 of an
     * ordinary single-descriptor binding. The pool this set was allocated
     * from must have been created with UPDATE_AFTER_BIND (see the
     * constructor) so this remains valid even while a previously-recorded
     * command buffer still references the set.
     * @param arrayElement Index into the array binding (a TextureHandle - 1).
     */
    void updateTextureArrayElement(VkDescriptorSet descriptorSet, u32 binding, u32 arrayElement,
                                   VkImageView imageView, VkSampler sampler);

    /**
     * Frees every set in @p sets back to the pool (requires
     * VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, set by the
     * constructor) and clears the vector. Used for the per-image transform/
     * light sets, which are freed and reallocated on every swapchain resize
     * rather than accumulating in the pool across the renderer's lifetime.
     */
    void freeDescriptorSets(std::vector<VkDescriptorSet>& sets);

    /**
     * Binds a descriptor set to a command buffer
     * @param commandBuffer The command buffer to bind to
     * @param pipelineLayout The pipeline layout
     * @param descriptorSet The descriptor set to bind
     * @param bindPoint The pipeline bind point (graphics or compute)
     */
    void bindDescriptorSet(VkCommandBuffer commandBuffer,
                           VkPipelineLayout pipelineLayout,
                           VkDescriptorSet descriptorSet,
                           VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS);

    /**
     * Cleans up the descriptor pool
     */
    void cleanup();

private:
    VkDevice* _vkDevice;                  // Pointer to the Vulkan device

    VkDescriptorPool _descriptorPool;      // The descriptor pool handle
    VkDescriptorPoolSize _poolSize;        // Size configuration for the pool
    VkDescriptorPoolCreateInfo _poolCreateInfo; // Creation info for the pool
    i32 _pool_size;                       // Number of sets in the pool
};

}
} // namespace aura3d

#endif // VKDESCRIPTORMANAGER_H
