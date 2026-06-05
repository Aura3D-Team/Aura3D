#include "aura/Renderer/Vulkan/VkAura/VkVertexBufferManager/VkVertexBufferManager.h"

#include <ink/ink.hpp>
#include <cstring>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"

namespace aura3d {
namespace vk {

namespace {

VkMemoryPropertyFlags hostVisibleUploadFlags()
{
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

void uploadToPersistentBuffer(VkDeviceAllocator* allocator,
                              VkDeviceAllocation& allocation,
                              AuraBufferInfo& bufferInfo,
                              const void* src,
                              VkDeviceSize size)
{
    void* dst = nullptr;
    VK_RESULT_CHECK(allocator->mapMemory(allocation, 0, size, &dst));

    VkDeviceAllocation& storedAlloc = allocator->getAllocation(allocation.allocationId);
    bufferInfo.mappedPointer = storedAlloc.mappedData;
    storedAlloc.mappingState = AllocationMappingState::PERSISTENTLY_MAPPED;
    bufferInfo.persistent = true;

    if (src && size > 0) {
        std::memcpy(dst, src, static_cast<size_t>(size));
    }
}

void createVertexBufferImpl(VkHostAllocator* vkHostAllocator,
                            VkDeviceAllocator* vkDeviceAllocator,
                            VkDevice* vkDevice,
                            VkPhysicalDevice physicalDevice,
                            VkCommandPool commandPool,
                            VkSharingMode sharingMode,
                            VkQueue graphicsQueue,
                            const std::string& name,
                            VkDeviceSize bufferSize,
                            const void* src,
                            u32 vertexCount,
                            bool is2d,
                            bool persistentMapping,
                            std::unordered_map<std::string, VertexBufferInfo>& vertexBuffers)
{
    auto it = vertexBuffers.find(name);
    if (it != vertexBuffers.end()) {
        VertexBufferInfo& old = it->second;
        if (old.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(*vkDevice, old.buffer, vkHostAllocator->getCallbacks());
        }
        if (old.allocationId != 0) {
            vkDeviceAllocator->freeMemory(vkDeviceAllocator->getAllocation(old.allocationId));
        }
        vertexBuffers.erase(it);
    }

    VertexBufferInfo bufferInfo{};
    bufferInfo.vertexCount = vertexCount;
    bufferInfo.is2d = is2d;

    if (persistentMapping) {
        VkDeviceAllocation allocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      sharingMode,
                                      hostVisibleUploadFlags(),
                                      bufferInfo.buffer,
                                      allocation);

        bufferInfo.allocationId = allocation.allocationId;
        uploadToPersistentBuffer(vkDeviceAllocator, allocation, bufferInfo, src, bufferSize);

        INK_DEBUG << "Created persistently mapped vertex buffer: " << name
                  << ", vertices: " << bufferInfo.vertexCount;
    } else {
        VkBuffer stagingBuffer;
        VkDeviceAllocation stagingAllocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      sharingMode,
                                      hostVisibleUploadFlags(),
                                      stagingBuffer,
                                      stagingAllocation);

        void* stagingData = nullptr;
        VK_RESULT_CHECK(vkDeviceAllocator->mapMemory(stagingAllocation, 0, bufferSize, &stagingData));
        std::memcpy(stagingData, src, static_cast<size_t>(bufferSize));
        vkDeviceAllocator->unmapMemory(stagingAllocation);

        VkDeviceAllocation vertexAllocation;

        VkBufferManager::createBuffer(vkHostAllocator,
                                      vkDeviceAllocator,
                                      *vkDevice,
                                      physicalDevice,
                                      bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      sharingMode,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      bufferInfo.buffer,
                                      vertexAllocation);

        bufferInfo.allocationId = vertexAllocation.allocationId;
        bufferInfo.persistent = false;
        bufferInfo.mappedPointer = nullptr;

        VkBufferManager::bufferCopy(*vkDevice,
                                    commandPool,
                                    graphicsQueue,
                                    stagingBuffer,
                                    bufferInfo.buffer,
                                    VK_NULL_HANDLE,
                                    bufferSize,
                                    0,
                                    0);

        vkDestroyBuffer(*vkDevice, stagingBuffer, vkHostAllocator->getCallbacks());
        vkDeviceAllocator->freeMemory(stagingAllocation);

        INK_DEBUG << "Created device-local vertex buffer: " << name
                  << ", vertices: " << bufferInfo.vertexCount;
    }

