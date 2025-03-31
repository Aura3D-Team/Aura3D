#include "VkHostAllocator.h"
#include <iostream>
#include <iomanip>
#include <ink/ink.hpp>
#include <algorithm>
#include <string.h>  // For memcpy

// Platform-specific includes for aligned memory allocation
#if defined(_WIN32)
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include "aura.hpp"

namespace aura3d {

//=========================================================================================
// Lock-free Memory Pool Implementation
//=========================================================================================

MemoryPool::MemoryPool(size_t blockSize, size_t initialBlocks)
    : blockSize(blockSize), freeList(nullptr)
{
    addBlocks(initialBlocks);
}

MemoryPool::~MemoryPool()
{
    // Free all allocated blocks
    for (void* block : blocks) {
#if defined(_WIN32)
        _aligned_free(block);
#else
        free(block);
#endif
    }
}

void MemoryPool::addBlocks(size_t count)
{
    std::lock_guard<std::mutex> lock(expansionMutex);

    // Adjust block size to ensure it can hold at least a pointer
    size_t actualBlockSize = std::max(blockSize, sizeof(Block*));

    // Allocate a new large chunk of memory
    size_t totalSize = actualBlockSize * count;
#if defined(_WIN32)
    void* memory = _aligned_malloc(totalSize, std::max(size_t(16), sizeof(void*)));
#else
    void* memory = nullptr;
    if (posix_memalign(&memory, std::max(size_t(16), sizeof(void*)), totalSize) != 0) {
        memory = nullptr;
    }
#endif
    if (!memory) {
        INK_ERROR << "Failed to allocate memory for pool of block size " << blockSize;
        return;
    }

    // Add to our blocks list
    blocks.push_back(memory);

    // Initialize free list with new blocks
    char* curr = static_cast<char*>(memory);

    // Get current head of the free list
    Block* oldHead = freeList.load(std::memory_order_relaxed);

    for (size_t i = 0; i < count; ++i) {
        Block* block = reinterpret_cast<Block*>(curr);

        if (i == count - 1) {
            // Last block points to the old head
            block->next.store(oldHead, std::memory_order_relaxed);

            // Atomically update the free list head to the first new block
            Block* firstBlock = reinterpret_cast<Block*>(static_cast<char*>(memory));
            freeList.store(firstBlock, std::memory_order_release);
        } else {
            // Each block points to the next one
            Block* nextBlock = reinterpret_cast<Block*>(curr + actualBlockSize);
            block->next.store(nextBlock, std::memory_order_relaxed);
        }

        curr += actualBlockSize;
    }

    totalBlocks.fetch_add(count, std::memory_order_relaxed);
}

void* MemoryPool::allocate()
{
    Block* oldHead = freeList.load(std::memory_order_acquire);
    Block* newHead;

    do {
        // If free list is empty, allocate more blocks
        if (!oldHead) {
            // Check again after acquiring the lock
            if (!(oldHead = freeList.load(std::memory_order_acquire))) {
                // f64 the pool size with each expansion
                size_t newBlocks = std::max(size_t(64), totalBlocks.load(std::memory_order_relaxed));
                addBlocks(newBlocks);
                oldHead = freeList.load(std::memory_order_acquire);

                // If still empty after trying to add blocks, fail
                if (!oldHead) {
                    return nullptr;
                }
            }
        }

        newHead = oldHead->next.load(std::memory_order_relaxed);
    } while (!freeList.compare_exchange_weak(oldHead, newHead,
                                             std::memory_order_release,
                                             std::memory_order_acquire));

    allocatedCount.fetch_add(1, std::memory_order_relaxed);
    return oldHead;
}

void MemoryPool::free(void* ptr)
{
    Block* block = static_cast<Block*>(ptr);
    Block* oldHead = freeList.load(std::memory_order_relaxed);

    do {
        block->next.store(oldHead, std::memory_order_relaxed);
    } while (!freeList.compare_exchange_weak(oldHead, block,
                                             std::memory_order_release,
                                             std::memory_order_acquire));

    allocatedCount.fetch_sub(1, std::memory_order_relaxed);
}

//=========================================================================================
// VkHostAllocator Implementation
//=========================================================================================

VkHostAllocator::VkHostAllocator(const VkHostAllocatorCreateInfo& createInfo)
    : threadSafetyMode(createInfo.threadSafetyMode),
    enableMemoryPools(createInfo.enableMemoryPools),
    trackLeaks(createInfo.trackLeaks),
    enableBatchProcessing(createInfo.enableBatchProcessing),
    smallAllocationThreshold(createInfo.smallAllocationThreshold),
    batchDeallocLimit(createInfo.batchDeallocLimit),
    callbacks({}),
    totalAllocatedSize(0),
    totalAllocationCount(0)
{
    callbacks.pUserData = this;
    callbacks.pfnAllocation = &VkHostAllocator::Allocation;
    callbacks.pfnReallocation = &VkHostAllocator::Reallocation;
    callbacks.pfnFree = &VkHostAllocator::Free;
    callbacks.pfnInternalAllocation = &VkHostAllocator::InternalAllocation;
    callbacks.pfnInternalFree = &VkHostAllocator::InternalFree;

    // Initialize memory pools if enabled
    if (enableMemoryPools) {
        size_t poolSize = MIN_POOL_SIZE;
        for (size_t i = 0; i < NUM_POOLS; ++i) {
            memoryPools[i] = std::make_unique<MemoryPool>(poolSize);
            poolSize *= 2;  // f64 for each pool (16, 32, 64, 128, etc.)
        }
    }

    INK_DEBUG << "VkHostAllocator created with "
              << (threadSafetyMode == HostThreadSafetyMode::NONE ? "no thread safety" :
                      (threadSafetyMode == HostThreadSafetyMode::LOCK_FREE ? "lock-free" : "mutex"))
              << ", memory pools " << (enableMemoryPools ? "enabled" : "disabled");
}

VkHostAllocator::~VkHostAllocator()
{
    // Process any pending deallocations
    if (enableBatchProcessing) {
        processDeferredDeallocations(true);
    }

    if (trackLeaks && !allocations.empty()) {
        INK_WARN << "WARNING: " << allocations.size()
        << " Vulkan allocations still active at VkHostAllocator destruction!";

        // Print details of leaked allocations
        for (const auto& [ptr, info] : allocations) {
            INK_WARN << "  Leaked: " << ptr << ", size: " << info.size
                       << ", scope: " << static_cast<int>(info.scope)
                       << (info.isPooled ? " (pooled)" : "");
        }
    }

    // Memory pools will be automatically destroyed by unique_ptr
}

size_t VkHostAllocator::getSuitablePoolIndex(size_t size) const
{
    // Find pool index for the given size
    // Pool sizes are powers of 2 starting at MIN_POOL_SIZE (16, 32, 64, 128, etc.)
    if (size <= MIN_POOL_SIZE) {
        return 0;
    }

    size_t index = 0;
    size_t poolSize = MIN_POOL_SIZE;

    while (poolSize < size && index < NUM_POOLS - 1) {
        poolSize *= 2;
        index++;
    }

    return index;
}

void VkHostAllocator::acquireLock() const
{
    if (shouldUseLock()) {
        allocationMutex.lock();
    }
}

void VkHostAllocator::releaseLock() const
{
    if (shouldUseLock()) {
        allocationMutex.unlock();
    }
}

VkAllocationCallbacks* VkHostAllocator::getCallbacks()
{
    return &callbacks;
}

// Static callback implementations

void* VKAPI_PTR VkHostAllocator::Allocation(
    void* pUserData,
    size_t size,
    size_t alignment,
    VkSystemAllocationScope allocationScope)
{
    VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);
    return allocator->allocateMemory(size, alignment, allocationScope);
}

