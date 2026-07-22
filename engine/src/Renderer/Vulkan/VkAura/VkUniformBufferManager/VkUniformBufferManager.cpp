#include "aura/Renderer/Vulkan/VkAura/VkUniformBufferManager/VkUniformBufferManager.h"

#include <cstring>

#include "aura/aura.h"

namespace aura3d {
namespace vk {

VkUniformBufferManager::VkUniformBufferManager(VulkanMemoryManager* memoryManager, VkDevice* vkDevice)
    : _memoryManager(memoryManager), _vkDevice(vkDevice)
{
}

VkUniformBufferManager::~VkUniformBufferManager()
{
    cleanup();
}

void VkUniformBufferManager::createUniformBuffers(VkSharingMode sharingMode, u32 count, VkDeviceSize elementSize)
{
    cleanup();

    _elementSize = elementSize;
    const VkDeviceSize bufferSize = _elementSize;
    _buffers.resize(count);

    VmaAllocationCreateFlags flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (u32 i = 0; i < count; ++i) {
        _buffers[i] = _memoryManager->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            sharingMode,
            VMA_MEMORY_USAGE_AUTO,
            flags);

        if (!_buffers[i].mappedData) {
            _buffers[i].mappedData = _memoryManager->map(_buffers[i]);
        }
    }
}

void VkUniformBufferManager::updateUniformBuffer(u32 currentImage, gfx::TransformUBO& ubo)
{
    updateUniformBufferRaw(currentImage, &ubo, sizeof(gfx::TransformUBO));
}

void VkUniformBufferManager::updateUniformBufferRaw(u32 currentImage, const void* data, VkDeviceSize size)
{
    if (currentImage >= _buffers.size() || _buffers[currentImage].mappedData == nullptr || !data)
        return;

    if (size > _elementSize)
    {
        INK_ERROR << "updateUniformBuffer: write of " << size
                  << " bytes exceeds the " << _elementSize << "-byte buffer";
        return;
    }

    std::memcpy(_buffers[currentImage].mappedData, data, static_cast<size_t>(size));
}

VkBuffer VkUniformBufferManager::getUniformBuffer(u32 index) const
{
    if (index < _buffers.size()) {
        return _buffers[index].buffer;
    }
    return VK_NULL_HANDLE;
}

VkDeviceSize VkUniformBufferManager::getUniformBufferSize() const
{
    return _elementSize;
}

VkDescriptorSetLayoutBinding VkUniformBufferManager::getDescriptorSetLayoutBinding(u32 binding) const
{
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBinding.pImmutableSamplers = nullptr;
    return layoutBinding;
}

VkDescriptorBufferInfo VkUniformBufferManager::getDescriptorBufferInfo(u32 index) const
{
    VkDescriptorBufferInfo bufferInfo{};
    if (index < _buffers.size()) {
        bufferInfo.buffer = _buffers[index].buffer;
        // _buffers[index].offset is VMA's suballocation offset within a
        // shared VkDeviceMemory block, not an offset into this VkBuffer —
        // each element owns its own dedicated buffer, so this is always 0.
        bufferInfo.offset = 0;
        bufferInfo.range = _elementSize;
    }
    return bufferInfo;
}

void VkUniformBufferManager::cleanup()
{
    for (auto& buffer : _buffers) {
        _memoryManager->destroyBuffer(buffer);
    }
    _buffers.clear();
}

} // namespace vk
} // namespace aura3d
