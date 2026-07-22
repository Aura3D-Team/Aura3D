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
    AllocatedBuffer allocated{info.buffer, info.allocation, info.mappedPointer, info.memoryOffset};

    //! Persistent buffers get their mappedPointer from
    //! VMA_ALLOCATION_CREATE_MAPPED_BIT's automatic "0-th" mapping, never from
    //! an explicit vmaMapMemory() call, so there's no matching unmap to make
    //! here destroyBuffer() releases that mapping on its own. Calling
    //! vmaUnmapMemory() here would be an extra, unbalanced call and asserts
    //! inside VMA.
    //! docs: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#ga9bc268595cb33f6ec4d519cfce81ff45
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

template <typename IndexT>
void VkIndexBufferManager::createIndexBufferImpl(const std::string& name,
                                                 VkCommandPool commandPool,
                                                 VkSharingMode sharingMode,
                                                 VkQueue graphicsQueue,
                                                 std::vector<IndexT>&& indices,
                                                 bool persistentMapping,
                                                 VkIndexType indexType)
{
    cleanup(name);

    if (indices.empty()) {
        INK_WARN << "createIndexBuffer: empty index data for " << name;
        return;
    }

    const VkDeviceSize bufferSize = sizeof(IndexT) * indices.size();
    IndexBufferInfo bufferInfo{};
    bufferInfo.indexCount = static_cast<u32>(indices.size());
    bufferInfo.indexType = indexType;

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

        if (bufferInfo.mappedPointer)
            std::ranges::copy(indices, static_cast<IndexT*>(bufferInfo.mappedPointer));

        INK_DEBUG << "Created persistently mapped index buffer: " << name
                  << ", indices: " << bufferInfo.indexCount;
    } else {
        AllocatedBuffer staging = _memoryManager->createUploadBuffer(bufferSize, sharingMode);
        if (staging.mappedData)
        {
            std::ranges::copy(indices, static_cast<IndexT*>(staging.mappedData));
        }
        else 
        {
            auto* data = static_cast<IndexT*>(_memoryManager->map(staging));
            std::ranges::copy(indices, data);
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

void VkIndexBufferManager::createIndexBuffer(const std::string& name,
                                             VkCommandPool commandPool,
                                             VkSharingMode sharingMode,
                                             VkQueue graphicsQueue,
                                             std::vector<u16>&& indices,
                                             bool persistentMapping)
{
    createIndexBufferImpl(name, commandPool, sharingMode, graphicsQueue,
                          std::move(indices), persistentMapping, VK_INDEX_TYPE_UINT16);
}

void VkIndexBufferManager::createIndexBuffer(const std::string& name,
                                             VkCommandPool commandPool,
                                             VkSharingMode sharingMode,
                                             VkQueue graphicsQueue,
                                             std::vector<u32>&& indices,
                                             bool persistentMapping)
{
    createIndexBufferImpl(name, commandPool, sharingMode, graphicsQueue,
                          std::move(indices), persistentMapping, VK_INDEX_TYPE_UINT32);
}

template <typename IndexT>
void VkIndexBufferManager::updateIndexBufferImpl(const std::string& name, std::vector<IndexT>&& indices)
{
    auto it = _indexBuffers.find(name);
    if (it == _indexBuffers.end())
    {
        INK_ERROR << "Failed to update buffer - buffer doesn't exist " << name;
        return;
    }

    IndexBufferInfo& bufferInfo = it->second;

    constexpr VkIndexType kIncomingType = sizeof(IndexT) == sizeof(u16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

    if (bufferInfo.indexType != kIncomingType)
    {
        INK_ERROR << "updateIndexBuffer: index width mismatch for " << name
                  << " - recreate the buffer instead";
        return;
    }

    if (bufferInfo.persistent && bufferInfo.mappedPointer)
    {
        std::ranges::copy(indices, static_cast<IndexT*>(bufferInfo.mappedPointer));
        bufferInfo.indexCount = static_cast<u32>(indices.size());
        return;
    }

    INK_WARN << "updateIndexBuffer: non-persistent buffer cannot be updated in place: " << name;
}

void VkIndexBufferManager::updateIndexBuffer(const std::string& name, std::vector<u16>&& indices)
{
    updateIndexBufferImpl(name, std::move(indices));
}

void VkIndexBufferManager::updateIndexBuffer(const std::string& name, std::vector<u32>&& indices)
{
    updateIndexBufferImpl(name, std::move(indices));
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