void* VKAPI_PTR VkHostAllocator::Reallocation(
    void* pUserData,
    void* pOriginal,
    size_t size,
    size_t alignment,
    VkSystemAllocationScope allocationScope)
{
    VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);
    return allocator->reallocateMemory(pOriginal, size, alignment, allocationScope);
}

void VKAPI_PTR VkHostAllocator::Free(
    void* pUserData,
    void* pMemory)
{
    if (pMemory) {
        VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);
        allocator->freeMemory(pMemory);
    }
}

void VKAPI_PTR VkHostAllocator::InternalAllocation(
    void* pUserData,
    size_t size,
    VkInternalAllocationType allocationType,
    VkSystemAllocationScope allocationScope)
{
    VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);
    allocator->trackInternalAllocation(size, allocationType, allocationScope);
}

void VKAPI_PTR VkHostAllocator::InternalFree(
    void* pUserData,
    size_t size,
    VkInternalAllocationType allocationType,
    VkSystemAllocationScope allocationScope)
{
    VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);
    allocator->trackInternalDeallocation(size, allocationType, allocationScope);
}

// Core allocation methods

void* VkHostAllocator::allocateMemory(size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    // Use memory pools for small allocations if enabled
    if (enableMemoryPools && size <= smallAllocationThreshold) {
        return allocateFromPool(size, scope);
    }

    // Platform-specific aligned memory allocation
    void* memory = nullptr;

#if defined(_WIN32)
    memory = _aligned_malloc(size, alignment);
#else
    // POSIX-compliant systems
    if (posix_memalign(&memory, alignment, size) != 0) {
        memory = nullptr;
    }
#endif

    // Track the allocation if successful
    if (memory != nullptr) {
        trackAllocation(memory, size, scope);
    }

    return memory;
}

