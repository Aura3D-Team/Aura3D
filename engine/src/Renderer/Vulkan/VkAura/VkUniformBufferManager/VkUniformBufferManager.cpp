#include "aura/Renderer/Vulkan/VkAura/VkUniformBufferManager/VkUniformBufferManager.h"

#include <algorithm>
#include <ranges>
#include <span>

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

void VkUniformBufferManager::createUniformBuffers(VkSharingMode sharingMode, u32 count)
{
    cleanup();

    const VkDeviceSize bufferSize = sizeof(gfx::TransformUBO);
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
    if (currentImage >= _buffers.size() || _buffers[currentImage].mappedData == nullptr) {
        return;
    }

    const auto uboBytes = std::as_bytes(std::span{&ubo, 1});
    const auto dst = std::span{
        static_cast<std::byte*>(_buffers[currentImage].mappedData),
        sizeof(gfx::TransformUBO),
    };
    std::ranges::copy(uboBytes, dst);
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
    return sizeof(gfx::TransformUBO);
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
        bufferInfo.offset = _buffers[index].offset;
        bufferInfo.range = sizeof(gfx::TransformUBO);
    }
    return bufferInfo;
}

void VkUniformBufferManager::cleanup()
{
    for (auto& buffer : _buffers) {
        if (buffer.mappedData && buffer.allocation != VK_NULL_HANDLE) {
            _memoryManager->unmap(buffer);
        }
        _memoryManager->destroyBuffer(buffer);
    }
    _buffers.clear();
}

} // namespace vk
} // namespace aura3d
