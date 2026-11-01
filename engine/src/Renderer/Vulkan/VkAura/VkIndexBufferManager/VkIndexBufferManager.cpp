#include "aura/Renderer/Vulkan/VkAura/VkIndexBufferManager/VkIndexBufferManager.h"

#include <span>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"

namespace aura3d
{
namespace vk
{

namespace
{

void destroyBufferInfo(VulkanMemoryManager *memory, IndexBufferInfo &info)
{
    AllocatedBuffer allocated{info.buffer, info.allocation, info.mappedPointer, info.memoryOffset};

    memory->destroyBuffer(allocated);
    info = {};
}

void fillFromAllocated(IndexBufferInfo &info, const AllocatedBuffer &allocated)
{
    info.buffer = allocated.buffer;
    info.allocation = allocated.allocation;
    info.memoryOffset = allocated.offset;
    info.mappedPointer = allocated.mappedData;
}

} // namespace

VkIndexBufferManager::VkIndexBufferManager(VulkanMemoryManager *memoryManager, VkDevice *vkDevice)
    : _memoryManager(memoryManager), _vkDevice(vkDevice)
{
}

VkIndexBufferManager::~VkIndexBufferManager()
{
    cleanup();
}

template <typename IndexT>
void VkIndexBufferManager::createIndexBufferImpl(const std::string &name, VkCommandPool commandPool,
                                                 VkSharingMode sharingMode, VkQueue graphicsQueue,
                                                 std::vector<IndexT> &&indices, VkIndexType indexType)
{
    cleanup(name);

    if (indices.empty())
    {
        INK_WARN << "createIndexBuffer: empty index data for " << name;
        return;
    }

    IndexBufferInfo bufferInfo{};
    bufferInfo.indexCount = static_cast<u32>(indices.size());
    bufferInfo.indexType = indexType;

    fillFromAllocated(bufferInfo, VkBufferManager::uploadDeviceLocal(
                                      *_memoryManager, *_vkDevice, commandPool, graphicsQueue, sharingMode,
                                      std::as_bytes(std::span(indices)), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));

    INK_TRACE << "Created device-local index buffer: " << name << ", indices: " << bufferInfo.indexCount;

    _indexBuffers[name] = bufferInfo;
}

void VkIndexBufferManager::createIndexBuffer(const std::string &name, VkCommandPool commandPool,
                                             VkSharingMode sharingMode, VkQueue graphicsQueue,
                                             std::vector<u16> &&indices)
{
    createIndexBufferImpl(name, commandPool, sharingMode, graphicsQueue, std::move(indices), VK_INDEX_TYPE_UINT16);
}

void VkIndexBufferManager::createIndexBuffer(const std::string &name, VkCommandPool commandPool,
                                             VkSharingMode sharingMode, VkQueue graphicsQueue,
                                             std::vector<u32> &&indices)
{
    createIndexBufferImpl(name, commandPool, sharingMode, graphicsQueue, std::move(indices), VK_INDEX_TYPE_UINT32);
}

IndexBufferInfo VkIndexBufferManager::getIndexBuffer(const std::string &name)
{
    auto it = _indexBuffers.find(name);
    if (it != _indexBuffers.end())
    {
        return it->second;
    }

    INK_ERROR << "Invalid index buffer name: " << name;
    throw AuraException("Invalid IndexBuffer Name");
}

void VkIndexBufferManager::cleanup(const std::string &name)
{
    auto it = _indexBuffers.find(name);
    if (it == _indexBuffers.end())
    {
        return;
    }

    destroyBufferInfo(_memoryManager, it->second);
    _indexBuffers.erase(it);
    INK_TRACE << "Cleaned up index buffer: " << name;
}

void VkIndexBufferManager::cleanup()
{
    for (const auto &pair : _indexBuffers)
    {
        IndexBufferInfo info = pair.second;
        destroyBufferInfo(_memoryManager, info);
    }
    _indexBuffers.clear();
    INK_INFO << "Cleaned up all index buffers";
}

} // namespace vk
} // namespace aura3d