void* VkHostAllocator::allocateFromPool(size_t size, VkSystemAllocationScope scope)
{
    if (!enableMemoryPools || size > smallAllocationThreshold) {
        return nullptr;
    }

    size_t poolIndex = getSuitablePoolIndex(size);

    if (poolIndex >= NUM_POOLS) {
        // Fallback to regular allocation if no suitable pool
        return allocateMemory(size, 16, scope);
    }

    void* memory = memoryPools[poolIndex]->allocate();

    if (memory) {
        trackAllocation(memory, size, scope, true, poolIndex);
    }

    return memory;
}

void* VkHostAllocator::reallocateMemory(void* original, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    // If original is null, this is just an allocation
    if (original == nullptr) {
        return allocateMemory(size, alignment, scope);
    }

    // Check if this is a pooled allocation
    bool isPooled = false;
    size_t poolIndex = 0;
    size_t originalSize = 0;

    acquireLock();

    auto it = allocations.find(original);
    if (it != allocations.end()) {
        originalSize = it->second.size;
        isPooled = it->second.isPooled;
        poolIndex = it->second.poolIndex;
    } else {
        INK_WARN << "Reallocation called on untracked memory: " << original;
        // Fall back to regular allocation
        releaseLock();
        void* newMem = allocateMemory(size, alignment, scope);
        return newMem;
    }

    releaseLock();

    // For pooled allocations, we need to allocate a new block and copy data
    if (isPooled) {
        // Check if new size still fits in the same pool
        size_t newPoolIndex = getSuitablePoolIndex(size);

        if (newPoolIndex == poolIndex && size <= memoryPools[poolIndex]->getBlockSize()) {
            // Same pool, can reuse the allocation - just update the size
            acquireLock();
            auto it = allocations.find(original);
            if (it != allocations.end()) {
                it->second.size = size;
                // Update total allocated size delta
                totalAllocatedSize.fetch_add(size - originalSize, std::memory_order_relaxed);
            }
            releaseLock();
            return original;
        } else {
            // Need to allocate from a different pool or use regular allocation
            void* newMemory = (size <= smallAllocationThreshold)
                                  ? allocateFromPool(size, scope)
                                  : allocateMemory(size, alignment, scope);

            if (newMemory) {
                // Copy data to new location
                memcpy(newMemory, original, std::min(originalSize, size));

                // Free original allocation
                freeToPool(original, poolIndex);

                // Remove tracking for the original allocation
                acquireLock();
                allocations.erase(original);
                releaseLock();
            }

            return newMemory;
        }
    }

    // Handle regular (non-pooled) allocations
    void* newMemory = nullptr;

#if defined(_WIN32)
    // Windows has _aligned_realloc
    newMemory = _aligned_realloc(original, size, alignment);
#else
    // POSIX doesn't have aligned realloc, so we need to implement it
    if (posix_memalign(&newMemory, alignment, size) == 0 && newMemory != nullptr) {
        // Copy the original data to the new location
        memcpy(newMemory, original, std::min(originalSize, size));
        // Free the original memory
        free(original);
    } else {
        newMemory = nullptr;
    }
#endif

    if (newMemory != nullptr) {
        // Update tracking
        updateAllocationOnRealloc(original, newMemory, size, scope);
    }

    return newMemory;
}