    vertexBuffers[name] = bufferInfo;
}

} // namespace

VkVertexBufferManager::VkVertexBufferManager(VkHostAllocator* vkHostAllocator,
                                             VkDeviceAllocator* vkDeviceAllocator,
                                             VkDevice* vkDevice)
    : vkHostAllocator(vkHostAllocator), vkDeviceAllocator(vkDeviceAllocator), _vkDevice(vkDevice)
{
}

VkVertexBufferManager::~VkVertexBufferManager()
{
    cleanup();
    INK_INFO << "VkVertexBufferManager destroyed";
}

void VkVertexBufferManager::createVertexBuffer(const std::string& name,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               std::vector<gfx::Vertex2D>&& vertices2d,
                                               bool persistentMapping)
{
    if (vertices2d.empty()) {
        INK_WARN << "createVertexBuffer: empty 2D vertex data for " << name;
        return;
    }

    const VkDeviceSize bufferSize = sizeof(gfx::Vertex2D) * vertices2d.size();
    createVertexBufferImpl(vkHostAllocator,
                           vkDeviceAllocator,
                           _vkDevice,
                           physicalDevice,
                           commandPool,
                           sharingMode,
                           graphicsQueue,
                           name,
                           bufferSize,
                           vertices2d.data(),
                           static_cast<u32>(vertices2d.size()),
                           true,
                           persistentMapping,
                           _vertexBuffers);
}

void VkVertexBufferManager::createVertexBuffer(const std::string& name,
                                               VkPhysicalDevice physicalDevice,
                                               VkCommandPool commandPool,
                                               VkSharingMode sharingMode,
                                               VkQueue graphicsQueue,
                                               std::vector<gfx::Vertex3D>&& vertices3d,
                                               bool persistentMapping)
{
    if (vertices3d.empty()) {
        INK_WARN << "createVertexBuffer: empty 3D vertex data for " << name;
        return;
    }

    const VkDeviceSize bufferSize = sizeof(gfx::Vertex3D) * vertices3d.size();
    createVertexBufferImpl(vkHostAllocator,
                           vkDeviceAllocator,
                           _vkDevice,
                           physicalDevice,
                           commandPool,
                           sharingMode,
                           graphicsQueue,
                           name,
                           bufferSize,
                           vertices3d.data(),
                           static_cast<u32>(vertices3d.size()),
                           false,
                           persistentMapping,
                           _vertexBuffers);
}

void VkVertexBufferManager::updateVertexBuffer(const std::string& name, std::vector<gfx::Vertex2D>&& vertices2d)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end() || !it->second.is2d) {
        INK_ERROR << "Failed to update buffer - buffer doesn't exist or is not 2D: " << name;
        return;
    }

    VertexBufferInfo& bufferInfo = it->second;
    const VkDeviceSize bufferSize = sizeof(gfx::Vertex2D) * vertices2d.size();

    VkDeviceAllocation& allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
    if (allocation.allocationId == 0) {
        INK_ERROR << "Failed to retrieve allocation for buffer: " << name;
        return;
    }

    if (bufferInfo.persistent && allocation.mappedData) {
        std::memcpy(allocation.mappedData, vertices2d.data(), static_cast<size_t>(bufferSize));
    } else {
        void* data = nullptr;
        if (vkDeviceAllocator->mapMemory(allocation, 0, bufferSize, &data) == VK_SUCCESS) {
            std::memcpy(data, vertices2d.data(), static_cast<size_t>(bufferSize));
            vkDeviceAllocator->unmapMemory(allocation);
        }
    }

    bufferInfo.vertexCount = vertices2d.size();
}

