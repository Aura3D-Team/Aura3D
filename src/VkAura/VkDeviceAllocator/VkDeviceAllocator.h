#ifndef VKDEVICEALLOCATOR_H
#define VKDEVICEALLOCATOR_H

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>

namespace aura3d {

// Represents an allocation from the device allocator
struct VkDeviceAllocation {
    VkDeviceMemory memory;      // The Vulkan memory object
    VkDeviceSize offset;        // Offset within the memory object
    VkDeviceSize size;          // Size of the allocation
    uint32_t memoryTypeIndex;   // Memory type index
    void* mappedData;           // Pointer to mapped memory (nullptr if not mapped)
    uint32_t allocationId;      // Unique ID for this allocation
};

// Configuration for the device allocator
struct VkDeviceAllocatorCreateInfo {
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDeviceSize blockSize = 64 * 1024 * 1024;  // 64MB default block size
    bool enableDefragmentation = false;         // Defragmentation support
    bool enableHostMapping = true;              // Support for memory mapping
};

struct MemoryChunk {
    VkDeviceSize offset;
    VkDeviceSize size;
    // Constructor for convenience
    MemoryChunk(VkDeviceSize offset = 0, VkDeviceSize size = 0)
        : offset(offset), size(size) {}
    // Comparison operators for sorting chunks by offset
    bool operator<(const MemoryChunk& other) const {
        return offset < other.offset;
    }
};

// Memory block within a memory type pool
struct MemoryBlock {
    VkDeviceMemory memory;
    VkDeviceSize size;
    std::vector<MemoryChunk> freeList;  // List of free ranges
    bool canBeMapped;
    void* mappedAddress;
};

// Pool of memory blocks for a specific memory type
struct MemoryTypePool {
    uint32_t memoryTypeIndex;
    VkMemoryPropertyFlags properties;
    std::vector<MemoryBlock> blocks;
    VkDeviceSize totalSize;
    VkDeviceSize usedSize;
};

class VkDeviceAllocator {
public:
    // Delete copy and move constructors/assignments
    VkDeviceAllocator(const VkDeviceAllocator&) = delete;
    VkDeviceAllocator& operator=(const VkDeviceAllocator&) = delete;
    VkDeviceAllocator(VkDeviceAllocator&&) = delete;
    VkDeviceAllocator& operator=(VkDeviceAllocator&&) = delete;

    // Initialize the singleton with creation info
    static void initialize(const VkDeviceAllocatorCreateInfo& createInfo);

    // Get the singleton instance
    static VkDeviceAllocator& getInstance();

    // Destroy the singleton instance
    static void destroy();

    // Destructor
    ~VkDeviceAllocator();

    // Allocation methods
    VkResult allocateMemory(const VkMemoryRequirements& memRequirements,
                            VkMemoryPropertyFlags properties,
                            VkDeviceAllocation& allocation);

    VkResult allocateMemoryForBuffer(VkBuffer buffer,
                                     VkMemoryPropertyFlags properties,
                                     VkDeviceAllocation& allocation);

    VkResult allocateMemoryForImage(VkImage image,
                                    VkMemoryPropertyFlags properties,
                                    VkDeviceAllocation& allocation);

    // Free memory
    void freeMemory(VkDeviceAllocation& allocation);

    // Memory mapping
    VkResult mapMemory(VkDeviceAllocation& allocation, VkDeviceSize offset, VkDeviceSize size, void** ppData);
    void unmapMemory(VkDeviceAllocation& allocation);

    // Bind resources to allocations
    VkResult bindBufferMemory(VkBuffer buffer, const VkDeviceAllocation& allocation, VkDeviceSize offsetInAllocation = 0);
    VkResult bindImageMemory(VkImage image, const VkDeviceAllocation& allocation, VkDeviceSize offsetInAllocation = 0);

    // Memory statistics
    struct MemoryStats {
        VkDeviceSize totalSize;
        VkDeviceSize usedSize;
        uint32_t allocationCount;
        uint32_t blockCount;
        std::vector<std::pair<uint32_t, VkDeviceSize>> sizeByMemoryType;
    };

    MemoryStats getMemoryStats() const;
    void printMemoryStats() const;

    // Get devices
    VkDevice getDevice() const { return device; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }

private:
    // Private constructor - use initialize() instead
    VkDeviceAllocator(const VkDeviceAllocatorCreateInfo& createInfo);

    // Static singleton instance
    static std::unique_ptr<VkDeviceAllocator> instance;
    static std::once_flag initInstanceFlag;

    // Memory type helpers
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Allocation helpers
    VkResult allocateNewBlock(uint32_t memoryTypeIndex, VkDeviceSize blockSize);
    VkResult findAndAllocateInBlock(uint32_t memoryTypeIndex,
                                    VkDeviceSize size,
                                    VkDeviceSize alignment,
                                    VkDeviceAllocation& allocation);

    // Memory type pools
    MemoryTypePool& getOrCreateMemoryTypePool(uint32_t memoryTypeIndex);

    // Member variables
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDeviceSize defaultBlockSize;
    bool defragmentationEnabled;

    // Memory pools by memory type
    std::vector<MemoryTypePool> memoryTypePools;

    // Physical device memory properties
    VkPhysicalDeviceMemoryProperties memoryProperties;

    // Tracking info
    uint32_t nextAllocationId;
    std::unordered_map<uint32_t, VkDeviceAllocation> allocationMap;

    // Thread safety
    mutable std::mutex allocationMutex;
};

} // namespace aura3d

#endif // VKDEVICEALLOCATOR_H
