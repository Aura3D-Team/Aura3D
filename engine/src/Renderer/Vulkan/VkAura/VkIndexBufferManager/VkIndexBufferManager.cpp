#include "aura/Renderer/Vulkan/VkAura/VkIndexBufferManager/VkIndexBufferManager.h"

#include <algorithm>
#include <ranges>
#include <span>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"

namespace aura3d {
namespace vk {

namespace {

void destroyBufferInfo(VulkanMemoryManager* memory, IndexBufferInfo& info)
{
    if (info.persistent && info.mappedPointer && info.allocation != VK_NULL_HANDLE) {
        memory->unmap({info.buffer, info.allocation, info.mappedPointer, info.memoryOffset});
    }

    AllocatedBuffer allocated{info.buffer, info.allocation, info.mappedPointer, info.memoryOffset};
    memory->destroyBuffer(allocated);
    info = {};
}

void fillFromAllocated(IndexBufferInfo& info, const AllocatedBuffer& allocated)
{
    info.buffer = allocated.buffer;
    info.allocation = allocated.allocation;
    info.memoryOffset = allocated.offset;
    info.mappedPointer = allocated.mappedData;
}

} // namespace

VkIndexBufferManager::VkIndexBufferManager(VulkanMemoryManager* memoryManager, VkDevice* vkDevice)
    : _memoryManager(memoryManager), _vkDevice(vkDevice)
{
}

VkIndexBufferManager::~VkIndexBufferManager()
{
    cleanup();
}

void VkIndexBufferManager::createIndexBuffer(const std::string& name,
                                             VkCommandPool commandPool,
                                             VkSharingMode sharingMode,
                                             VkQueue graphicsQueue,
                                             std::vector<u16>&& indices,
                                             bool persistentMapping)
{
    cleanup(name);

    if (indices.empty()) {
        INK_WARN << "createIndexBuffer: empty index data for " << name;
        return;
    }

    const VkDeviceSize bufferSize = sizeof(u16) * indices.size();
    IndexBufferInfo bufferInfo{};
    bufferInfo.indexCount = static_cast<u32>(indices.size());

    if (persistentMapping) {
        VmaAllocationCreateFlags flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;

        AllocatedBuffer allocated = _memoryManager->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            sharingMode,
            VMA_MEMORY_USAGE_AUTO,
            flags);

        fillFromAllocated(bufferInfo, allocated);
        bufferInfo.persistent = true;

        if (bufferInfo.mappedPointer) {
            std::ranges::copy(indices,
                              std::span{static_cast<u16*>(bufferInfo.mappedPointer), indices.size()});
        }

        INK_DEBUG << "Created persistently mapped index buffer: " << name
                  << ", indices: " << bufferInfo.indexCount;
    } else {
        AllocatedBuffer staging = _memoryManager->createUploadBuffer(bufferSize, sharingMode);
        if (staging.mappedData) {
            std::ranges::copy(indices, std::span{static_cast<u16*>(staging.mappedData), indices.size()});
        } else {
            auto* data = static_cast<u16*>(_memoryManager->map(staging));
            std::ranges::copy(indices, std::span{data, indices.size()});
            _memoryManager->unmap(staging);
        }

        AllocatedBuffer gpu = _memoryManager->createDeviceLocalBuffer(
            bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sharingMode);
        fillFromAllocated(bufferInfo, gpu);
        bufferInfo.persistent = false;

        VkBufferManager::bufferCopy(*_vkDevice,
                                    commandPool,
                                    graphicsQueue,
                                    staging.buffer,
                                    bufferInfo.buffer,
                                    VK_NULL_HANDLE,
                                    bufferSize);

        _memoryManager->destroyBuffer(staging);

        INK_DEBUG << "Created device-local index buffer: " << name
                  << ", indices: " << bufferInfo.indexCount;
    }

    _indexBuffers[name] = bufferInfo;
}

void VkIndexBufferManager::updateIndexBuffer(const std::string& name, std::vector<u16>&& indices)
{
    auto it = _indexBuffers.find(name);
    if (it == _indexBuffers.end()) {
        INK_ERROR << "Failed to update buffer - buffer doesn't exist " << name;
        return;
    }

    IndexBufferInfo& bufferInfo = it->second;

    if (bufferInfo.persistent && bufferInfo.mappedPointer) {
        std::ranges::copy(indices,
                          std::span{static_cast<u16*>(bufferInfo.mappedPointer), indices.size()});
        bufferInfo.indexCount = static_cast<u32>(indices.size());
        return;
    }

    INK_WARN << "updateIndexBuffer: non-persistent buffer cannot be updated in place: " << name;
}

IndexBufferInfo VkIndexBufferManager::getIndexBuffer(const std::string& name)
{
    auto it = _indexBuffers.find(name);
    if (it != _indexBuffers.end()) {
        return it->second;
    }

    INK_ERROR << "Invalid index buffer name: " << name;
    throw AuraException("Invalid IndexBuffer Name");
}

void VkIndexBufferManager::cleanup(const std::string& name)
{
    auto it = _indexBuffers.find(name);
    if (it == _indexBuffers.end()) {
        return;
    }

    destroyBufferInfo(_memoryManager, it->second);
    _indexBuffers.erase(it);
    INK_DEBUG << "Cleaned up index buffer: " << name;
}

void VkIndexBufferManager::cleanup()
{
    for (const auto& pair : _indexBuffers) {
        IndexBufferInfo info = pair.second;
        destroyBufferInfo(_memoryManager, info);
    }
    _indexBuffers.clear();
    INK_INFO << "Cleaned up all index buffers";
}

} // namespace vk
} // namespace aura3d