void VkVertexBufferManager::updateVertexBuffer(const std::string& name, std::vector<gfx::Vertex3D>&& vertices3d)
{
    auto it = _vertexBuffers.find(name);
    if (it == _vertexBuffers.end() || it->second.is2d) {
        INK_ERROR << "Failed to update buffer - buffer doesn't exist or is not 3D: " << name;
        return;
    }

    VertexBufferInfo& bufferInfo = it->second;
    const VkDeviceSize bufferSize = sizeof(gfx::Vertex3D) * vertices3d.size();

    VkDeviceAllocation& allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
    if (allocation.allocationId == 0) {
        INK_ERROR << "Failed to retrieve allocation for buffer: " << name;
        return;
    }

    if (bufferInfo.persistent && allocation.mappedData) {
        std::memcpy(allocation.mappedData, vertices3d.data(), static_cast<size_t>(bufferSize));
    } else {
        void* data = nullptr;
        if (vkDeviceAllocator->mapMemory(allocation, 0, bufferSize, &data) == VK_SUCCESS) {
            std::memcpy(data, vertices3d.data(), static_cast<size_t>(bufferSize));
            vkDeviceAllocator->unmapMemory(allocation);
        }
    }

    bufferInfo.vertexCount = vertices3d.size();
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

    VertexBufferInfo& bufferInfo = it->second;

    if (bufferInfo.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(*_vkDevice, bufferInfo.buffer, vkHostAllocator->getCallbacks());
        bufferInfo.buffer = VK_NULL_HANDLE;
    }

    if (bufferInfo.allocationId != 0) {
        auto allocation = vkDeviceAllocator->getAllocation(bufferInfo.allocationId);
        vkDeviceAllocator->freeMemory(allocation);
    }

    _vertexBuffers.erase(it);

    INK_DEBUG << "Cleaned up vertex buffer: " << name;
}

void VkVertexBufferManager::cleanup()
{
    std::vector<std::string> bufferNames;
    for (const auto& pair : _vertexBuffers) {
        bufferNames.push_back(pair.first);
    }

    for (const auto& name : bufferNames) {
        cleanup(name);
    }

    _vertexBuffers.clear();

    INK_INFO << "Cleaned up all vertex buffers";
}

VkVertexInputBindingDescription VkVertexBufferManager::getBindingDescription(bool is2d)
{
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = is2d ? sizeof(gfx::Vertex2D) : sizeof(gfx::Vertex3D);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    return bindingDescription;
}

u32 VkVertexBufferManager::getAttributeDescriptionCount(bool is2d)
{
    return is2d ? MAX_ATTRIBUTE_DESCRIPTION_2D : MAX_ATTRIBUTE_DESCRIPTION_3D;
}

AttributeDescriptionArray<VkVertexInputAttributeDescription> VkVertexBufferManager::getAttributeDescriptions(bool is2d)
{
    AttributeDescriptionArray<VkVertexInputAttributeDescription> attributeDescriptions = {};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = is2d ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_R32G32B32_SFLOAT;

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;

    if (is2d) {
        attributeDescriptions[0].offset = offsetof(gfx::Vertex2D, pos);
        attributeDescriptions[1].offset = offsetof(gfx::Vertex2D, texCoord);
        attributeDescriptions[2].offset = offsetof(gfx::Vertex2D, color);
    } else {
        attributeDescriptions[0].offset = offsetof(gfx::Vertex3D, pos);
        attributeDescriptions[1].offset = offsetof(gfx::Vertex3D, texCoord);
        attributeDescriptions[2].offset = offsetof(gfx::Vertex3D, color);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(gfx::Vertex3D, normal);
    }

    return attributeDescriptions;
}

} // namespace vk
} // namespace aura3d
