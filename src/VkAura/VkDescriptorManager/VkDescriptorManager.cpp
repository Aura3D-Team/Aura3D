#include "VkDescriptorManager.h"
#include <AuraException/AuraException.h>
#include <aura.hpp>

namespace aura3d {

/**
 * Constructor - Creates a descriptor pool
 *
 * A descriptor pool is like a memory manager for descriptor sets.
 * It pre-allocates memory for a certain number and type of descriptors.
 * This allows efficient allocation/deallocation of descriptor sets.
 */
VkDescriptorManager::VkDescriptorManager(VkDevice* vkDevice, int pool_size) :
    _pool_size(pool_size), _vkDevice(vkDevice)
{
    // Configure the pool sizes - for both uniform buffers and combined image samplers
    VkFixedArray<VkDescriptorPoolSize> poolSizes = {};

    // Uniform buffer pool size
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = _pool_size;

    // Combined image sampler pool size
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = _pool_size;

    // Configure the descriptor pool creation info
    _poolCreateInfo = {};
    _poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    _poolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    _poolCreateInfo.pPoolSizes = poolSizes.data();
    _poolCreateInfo.maxSets = _pool_size * 2;  // Maximum number of descriptor sets (for both types)

    // Create the descriptor pool
    VK_RESULT_CHECK(vkCreateDescriptorPool(*_vkDevice, &_poolCreateInfo, nullptr, &_descriptorPool));
}

/**
 * Destructor - Ensures resources are properly released
 */
VkDescriptorManager::~VkDescriptorManager()
{
    cleanup();
}

/**
 * Cleans up the descriptor pool
 *
 * When a descriptor pool is destroyed, all descriptor sets
 * allocated from it are automatically freed.
 */
void VkDescriptorManager::cleanup()
{
    vkDestroyDescriptorPool(*_vkDevice, _descriptorPool, nullptr);
}

/**
 * Allocates a descriptor set from the pool
 *
 * A descriptor set is an object that connects shader variables to resources.
 * The layout specified how many bindings are in the set and what types they are.
 *
 * @return The allocated descriptor set handle
 */
VkDescriptorSet VkDescriptorManager::allocateDescriptorSet(VkDescriptorSetLayout dSetLayout)
{
    // Configure the allocation info
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _descriptorPool;
    allocInfo.descriptorSetCount = 1;  // Number of sets to allocate
    allocInfo.pSetLayouts = &dSetLayout;  // The descriptor set layout to use

    // Allocate the descriptor set
    VkDescriptorSet descriptorSet;
    VK_RESULT_CHECK(vkAllocateDescriptorSets(*_vkDevice, &allocInfo, &descriptorSet));

    return descriptorSet;
}

/**
 * Updates a descriptor set with buffer information
 *
 * This connects a specific buffer to a specific binding point in a descriptor set.
 * The shader can then access this buffer through the binding point.
 */
void VkDescriptorManager::updateDescriptorSet(VkDescriptorSet descriptorSet, uint32_t binding,
                                              VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset)
{
    // Configure buffer info
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = size;

    // Configure write descriptor info
    VkWriteDescriptorSet writeDescriptorSet = {};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding = binding;  // Binding index for the descriptor
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.pBufferInfo = &bufferInfo;

    // Update the descriptor set
    vkUpdateDescriptorSets(*_vkDevice, 1, &writeDescriptorSet, 0, nullptr);
}

/**
 * Updates a descriptor set with combined image sampler information
 *
 * This connects a specific image and sampler to a specific binding point in a descriptor set.
 * The shader can then access this texture through the binding point.
 */
void VkDescriptorManager::updateCombinedImageSamplerDescriptorSet(VkDescriptorSet descriptorSet,
                                                                  uint32_t binding,
                                                                  VkImageView imageView,
                                                                  VkSampler sampler)
{
    // Configure image info
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    // Configure write descriptor info
    VkWriteDescriptorSet writeDescriptorSet = {};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding = binding;  // Binding index for the descriptor
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.pImageInfo = &imageInfo;

    // Update the descriptor set
    vkUpdateDescriptorSets(*_vkDevice, 1, &writeDescriptorSet, 0, nullptr);
}

/**
 * Binds a descriptor set to a command buffer
 *
 * This makes the descriptor set active for subsequent draw commands.
 * The pipeline layout must match the descriptor set layout.
 */
void VkDescriptorManager::bindDescriptorSet(VkCommandBuffer commandBuffer,
                                            VkPipelineLayout pipelineLayout,
                                            VkDescriptorSet descriptorSet,
                                            VkPipelineBindPoint bindPoint)
{
    vkCmdBindDescriptorSets(commandBuffer, bindPoint, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
}

} // namespace aura3d
