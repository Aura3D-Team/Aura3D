#ifndef VKBUFFERMEMORYALLOCATOR_H
#define VKBUFFERMEMORYALLOCATOR_H
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace aura3d {

// Represents a sub-allocation within a memory block
struct MemoryChunk {
    VkDeviceSize offset;      // Offset within the memory block
    VkDeviceSize size;        // Size of this chunk
    bool free;                // Whether the chunk is available
    uint32_t alignment;       // Alignment requirements
};

// Represents a large memory block from which we sub-allocate
struct MemoryBlock {
    VkDeviceMemory memory;    // Vulkan memory handle
    VkDeviceSize size;        // Total size of the block
    uint32_t memoryTypeIndex; // Memory type index of this block
    std::vector<MemoryChunk> chunks; // Sub-allocations within this block
    void* mappedData;         // Mapped memory pointer (if host-visible)
};

// Allocation result containing memory info and mapping
struct AllocationInfo {
    VkDeviceMemory memory;    // Memory handle
    VkDeviceSize offset;      // Offset within the memory
    VkDeviceSize size;        // Size of the allocation
    void* mappedData;         // Pointer to mapped memory (if applicable)
};

class VkBufferMemoryAllocator {
public:
    VkBufferMemoryAllocator(VkDevice* device, VkPhysicalDevice physicalDevice,
                            VkDeviceSize blockSize = 16 * 1024 * 1024); // 16MB by default
    ~VkBufferMemoryAllocator();

    // Allocate memory for a buffer
    AllocationInfo allocate(VkBuffer buffer, VkMemoryPropertyFlags properties);

    // Allocate memory for an image
    AllocationInfo allocateForImage(VkImage image, VkMemoryPropertyFlags properties);

    AllocationInfo getAllocationInfo(VkDeviceMemory memory, VkDeviceSize offset);

    // Find a suitable memory type
    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Free an allocation
    void free(VkDeviceMemory memory, VkDeviceSize offset);

    // Unbind a buffer from memory
    void unbindBuffer(VkBuffer buffer);

    // Unbind an image from memory
    void unbindImage(VkImage image);

    // Clean up all memory
    void cleanup();

private:
    VkDevice* _device;
    VkPhysicalDevice _physicalDevice;
    VkDeviceSize _blockSize;
    std::vector<MemoryBlock> _blocks;
    std::unordered_map<VkBuffer, AllocationInfo> _bufferAllocations;
    std::unordered_map<VkImage, AllocationInfo> _imageAllocations;
    std::mutex _allocationMutex;

    // Allocate a new memory block
    MemoryBlock* allocateBlock(uint32_t memoryTypeIndex, VkMemoryPropertyFlags properties);

    // Find suitable free chunk in existing blocks
    AllocationInfo findChunk(VkMemoryRequirements memRequirements, uint32_t memoryTypeIndex);
};

} // namespace aura3d
#endif // VKBUFFERMEMORYALLOCATOR_H
