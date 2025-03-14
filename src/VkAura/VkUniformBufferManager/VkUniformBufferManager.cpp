#include "VkUniformBufferManager.h"
#include "VkAura/VkBufferManager/VkBufferManager.h"
#include <cstring>

namespace aura3d {

VkUniformBufferManager::VkUniformBufferManager(VkDevice* vkDevice) :
    _vkDevice(vkDevice)
{
    // Initialize buffers with empty vectors
}

VkUniformBufferManager::~VkUniformBufferManager()
{
    cleanup();
}

void VkUniformBufferManager::createUniformBuffers(
    VkPhysicalDevice physicalDevice,
    VkSharingMode sharingMode,
    uint32_t count,
    VkBufferMemoryAllocator* allocator)
{
    // Clean up existing buffers if any
    cleanup();

    // Calculate buffer size
    VkDeviceSize bufferSize = sizeof(TransformUBO);

    // Resize vectors to hold the requested number of buffers
    _uniformBuffers.resize(count, VK_NULL_HANDLE);
    _uniformBuffersMemory.resize(count, VK_NULL_HANDLE);
    _bufferOffsets.resize(count, 0); // Add this to store offsets
    _mappedMemory.resize(count, nullptr);

    // Create uniform buffers
    for (size_t i = 0; i < count; i++) {
        // Create host-visible buffer for easy updates
        VkBufferManager::createBuffer(
            *_vkDevice,
            physicalDevice,
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            sharingMode,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            _uniformBuffers[i],
            _uniformBuffersMemory[i],
            _bufferOffsets[i], // Pass the offset
            allocator
            );

        // Persistently map the memory for efficient updates
        // If using allocator, we need to get the mapping from it
        if (allocator) {
            AllocationInfo info = allocator->getAllocationInfo(_uniformBuffersMemory[i], _bufferOffsets[i]);
            _mappedMemory[i] = info.mappedData;
        } else {
            _mappedMemory[i] = VkBufferManager::mapBufferMemory(
                *_vkDevice,
                _uniformBuffersMemory[i],
                bufferSize
                );
        }
    }
}
void VkUniformBufferManager::updateUniformBuffer(uint32_t currentImage, const TransformUBO& ubo)
{
    // Check if the index is valid
    if (currentImage >= _mappedMemory.size() || _mappedMemory[currentImage] == nullptr) {
        // Index out of range or buffer not initialized
        return;
    }

    // Copy the new UBO data directly to the mapped memory
    std::memcpy(_mappedMemory[currentImage], &ubo, sizeof(ubo));

    // No need to call vkFlushMappedMemoryRanges if the memory is coherent
    // (which we specified when creating the buffer)
}

VkBuffer VkUniformBufferManager::getUniformBuffer(uint32_t index) const
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

VkDescriptorSetLayoutBinding VkUniformBufferManager::getDescriptorSetLayoutBinding(uint32_t binding) const
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

VkDescriptorBufferInfo VkUniformBufferManager::getDescriptorBufferInfo(uint32_t index) const
{
    // Create a descriptor buffer info for the uniform buffer
    VkDescriptorBufferInfo bufferInfo{};

    if (index < _uniformBuffers.size()) {
        bufferInfo.buffer = _uniformBuffers[index];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(TransformUBO);
    }

    return bufferInfo;
}

void VkUniformBufferManager::cleanup()
{
    // Unmap memory, destroy buffers, and free memory
    for (size_t i = 0; i < _uniformBuffers.size(); i++) {
        if (_mappedMemory[i] != nullptr) {
            VkBufferManager::unmapBufferMemory(*_vkDevice, _uniformBuffersMemory[i]);
            _mappedMemory[i] = nullptr;
        }

        if (_uniformBuffers[i] != VK_NULL_HANDLE) {
            VkBufferManager::destroyBuffer(*_vkDevice, _uniformBuffers[i], _uniformBuffersMemory[i]);
            _uniformBuffers[i] = VK_NULL_HANDLE;
            _uniformBuffersMemory[i] = VK_NULL_HANDLE;
        }
    }

    // Clear the vectors
    _uniformBuffers.clear();
    _uniformBuffersMemory.clear();
    _mappedMemory.clear();
}

} // namespace aura3d
