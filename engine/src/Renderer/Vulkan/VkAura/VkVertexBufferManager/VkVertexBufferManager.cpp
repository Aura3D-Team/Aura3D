#include "aura/Renderer/Vulkan/VkAura/VkVertexBufferManager/VkVertexBufferManager.h"

#include <algorithm>
#include <ranges>
#include <span>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"

namespace aura3d {
namespace vk {

namespace {

void destroyBufferInfo(VulkanMemoryManager* memory, VertexBufferInfo& info)
{
    if (info.persistent && info.mappedPointer && info.allocation != VK_NULL_HANDLE) {
        memory->unmap({info.buffer, info.allocation, info.mappedPointer, info.memoryOffset});
    }

    AllocatedBuffer allocated{info.buffer, info.allocation, info.mappedPointer, info.memoryOffset};
    memory->destroyBuffer(allocated);
    info = {};
}

void fillFromAllocated(VertexBufferInfo& info, const AllocatedBuffer& allocated)
{
    info.buffer = allocated.buffer;
    info.allocation = allocated.allocation;
    info.memoryOffset = allocated.offset;
    info.mappedPointer = allocated.mappedData;
}

} // namespace

VkVertexBufferManager::VkVertexBufferManager(VulkanMemoryManager* memoryManager, VkDevice* vkDevice)
    : _memoryManager(memoryManager), _vkDevice(vkDevice)
{
}

VkVertexBufferManager::~VkVertexBufferManager()
{
    cleanup();
}

void VkVertexBufferManager::createVertexBuffer(const std::string& name,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               std::vector<gfx::Vertex3D>&& vertices3d,
                                               bool persistentMapping)
{
    cleanup(name);

    if (vertices3d.empty()) {
        INK_WARN << "createVertexBuffer: empty 3D vertex data for " << name;
        return;
    }

    const VkDeviceSize bufferSize = sizeof(gfx::Vertex3D) * vertices3d.size();
    VertexBufferInfo bufferInfo{};
    bufferInfo.vertexCount = vertices3d.size();

    if (persistentMapping) {
        VmaAllocationCreateFlags flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;

        AllocatedBuffer allocated = _memoryManager->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            sharingMode,
            VMA_MEMORY_USAGE_AUTO,
            flags);

        fillFromAllocated(bufferInfo, allocated);
        bufferInfo.persistent = true;

        if (bufferInfo.mappedPointer) {
            std::ranges::copy(vertices3d,
                              std::span{static_cast<gfx::Vertex3D*>(bufferInfo.mappedPointer), vertices3d.size()});
        }

        INK_DEBUG << "Created persistently mapped vertex buffer: " << name
                  << ", vertices: " << bufferInfo.vertexCount;
    } else {
        AllocatedBuffer staging = _memoryManager->createUploadBuffer(bufferSize, sharingMode);
        if (staging.mappedData) {
            std::ranges::copy(vertices3d,
                              std::span{static_cast<gfx::Vertex3D*>(staging.mappedData), vertices3d.size()});
        } else {
            auto* data = static_cast<gfx::Vertex3D*>(_memoryManager->map(staging));
            std::ranges::copy(vertices3d, std::span{data, vertices3d.size()});
            _memoryManager->unmap(staging);
        }

        AllocatedBuffer gpu = _memoryManager->createDeviceLocalBuffer(
            bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sharingMode);
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

        INK_DEBUG << "Created device-local vertex buffer: " << name
                  << ", vertices: " << bufferInfo.vertexCount;
    }

    _vertexBuffers[name] = bufferInfo;
}

void VkVertexBufferManager::updateVertexBuffer(const std::string& name, std::vector<gfx::Vertex3D>&& vertices3d)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end()) {
        INK_ERROR << "Failed to update vertex buffer - not found: " << name;
        return;
    }

    VertexBufferInfo& bufferInfo = it->second;

    if (bufferInfo.persistent && bufferInfo.mappedPointer) {
        std::ranges::copy(vertices3d,
                          std::span{static_cast<gfx::Vertex3D*>(bufferInfo.mappedPointer), vertices3d.size()});
        bufferInfo.vertexCount = vertices3d.size();
        return;
    }

    INK_WARN << "updateVertexBuffer: non-persistent buffer cannot be updated in place: " << name;
}

VertexBufferInfo VkVertexBufferManager::getVertexBuffer(const std::string& name)
{
    auto it = _vertexBuffers.find(name);
    if (it != _vertexBuffers.end()) {
        return it->second;
    }

    INK_ERROR << "Invalid vertex buffer name: " << name;
    throw AuraException("Invalid VertexBuffer Name");
}

size_t VkVertexBufferManager::getVertexCount(const std::string& name)
{
    auto it = _vertexBuffers.find(name);
    if (it != _vertexBuffers.end()) {
        return it->second.vertexCount;
    }
    return 0;
}

void VkVertexBufferManager::cleanup(const std::string& name)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end()) {
        return;
    }

    destroyBufferInfo(_memoryManager, it->second);
    _vertexBuffers.erase(it);
    INK_DEBUG << "Cleaned up vertex buffer: " << name;
}

void VkVertexBufferManager::cleanup()
{
    for (const auto& pair : _vertexBuffers) {
        VertexBufferInfo info = pair.second;
        destroyBufferInfo(_memoryManager, info);
    }
    _vertexBuffers.clear();
    INK_INFO << "Cleaned up all vertex buffers";
}

VkVertexInputBindingDescription VkVertexBufferManager::getBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(gfx::Vertex3D);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

u32 VkVertexBufferManager::getAttributeDescriptionCount()
{
    return MAX_ATTRIBUTE_DESCRIPTION_3D;
}

AttributeDescriptionArray<VkVertexInputAttributeDescription> VkVertexBufferManager::getAttributeDescriptions()
{
    AttributeDescriptionArray<VkVertexInputAttributeDescription> attributeDescriptions = {};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(gfx::Vertex3D, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(gfx::Vertex3D, texCoord);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(gfx::Vertex3D, color);

    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(gfx::Vertex3D, normal);

    return attributeDescriptions;
}

} // namespace vk
} // namespace aura3d
