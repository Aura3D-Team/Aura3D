#include "aura/Renderer/Vulkan/VkAura/VkDescriptorManager/VkDescriptorManager.h"

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"

namespace aura3d {
namespace vk {

VkDescriptorManager::VkDescriptorManager(VkDevice* vkDevice, u32 bindlessTextureCapacity, i32 pool_size) :
    _vkDevice(vkDevice), _pool_size(pool_size)
{
    /*
     * Uniform-buffer sets: the transform (set 0) and light (set 2) UBOs,
     * one pair per swapchain image, freed and reallocated on every resize --
     * pool_size is generous headroom for that per-frame-count churn, not a
     * texture-count budget (see the constructor's doc comment).
     *
     * Combined-image-sampler sets: exactly two persistent bindless texture
     * arrays (VulkanRenderer's 3D and overlay set 1), each declared with
     * descriptorCount == bindlessTextureCapacity. This is a fixed, one-time
     * reservation -- it does not grow with the number of textures actually
     * created, which is the entire point of the bindless migration (a
     * per-texture-per-image-per-pipeline scheme exhausted a 256-descriptor
     * pool around the 42nd texture).
     */
    std::array<VkDescriptorPoolSize, 2> poolSizes = {};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<u32>(_pool_size);

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = bindlessTextureCapacity * 2u;

    _poolCreateInfo = {};
    _poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    _poolCreateInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    _poolCreateInfo.pPoolSizes = poolSizes.data();
    //! +4: the two persistent bindless sets, plus slack above the per-image
    //! UBO churn pool_size*2 already generously covers.
    _poolCreateInfo.maxSets = static_cast<u32>(_pool_size) * 2u + 4u;
    //! FREE_DESCRIPTOR_SET: the per-image transform/light sets are freed and
    //! reallocated on every swapchain resize (see
    //! VulkanRenderer::destroySwapchainResources / freeDescriptorSets()).
    //! UPDATE_AFTER_BIND: the bindless texture-array sets are written to
    //! (new textures bound into array elements) even while a previously
    //! recorded, not-yet-executed command buffer still references them.
    _poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
                           | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    VK_RESULT_CHECK(vkCreateDescriptorPool(*_vkDevice, &_poolCreateInfo, nullptr, &_descriptorPool));

    //! pPoolSizes pointed at the local array above; do not leave a dangling
    //! pointer behind on a member that outlives it.
    _poolCreateInfo.pPoolSizes = nullptr;
    _poolCreateInfo.poolSizeCount = 0;
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
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &dSetLayout;

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
void VkDescriptorManager::updateDescriptorSet(VkDescriptorSet descriptorSet, u32 binding,
                                              VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset)
{
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = size;

    VkWriteDescriptorSet writeDescriptorSet = {};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding = binding;
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(*_vkDevice, 1, &writeDescriptorSet, 0, nullptr);
}

/**
 * Updates a descriptor set with combined image sampler information
 *
 * This connects a specific image and sampler to a specific binding point in a descriptor set.
 * The shader can then access this texture through the binding point.
 */
void VkDescriptorManager::updateCombinedImageSamplerDescriptorSet(VkDescriptorSet descriptorSet,
                                                                  u32 binding,
                                                                  VkImageView imageView,
                                                                  VkSampler sampler)
{
    // Configure image info
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet writeDescriptorSet = {};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding = binding;
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(*_vkDevice, 1, &writeDescriptorSet, 0, nullptr);
}

/**
 * Writes one element of a bindless combined-image-sampler array binding.
 * Identical to updateCombinedImageSamplerDescriptorSet() except the write
 * targets dstArrayElement instead of always element 0.
 */
void VkDescriptorManager::updateTextureArrayElement(VkDescriptorSet descriptorSet,
                                                    u32 binding, u32 arrayElement,
                                                    VkImageView imageView, VkSampler sampler)
{
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet writeDescriptorSet = {};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding = binding;
    writeDescriptorSet.dstArrayElement = arrayElement;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(*_vkDevice, 1, &writeDescriptorSet, 0, nullptr);
}

/**
 * Frees every set in @p sets back to the pool and clears the vector.
 */
void VkDescriptorManager::freeDescriptorSets(std::vector<VkDescriptorSet>& sets)
{
    if (!sets.empty()) {
        vkFreeDescriptorSets(*_vkDevice, _descriptorPool,
                             static_cast<u32>(sets.size()), sets.data());
    }
    sets.clear();
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

}
} // namespace aura3d
