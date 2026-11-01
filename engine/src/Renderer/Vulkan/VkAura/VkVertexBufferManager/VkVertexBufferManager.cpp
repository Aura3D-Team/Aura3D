#include "aura/Renderer/Vulkan/VkAura/VkVertexBufferManager/VkVertexBufferManager.h"

#include <span>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"

namespace aura3d
{
namespace vk
{

namespace
{

void destroyBufferInfo(VulkanMemoryManager *memory, VertexBufferInfo &info)
{
    AllocatedBuffer allocated{info.buffer, info.allocation, info.mappedPointer, info.memoryOffset};

    memory->destroyBuffer(allocated);
    info = {};
}

void fillFromAllocated(VertexBufferInfo &info, const AllocatedBuffer &allocated)
{
    info.buffer = allocated.buffer;
    info.allocation = allocated.allocation;
    info.memoryOffset = allocated.offset;
    info.mappedPointer = allocated.mappedData;
}

} // namespace

VkVertexBufferManager::VkVertexBufferManager(VulkanMemoryManager *memoryManager, VkDevice *vkDevice)
    : _memoryManager(memoryManager), _vkDevice(vkDevice)
{
}

VkVertexBufferManager::~VkVertexBufferManager()
{
    cleanup();
}

void VkVertexBufferManager::createVertexBuffer(const std::string &name, VkCommandPool commandPool,
                                               VkSharingMode sharingMode, VkQueue graphicsQueue,
                                               std::vector<gfx::Vertex3D> &&vertices3d)
{
    cleanup(name);

    if (vertices3d.empty())
    {
        INK_WARN << "createVertexBuffer: empty 3D vertex data for " << name;
        return;
    }

    VertexBufferInfo bufferInfo{};
    bufferInfo.vertexCount = vertices3d.size();

    fillFromAllocated(bufferInfo, VkBufferManager::uploadDeviceLocal(
                                      *_memoryManager, *_vkDevice, commandPool, graphicsQueue, sharingMode,
                                      std::as_bytes(std::span(vertices3d)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));

    INK_TRACE << "Created device-local vertex buffer: " << name << ", vertices: " << bufferInfo.vertexCount;

    _vertexBuffers[name] = bufferInfo;
}

VertexBufferInfo VkVertexBufferManager::getVertexBuffer(const std::string &name)
{
    auto it = _vertexBuffers.find(name);
    if (it != _vertexBuffers.end())
        return it->second;

    INK_ERROR << "Invalid vertex buffer name: " << name;
    throw AuraException("Invalid VertexBuffer Name");
}

void VkVertexBufferManager::cleanup(const std::string &name)
{
    auto it = _vertexBuffers.find(name);

    if (it == _vertexBuffers.end())
        return;

    destroyBufferInfo(_memoryManager, it->second);
    _vertexBuffers.erase(it);

    INK_TRACE << "Cleaned up vertex buffer: " << name;
}

void VkVertexBufferManager::cleanup()
{
    for (const auto &pair : _vertexBuffers)
    {
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
