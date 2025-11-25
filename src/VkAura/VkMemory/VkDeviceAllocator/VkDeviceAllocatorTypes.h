#ifndef VK_DEVICE_ALLOCATOR_TYPES_H
#define VK_DEVICE_ALLOCATOR_TYPES_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>

#include "aura.hpp"
#include "BuddyAllocator.h"

namespace aura3d {
namespace vk {

// Thread safety options for the allocator
enum class ThreadSafetyMode {
    NONE,                   // No thread safety, fastest performance
    COARSE_GRAINED,         // Single global mutex, simple but potential contention
    FINE_GRAINED            // Per-pool locking, better performance in multi-threaded scenarios
};

// Allocation strategy options
enum class AllocationStrategy {
    FIRST_FIT,              // First suitable free block (fastest allocation, may waste space)
    BEST_FIT,               // Smallest suitable free block (less waste, slower allocation)
    WORST_FIT               // Largest suitable free block (minimizes fragmentation for large allocations)
};

// Enum to track allocation mapping state
enum class AllocationMappingState {
    UNMAPPED,
    MAPPED,
    PERSISTENTLY_MAPPED
};

// Represents an allocation from the device allocator
struct VkDeviceAllocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;    // The Vulkan memory object
    VkDeviceSize offset = 0;                   // Offset within the memory object
    VkDeviceSize size = 0;                     // Size of the allocation
    u32 memoryTypeIndex = 0;              // Memory type index
    void* mappedData = nullptr;                // Pointer to mapped memory (nullptr if not mapped)
    u32 allocationId = 0;                 // Unique ID for this allocation
    AllocationMappingState mappingState = AllocationMappingState::UNMAPPED; // Current mapping state

    // Reset the allocation to default state
    void reset() {
        memory = VK_NULL_HANDLE;
        offset = 0;
        size = 0;
        memoryTypeIndex = 0;
        mappedData = nullptr;
        // Don't reset allocationId to keep it unique
        mappingState = AllocationMappingState::UNMAPPED;
    }
};

// Configuration for the device allocator
struct VkDeviceAllocatorCreateInfo {
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDeviceSize blockSize = 64 * 1024 * 1024;  // 64MB default block size
    VkDeviceSize smallBlockSize = 4 * 1024 * 1024; // 4MB for small allocations
    bool enableDefragmentation = false;         // Defragmentation support
    bool enableHostMapping = true;              // Support for memory mapping
    bool trackLeaks = true;                     // Track memory leaks in debug mode
    ThreadSafetyMode threadSafetyMode = ThreadSafetyMode::COARSE_GRAINED;  // Thread safety option
    AllocationStrategy strategy = AllocationStrategy::BEST_FIT;  // Default allocation strategy
    u32 dedicatedAllocationThreshold = 32 * 1024 * 1024;    // 32MB threshold for dedicated allocation
    bool useBuddyAllocatorForBuffers = true;    // Use buddy allocator for buffer memory
    bool deferFrees = true;                     // Defer free operations to batch them
    u32 deferredFreeLimit = 128;           // Maximum number of deferred frees before processing
};

// Memory usage statistics
struct MemoryStats {
    VkDeviceSize totalSize;
    VkDeviceSize usedSize;
    u32 allocationCount;
    u32 blockCount;
    std::vector<std::pair<u32, VkDeviceSize>> sizeByMemoryType;
    f32 fragmentationIndex;  // 0.0 (no fragmentation) to 1.0 (fully fragmented)
    VkDeviceSize largestFreeBlock;
    u32 totalFreeChunks;
};

// Internal types below - not part of the public API

// Represents a free memory range within a block
struct MemoryRange {
    VkDeviceSize offset;
    VkDeviceSize size;

    MemoryRange(VkDeviceSize offset = 0, VkDeviceSize size = 0)
        : offset(offset), size(size) {}

    // Comparison operators for ordered containers
    bool operator<(const MemoryRange& other) const {
        // Primary sort by size for best-fit algorithm
        if (size != other.size) {
            return size < other.size;
        }
        // Secondary sort by offset for deterministic behavior
        return offset < other.offset;
    }

    bool operator==(const MemoryRange& other) const {
        return offset == other.offset && size == other.size;
    }
};

// Size-based free memory index for fast allocation
struct SizeRangeIndex {
    // Ranges sorted by size (for best-fit allocation)
    std::multimap<VkDeviceSize, VkDeviceSize> sizeToOffset;

    // Ranges sorted by offset (for coalescing and address-ordered operations)
    std::map<VkDeviceSize, VkDeviceSize> offsetToSize;

    // Insert a free range
    void insert(VkDeviceSize offset, VkDeviceSize size) {
        sizeToOffset.emplace(size, offset);
        offsetToSize.emplace(offset, size);
    }

    // Remove a range by offset
    void remove(VkDeviceSize offset) {
        auto it = offsetToSize.find(offset);
        if (it != offsetToSize.end()) {
            VkDeviceSize size = it->second;
            // Remove from size index
            auto sizeRange = sizeToOffset.equal_range(size);
            for (auto sit = sizeRange.first; sit != sizeRange.second; ++sit) {
                if (sit->second == offset) {
                    sizeToOffset.erase(sit);
                    break;
                }
            }
            // Remove from offset index
            offsetToSize.erase(it);
        }
    }

