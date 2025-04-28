#ifndef VKHOSTALLOCATOR_H
#define VKHOSTALLOCATOR_H

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <memory>
#include <array>

#include "MemoryPool.h"

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

/**
 * @brief High-performance host allocator for Vulkan
 */
class VkHostAllocator {
public:
    /**
     * @brief Constructs a VkHostAllocator with the specified configuration
     *
     * @param createInfo Configuration parameters for the allocator
     */
    explicit VkHostAllocator(const VkHostAllocatorCreateInfo& createInfo = VkHostAllocatorCreateInfo());

    /**
     * @brief Destroys the VkHostAllocator and cleans up all memory
     */
    ~VkHostAllocator();

    /**
     * @brief Gets the Vulkan allocation callbacks structure
     *
     * @return Pointer to the VkAllocationCallbacks structure
     */
    VkAllocationCallbacks* getCallbacks();

    /**
     * @brief Gets the total size of all currently allocated memory
     *
     * @return Total allocated size in bytes
     */
    size_t getTotalAllocatedSize() const;

    /**
     * @brief Gets the number of currently active allocations
     *
     * @return Number of active allocations
     */
    size_t getAllocationCount() const;

    /**
     * @brief Prints detailed statistics about memory usage
     */
    void printMemoryStats() const;

    /**
     * @brief Processes deferred deallocations in batch
     *
     * @param processAll Whether to process all deferred deallocations or just enough to meet the batch limit
     */
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

    /**
     * @brief Determines the appropriate memory pool index for a given allocation size
     *
     * @param size Size of the allocation in bytes
     * @return Index of the suitable memory pool
     */
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

    /**
     * @brief Vulkan allocation callback
     *
     * @param pUserData User data pointer (VkHostAllocator instance)
     * @param size Size of the allocation in bytes
     * @param alignment Required memory alignment
     * @param allocationScope Vulkan allocation scope
     * @return Allocated memory pointer or nullptr if allocation failed
     */
    static void* VKAPI_PTR Allocation(
        void* pUserData,
        size_t size,
        size_t alignment,
        VkSystemAllocationScope allocationScope);

    /**
     * @brief Vulkan reallocation callback
     *
     * @param pUserData User data pointer (VkHostAllocator instance)
     * @param pOriginal Original memory pointer
     * @param size New size of the allocation in bytes
     * @param alignment Required memory alignment
     * @param allocationScope Vulkan allocation scope
     * @return Reallocated memory pointer or nullptr if reallocation failed
     */
    static void* VKAPI_PTR Reallocation(
        void* pUserData,
        void* pOriginal,
        size_t size,
        size_t alignment,
        VkSystemAllocationScope allocationScope);

    /**
     * @brief Vulkan free callback
     *
     * @param pUserData User data pointer (VkHostAllocator instance)
     * @param pMemory Memory pointer to free
     */
    static void VKAPI_PTR Free(
        void* pUserData,
        void* pMemory);

    /**
     * @brief Vulkan internal allocation notification callback
     *
     * @param pUserData User data pointer (VkHostAllocator instance)
     * @param size Size of the allocation in bytes
     * @param allocationType Type of internal allocation
     * @param allocationScope Vulkan allocation scope
     */
    static void VKAPI_PTR InternalAllocation(
        void* pUserData,
        size_t size,
        VkInternalAllocationType allocationType,
        VkSystemAllocationScope allocationScope);

    /**
     * @brief Vulkan internal free notification callback
     *
     * @param pUserData User data pointer (VkHostAllocator instance)
     * @param size Size of the allocation in bytes
     * @param allocationType Type of internal allocation
     * @param allocationScope Vulkan allocation scope
     */
    static void VKAPI_PTR InternalFree(
        void* pUserData,
        size_t size,
        VkInternalAllocationType allocationType,
        VkSystemAllocationScope allocationScope);

    /**
     * @brief Allocates memory according to Vulkan requirements
     *
     * Uses memory pools for small allocations if enabled.
     *
     * @param size Size of the allocation in bytes
     * @param alignment Required memory alignment
     * @param scope Vulkan allocation scope
     * @return Allocated memory pointer or nullptr if allocation failed
     */
    void* allocateMemory(size_t size, size_t alignment, VkSystemAllocationScope scope);

    /**
     * @brief Reallocates memory according to Vulkan requirements
     *
     * @param original Original memory pointer
     * @param size New size of the allocation in bytes
     * @param alignment Required memory alignment
     * @param scope Vulkan allocation scope
     * @return Reallocated memory pointer or nullptr if reallocation failed
     */
    void* reallocateMemory(void* original, size_t size, size_t alignment, VkSystemAllocationScope scope);

    /**
     * @brief Frees memory allocated by the allocator
     *
     * @param memory Memory pointer to free
     */
    void freeMemory(void* memory);

    /**
     * @brief Allocates memory from an appropriate memory pool
     *
     * @param size Size of the allocation in bytes
     * @param scope Vulkan allocation scope
     * @return Allocated memory pointer or nullptr if allocation failed
     */
    void* allocateFromPool(size_t size, VkSystemAllocationScope scope);

    /**
     * @brief Returns memory to the appropriate memory pool
     *
     * @param memory Memory pointer to return to pool
     * @param poolIndex Index of the memory pool
     */
    void freeToPool(void* memory, size_t poolIndex);

    /**
     * @brief Tracks a new memory allocation
     *
     * @param memory Memory pointer
     * @param size Size of the allocation in bytes
     * @param scope Vulkan allocation scope
     * @param isPooled Whether the allocation is from a memory pool
     * @param poolIndex Index of the memory pool if pooled
     */
    void trackAllocation(void* memory, size_t size, VkSystemAllocationScope scope, bool isPooled = false, size_t poolIndex = 0);

    /**
     * @brief Tracks memory deallocation
     *
     * @param memory Memory pointer being deallocated
     */
    void trackDeallocation(void* memory);

    /**
     * @brief Updates allocation tracking information after reallocation
     *
     * @param oldMemory Original memory pointer
     * @param newMemory New memory pointer after reallocation
     * @param newSize New size of the allocation
     * @param scope Vulkan allocation scope
     */
    void updateAllocationOnRealloc(void* oldMemory, void* newMemory, size_t newSize, VkSystemAllocationScope scope);

    /**
     * @brief Tracks Vulkan internal allocations
     *
     * @param size Size of the allocation in bytes
     * @param type Type of internal allocation
     * @param scope Vulkan allocation scope
     */
    void trackInternalAllocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope);

    /**
     * @brief Tracks Vulkan internal deallocations
     *
     * @param size Size of the deallocation in bytes
     * @param type Type of internal allocation
     * @param scope Vulkan allocation scope
     */
    void trackInternalDeallocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope);

    /**
     * @brief Acquires the allocation mutex if thread safety is enabled
     */
    void acquireLock() const;

    /**
     * @brief Releases the allocation mutex if thread safety is enabled
     */
    void releaseLock() const;

    bool shouldUseLock() const { return threadSafetyMode == HostThreadSafetyMode::MUTEX; }
};

}  // namespace aura3d

#endif // VKHOSTALLOCATOR_H