void VkHostAllocator::freeMemory(void* memory)
{
    if (!memory) return;

    // Check if this is a pooled allocation
    bool isPooled = false;
    size_t poolIndex = 0;

    acquireLock();
    auto it = allocations.find(memory);
    if (it != allocations.end()) {
        isPooled = it->second.isPooled;
        poolIndex = it->second.poolIndex;

        // If batch processing is enabled and not a pooled allocation, defer the deallocation
        if (enableBatchProcessing && !isPooled) {
            deferredDeallocations.push_back(memory);

            // Process the batch if we've reached the limit
            if (deferredDeallocations.size() >= batchDeallocLimit) {
                // Release the lock before processing
                releaseLock();
                processDeferredDeallocations(false);
                return;
            }

            // Just erase from tracking but keep memory for deferred processing
            allocations.erase(it);
            releaseLock();
            return;
        }

        // Remove from tracking now
        allocations.erase(it);
    } else {
        // INK_WARN << "Attempted to free untracked allocation: " << memory;
        releaseLock();
        return;
    }
    releaseLock();

    // Return to pool if this was a pooled allocation
    if (isPooled) {
        freeToPool(memory, poolIndex);
    } else {
        // Regular deallocation
#if defined(_WIN32)
        _aligned_free(memory);
#else
        free(memory);
#endif
    }
}

void VkHostAllocator::freeToPool(void* memory, size_t poolIndex)
{
    if (poolIndex < NUM_POOLS) {
        memoryPools[poolIndex]->free(memory);
    } else {
        INK_ERROR << "Invalid pool index in freeToPool: " << poolIndex;
    }
}

// Tracking methods

void VkHostAllocator::trackAllocation(void* memory, size_t size, VkSystemAllocationScope scope, bool isPooled, size_t poolIndex)
{
    if (!trackLeaks && threadSafetyMode == HostThreadSafetyMode::NONE) {
        // Skip tracking in high-performance mode if leak tracking is disabled
        totalAllocatedSize.fetch_add(size, std::memory_order_relaxed);
        totalAllocationCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    acquireLock();

    // Record allocation details
    allocations[memory] = {size, scope, isPooled, poolIndex};

    releaseLock();

    // Update statistics using atomic operations
    totalAllocatedSize.fetch_add(size, std::memory_order_relaxed);
    totalAllocationCount.fetch_add(1, std::memory_order_relaxed);
}

void VkHostAllocator::processDeferredDeallocations(bool processAll)
{
    if (!enableBatchProcessing) {
        return;
    }

    acquireLock();

    std::vector<void*> toProcess;
    if (processAll || deferredDeallocations.size() >= batchDeallocLimit) {
        // Swap to avoid working with locked data
        toProcess.swap(deferredDeallocations);
    }

    releaseLock();

    if (toProcess.empty()) {
        return;
    }

    // Process all deallocations
    for (void* memory : toProcess) {
        // Find and remove the allocation from tracking
        acquireLock();
        auto it = allocations.find(memory);

        if (it != allocations.end()) {
            // Update statistics
            totalAllocatedSize.fetch_sub(it->second.size, std::memory_order_relaxed);

            bool isPooled = it->second.isPooled;
            size_t poolIndex = it->second.poolIndex;

            // Remove from tracking
            allocations.erase(it);

            releaseLock();

            // Free the memory
            if (isPooled) {
                freeToPool(memory, poolIndex);
            } else {
#if defined(_WIN32)
                _aligned_free(memory);
#else
                free(memory);
#endif
            }
        } else {
            releaseLock();
            // INK_WARN << "Deferred deallocation on untracked memory: " << memory;
        }
    }
}

void VkHostAllocator::trackDeallocation(void* memory)
{
    if (!trackLeaks && threadSafetyMode == HostThreadSafetyMode::NONE) {
        // Skip tracking in high-performance mode if leak tracking is disabled
        return;
    }

    acquireLock();

    // Find the allocation in our tracking map
    auto it = allocations.find(memory);
    if (it != allocations.end()) {
        // Update statistics
        totalAllocatedSize.fetch_sub(it->second.size, std::memory_order_relaxed);

        // Remove from tracking
        allocations.erase(it);
    } else {
        // This should never happen if Vulkan is behaving correctly
        // INK_WARN << "WARNING: Attempted to free untracked Vulkan allocation: " << memory;
    }

    releaseLock();
}

void VkHostAllocator::updateAllocationOnRealloc(void* oldMemory, void* newMemory, size_t newSize, VkSystemAllocationScope scope)
{
    if (!trackLeaks && threadSafetyMode == HostThreadSafetyMode::NONE) {
        // Skip tracking in high-performance mode if leak tracking is disabled
        return;
    }

    size_t oldSize = 0;

    acquireLock();

    // Find the old allocation
    auto it = allocations.find(oldMemory);
    if (it != allocations.end()) {
        oldSize = it->second.size;

        // Remove old entry
        allocations.erase(it);
    }

    // Add new entry
    allocations[newMemory] = {newSize, scope, false, 0};

    releaseLock();

    // Update total allocated size
    if (oldSize > 0) {
        // Update with difference
        if (newSize > oldSize) {
            totalAllocatedSize.fetch_add(newSize - oldSize, std::memory_order_relaxed);
        } else if (oldSize > newSize) {
            totalAllocatedSize.fetch_sub(oldSize - newSize, std::memory_order_relaxed);
        }
    } else {
        // If old size unknown, just add new size
        totalAllocatedSize.fetch_add(newSize, std::memory_order_relaxed);
    }
}

void VkHostAllocator::trackInternalAllocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope)
{
    // Update statistics using atomic operations
    totalAllocatedSize.fetch_add(size, std::memory_order_relaxed);
}