    // Find best fit (smallest size that fits the request)
    bool findBestFit(VkDeviceSize size, VkDeviceSize& outOffset, VkDeviceSize& outSize) {
        auto it = sizeToOffset.lower_bound(size);
        if (it != sizeToOffset.end()) {
            outSize = it->first;
            outOffset = it->second;
            return true;
        }
        return false;
    }

    // Find first fit (any size that fits the request)
    bool findFirstFit(VkDeviceSize size, VkDeviceSize& outOffset, VkDeviceSize& outSize) {
        auto it = sizeToOffset.lower_bound(size);
        if (it != sizeToOffset.end()) {
            outSize = it->first;
            outOffset = it->second;
            return true;
        }
        return false;
    }

    // Find worst fit (largest free block)
    bool findWorstFit(VkDeviceSize size, VkDeviceSize& outOffset, VkDeviceSize& outSize) {
        if (sizeToOffset.empty()) {
            return false;
        }
        auto it = std::prev(sizeToOffset.end());
        if (it->first >= size) {
            outSize = it->first;
            outOffset = it->second;
            return true;
        }
        return false;
    }

    // Coalesce adjacent free blocks
    void coalesce() {
        if (offsetToSize.size() <= 1) {
            return;
        }

        std::vector<std::pair<VkDeviceSize, VkDeviceSize>> newRanges;
        auto it = offsetToSize.begin();
        VkDeviceSize currentStart = it->first;
        VkDeviceSize currentEnd = currentStart + it->second;

        ++it;
        while (it != offsetToSize.end()) {
            if (it->first == currentEnd) {
                // This range is adjacent to current one, extend it
                currentEnd = it->first + it->second;
            } else {
                // Gap between ranges, store the current range and start a new one
                newRanges.push_back({currentStart, currentEnd - currentStart});
                currentStart = it->first;
                currentEnd = currentStart + it->second;
            }
            ++it;
        }

        // Add the last range
        newRanges.push_back({currentStart, currentEnd - currentStart});

        // Clear the existing indices
        sizeToOffset.clear();
        offsetToSize.clear();

        // Add the new coalesced ranges
        for (const auto& range : newRanges) {
            insert(range.first, range.second);
        }
    }

    // Clear all entries
    void clear() {
        sizeToOffset.clear();
        offsetToSize.clear();
    }

    // Get if empty
    bool empty() const {
        return offsetToSize.empty();
    }

    // Get number of ranges
    size_t size() const {
        return offsetToSize.size();
    }
};

// Memory block within a memory type pool
struct MemoryBlock {
    VkDeviceMemory memory;
    VkDeviceSize size;
    SizeRangeIndex freeRanges;  // Efficient index of free ranges
    bool canBeMapped;
    bool isMapped;
    void* mappedAddress;
    std::unordered_map<VkDeviceSize, u32> mappedRegions;  // Track active mappings by offset->allocationId

    MemoryBlock()
        : memory(VK_NULL_HANDLE), size(0), canBeMapped(false), isMapped(false), mappedAddress(nullptr) {}
};

// Pool of memory blocks for a specific memory type
struct MemoryTypePool {
    u32 memoryTypeIndex;
    VkMemoryPropertyFlags properties;
    std::vector<MemoryBlock> blocks;
    std::unique_ptr<BuddyAllocator> buddyAllocator;  // Optional buddy allocator for this pool
    VkDeviceSize totalSize;
    VkDeviceSize usedSize;
    std::vector<u32> deferredFrees;  // IDs of allocations pending free

    MemoryTypePool()
        : memoryTypeIndex(0), properties(0), totalSize(0), usedSize(0) {}

    // Delete copy constructor and assignment
    MemoryTypePool(const MemoryTypePool&) = delete;
    MemoryTypePool& operator=(const MemoryTypePool&) = delete;

    // Add move constructor and assignment
    MemoryTypePool(MemoryTypePool&& other) noexcept
        : memoryTypeIndex(other.memoryTypeIndex),
        properties(other.properties),
        blocks(std::move(other.blocks)),
        buddyAllocator(std::move(other.buddyAllocator)),
        totalSize(other.totalSize),
        usedSize(other.usedSize),
        deferredFrees(std::move(other.deferredFrees)) {}

    MemoryTypePool& operator=(MemoryTypePool&& other) noexcept {
        if (this != &other) {
            memoryTypeIndex = other.memoryTypeIndex;
            properties = other.properties;
            blocks = std::move(other.blocks);
            buddyAllocator = std::move(other.buddyAllocator);
            totalSize = other.totalSize;
            usedSize = other.usedSize;
            deferredFrees = std::move(other.deferredFrees);
        }
        return *this;
    }
};

}
} // namespace aura3d

#endif // VK_DEVICE_ALLOCATOR_TYPES_H
