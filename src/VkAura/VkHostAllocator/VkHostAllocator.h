#ifndef VKHOSTALLOCATOR_H
#define VKHOSTALLOCATOR_H
#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <array>

namespace aura3d {

// Thread safety options for the allocator
enum class HostThreadSafetyMode {
    NONE,                   // No thread safety, fastest performance
    LOCK_FREE,              // Lock-free where possible (using atomics)
    MUTEX                   // Full mutex protection (original behavior)
};

// Options for creating the host allocator
struct VkHostAllocatorCreateInfo {
    HostThreadSafetyMode threadSafetyMode = HostThreadSafetyMode::MUTEX;
    bool enableMemoryPools = true;         // Use memory pools for small allocations
    bool trackLeaks = true;                // Track memory leaks
    bool enableBatchProcessing = true;     // Enable batch processing for deallocations
    size_t smallAllocationThreshold = 1024; // Allocations smaller than this use memory pools
    size_t batchDeallocLimit = 128;        // Process batch when this many deallocations are queued
};

// Forward declaration
class MemoryPool;

/**
 * @brief High-performance host allocator for Vulkan
 */
class VkHostAllocator {
public:
    // Constructor with configuration options
    explicit VkHostAllocator(const VkHostAllocatorCreateInfo& createInfo = VkHostAllocatorCreateInfo());

    // Destructor
    ~VkHostAllocator();

    // Get callbacks for Vulkan
    VkAllocationCallbacks* getCallbacks();

    // Memory statistics
    size_t getTotalAllocatedSize() const;
    size_t getAllocationCount() const;
    void printMemoryStats() const;

    // Process any pending deferred deallocations
    void processDeferredDeallocations(bool processAll = false);

private:
    // Configuration
    HostThreadSafetyMode threadSafetyMode;
    bool enableMemoryPools;
    bool trackLeaks;
    bool enableBatchProcessing;
    size_t smallAllocationThreshold;
    size_t batchDeallocLimit;

    // Vulkan callbacks
    VkAllocationCallbacks callbacks;

    // Memory pool for small allocations (bucket sizes: 16, 32, 64, 128, 256, 512, 1024)
    static constexpr size_t NUM_POOLS = 7;
    static constexpr size_t MIN_POOL_SIZE = 16;
    std::array<std::unique_ptr<MemoryPool>, NUM_POOLS> memoryPools;

    // Find the appropriate pool index for a given size
    size_t getSuitablePoolIndex(size_t size) const;

    // Memory tracking
    struct AllocationInfo {
        size_t size;
        VkSystemAllocationScope scope;
        bool isPooled;  // Whether this was allocated from a memory pool
        size_t poolIndex;  // Which pool it came from (if pooled)
    };

    std::unordered_map<void*, AllocationInfo> allocations;
    std::atomic<size_t> totalAllocatedSize{0};
    std::atomic<size_t> totalAllocationCount{0};

    // Queue of memory addresses pending deallocation (for batch processing)
    std::vector<void*> deferredDeallocations;

    // Thread safety
    mutable std::mutex allocationMutex;

    // Allocation callbacks
    static void* VKAPI_PTR Allocation(
        void* pUserData,
        size_t size,
        size_t alignment,
        VkSystemAllocationScope allocationScope);

    static void* VKAPI_PTR Reallocation(
        void* pUserData,
        void* pOriginal,
        size_t size,
        size_t alignment,
        VkSystemAllocationScope allocationScope);

    static void VKAPI_PTR Free(
        void* pUserData,
        void* pMemory);

    static void VKAPI_PTR InternalAllocation(
        void* pUserData,
        size_t size,
        VkInternalAllocationType allocationType,
        VkSystemAllocationScope allocationScope);

    static void VKAPI_PTR InternalFree(
        void* pUserData,
        size_t size,
        VkInternalAllocationType allocationType,
        VkSystemAllocationScope allocationScope);

    // Core allocation methods
    void* allocateMemory(size_t size, size_t alignment, VkSystemAllocationScope scope);
    void* reallocateMemory(void* original, size_t size, size_t alignment, VkSystemAllocationScope scope);
    void freeMemory(void* memory);

    // Memory pool allocation
    void* allocateFromPool(size_t size, VkSystemAllocationScope scope);
    void freeToPool(void* memory, size_t poolIndex);

    // Tracking methods
    void trackAllocation(void* memory, size_t size, VkSystemAllocationScope scope, bool isPooled = false, size_t poolIndex = 0);
    void trackDeallocation(void* memory);
    void updateAllocationOnRealloc(void* oldMemory, void* newMemory, size_t newSize, VkSystemAllocationScope scope);
    void trackInternalAllocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope);
    void trackInternalDeallocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope);

    // Helper methods
    void acquireLock() const;
    void releaseLock() const;
    bool shouldUseLock() const { return threadSafetyMode == HostThreadSafetyMode::MUTEX; }
};

// Memory pool for small allocations
class MemoryPool {
public:
    MemoryPool(size_t blockSize, size_t initialBlocks = 64);
    ~MemoryPool();

    void* allocate();
    void free(void* ptr);

    size_t getBlockSize() const { return blockSize; }
    size_t getAllocatedCount() const { return allocatedCount.load(); }
    size_t getTotalCount() const { return totalBlocks.load(); }

private:
    struct Block {
        Block* next;
    };

    const size_t blockSize;
    Block* freeList;
    std::vector<void*> blocks;
    std::atomic<size_t> allocatedCount{0};
    std::atomic<size_t> totalBlocks{0};
    std::mutex poolMutex;

    void addBlocks(size_t count);
};

}  // namespace aura3d

#endif // VKHOSTALLOCATOR_H
