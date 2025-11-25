#include "VkUniformBufferManager.h"
#include <aura.hpp>
#include <cstring>

#include "VkAura/VkBufferManager/VkBufferManager.h"
#include <AuraException/AuraException.h>

namespace aura3d {
namespace vk {

VkUniformBufferManager::VkUniformBufferManager(VkHostAllocator* vkHostAllocator,
                                               VkDeviceAllocator* vkDeviceAllocator,
                                               VkDevice* vkDevice) :
    vkHostAllocator(vkHostAllocator), vkDeviceAllocator(vkDeviceAllocator), _vkDevice(vkDevice)
{
    // Initialize with empty vectors - will be populated in createUniformBuffers
}

VkUniformBufferManager::~VkUniformBufferManager()
{
    cleanup();
}

void VkUniformBufferManager::createUniformBuffers(
    VkPhysicalDevice physicalDevice,
    VkSharingMode sharingMode,
    u32 count)
{
    // Clean up existing buffers if any
    cleanup();

    // Calculate buffer size
    VkDeviceSize bufferSize = sizeof(TransformUBO);

    // Resize vectors to hold the requested number of buffers
    _uniformBuffers.resize(count, VK_NULL_HANDLE);
    _allocations.resize(count);

    // Create uniform buffers
    for (size_t i = 0; i < count; i++) {
        // Create host-visible buffer for easy updates
        VkBufferManager::createBuffer(
            vkHostAllocator,
            vkDeviceAllocator,
            *_vkDevice,
            physicalDevice,
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            sharingMode,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            _uniformBuffers[i],
            _allocations[i]);

        // Persistently map the memory for efficient updates
        void* data = nullptr;
        VK_RESULT_CHECK(vkDeviceAllocator->mapMemory(_allocations[i], 0, bufferSize, &data));
        // Note: The mappedData field in the allocation is automatically set by the
        // mapMemory function, so we don't need to store it separately
    }
}

void VkUniformBufferManager::updateUniformBuffer(u32 currentImage, TransformUBO& ubo)
{
    // Check if the index is valid
    if (currentImage >= _allocations.size() || _allocations[currentImage].mappedData == nullptr) {
        // Index out of range or buffer not mapped
        return;
    }

    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    f32 time = std::chrono::duration<f32, std::chrono::seconds::period>(currentTime - startTime).count();

    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f,0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    // glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float) swapChainExtent.height, 0.1f, 10.0f);

    // Copy the new UBO data directly to the mapped memory
    std::memcpy(_allocations[currentImage].mappedData, &ubo, sizeof(ubo));
    // No need to call vkFlushMappedMemoryRanges if the memory is coherent
    // (which we specified when creating the buffer)
}

VkBuffer VkUniformBufferManager::getUniformBuffer(u32 index) const
{
    if (index < _uniformBuffers.size()) {
        return _uniformBuffers[index];
    }
    return VK_NULL_HANDLE;
}

VkDeviceSize VkUniformBufferManager::getUniformBufferSize() const
{
    return sizeof(TransformUBO);
}

VkDescriptorSetLayoutBinding VkUniformBufferManager::getDescriptorSetLayoutBinding(u32 binding) const
{
    // Create a descriptor set layout binding for the uniform buffer
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Used in vertex shader
    layoutBinding.pImmutableSamplers = nullptr;
    return layoutBinding;
}

VkDescriptorBufferInfo VkUniformBufferManager::getDescriptorBufferInfo(u32 index) const
{
    // Create a descriptor buffer info for the uniform buffer
    VkDescriptorBufferInfo bufferInfo{};
    if (index < _uniformBuffers.size() && index < _allocations.size()) {
        bufferInfo.buffer = _uniformBuffers[index];
        bufferInfo.offset = _allocations[index].offset;
        bufferInfo.range = sizeof(TransformUBO);
    }
    return bufferInfo;
}

void VkUniformBufferManager::cleanup()
{
    // Unmap memory, destroy buffers, and free memory
    for (size_t i = 0; i < _uniformBuffers.size(); i++) {
        if (i < _allocations.size() && _allocations[i].mappedData != nullptr) {
            vkDeviceAllocator->unmapMemory(_allocations[i]);
            // mappedData will be set to nullptr in unmapMemory
        }

        if (_uniformBuffers[i] != VK_NULL_HANDLE) {
            if (i < _allocations.size()) {
                vkDestroyBuffer(*_vkDevice, _uniformBuffers[i], vkHostAllocator->getCallbacks());
                vkDeviceAllocator->freeMemory(_allocations[i]);
            }
            _uniformBuffers[i] = VK_NULL_HANDLE;
        }
    }

    // Clear the vectors
    _uniformBuffers.clear();
    _allocations.clear();
}

}
} // namespace aura3d