void VkHostAllocator::trackInternalDeallocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope)
{
    // Update statistics safely using atomics with underflow check
    size_t oldSize = totalAllocatedSize.load(std::memory_order_relaxed);

    while (oldSize >= size) {
        if (totalAllocatedSize.compare_exchange_weak(
                oldSize, oldSize - size,
                std::memory_order_relaxed, std::memory_order_relaxed)) {
            break;
        }
    }

    if (oldSize < size) {
        // INK_WARN << "Internal deallocation of " << size << " bytes would cause negative total!";
    }
}

size_t VkHostAllocator::getTotalAllocatedSize() const
{
    return totalAllocatedSize.load(std::memory_order_relaxed);
}

size_t VkHostAllocator::getAllocationCount() const
{
    acquireLock();
    size_t count = allocations.size();
    releaseLock();
    return count;
}

void VkHostAllocator::printMemoryStats() const
{
    // Snapshot counts to minimize time with lock held
    size_t allocationCount = 0;
    size_t totalAllocationCountSnapshot = totalAllocationCount.load(std::memory_order_relaxed);

    // Group by allocation scope
    size_t scopeSizes[5] = {0};
    size_t scopeCounts[5] = {0};
    size_t pooledAllocations = 0;
    size_t pooledSize = 0;

    acquireLock();

    allocationCount = allocations.size();

    // Calculate pool usage
    for (size_t i = 0; i < NUM_POOLS; ++i) {
        if (enableMemoryPools && memoryPools[i]) {
            pooledAllocations += memoryPools[i]->getAllocatedCount();
        }
    }

    // Analyze allocations by scope
    for (const auto& [ptr, info] : allocations) {
        if (info.scope < 5) {
            scopeSizes[info.scope] += info.size;
            scopeCounts[info.scope]++;
        }

        if (info.isPooled) {
            pooledSize += info.size;
        }
    }

    releaseLock();

    size_t totalSize = totalAllocatedSize.load(std::memory_order_relaxed);

    std::cout << "===== Vulkan Host Memory Stats =====" << std::endl;
    std::cout << "Active allocations: " << allocationCount << " ("
              << totalAllocationCountSnapshot << " total)" << std::endl;
    std::cout << "Active memory: " << totalSize << " bytes ("
              << (totalSize / 1024.0f / 1024.0f) << " MB)" << std::endl;

    if (enableMemoryPools) {
        std::cout << "Pooled allocations: " << pooledAllocations << " ("
                  << (pooledSize / 1024.0f) << " KB)" << std::endl;
    }

    std::cout << "Memory by scope:" << std::endl;
    const char* scopeNames[] = {
        "COMMAND", "OBJECT", "CACHE", "DEVICE", "INSTANCE"
    };

    for (int i = 0; i < 5; i++) {
        std::cout << "  " << std::left << std::setw(10) << scopeNames[i]
                  << ": " << std::setw(8) << scopeCounts[i] << " allocations, "
                  << scopeSizes[i] << " bytes" << std::endl;
    }

    if (enableMemoryPools) {
        std::cout << "Memory pools:" << std::endl;
        size_t poolSize = MIN_POOL_SIZE;

        for (size_t i = 0; i < NUM_POOLS; ++i) {
            if (memoryPools[i]) {
                size_t used = memoryPools[i]->getAllocatedCount();
                size_t total = memoryPools[i]->getTotalCount();
                f32 usagePercent = (total > 0) ? (100.0f * used / total) : 0.0f;

                std::cout << "  " << std::setw(4) << poolSize << " bytes: "
                          << std::setw(8) << used << "/" << total
                          << " (" << usagePercent << "%)" << std::endl;
            }
            poolSize *= 2;
        }
    }

    std::cout << "===================================" << std::endl;
}

}  // namespace aura3d
