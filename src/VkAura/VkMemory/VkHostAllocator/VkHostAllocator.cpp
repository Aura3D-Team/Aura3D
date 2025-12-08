#include "VkHostAllocator.h"

#include <iostream>
#include <iomanip>
#include <ink/ink.hpp>
#include <algorithm>
#include <string.h>
#if defined(_WIN32)
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include "aura.hpp"

namespace aura3d {
namespace vk {

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

    if (enableMemoryPools) {

        size_t poolSize = MIN_POOL_SIZE;
        for (size_t i = 0; i < NUM_POOLS; ++i) {

            memoryPools[i] = std::make_unique<MemoryPool>(poolSize);
            poolSize *= 2;
        }
    }

    INK_DEBUG << "VkHostAllocator created with "
              << (threadSafetyMode == HostThreadSafetyMode::NONE ? "no thread safety" :
                      (threadSafetyMode == HostThreadSafetyMode::LOCK_FREE ? "lock-free" : "mutex"))
              << ", memory pools " << (enableMemoryPools ? "enabled" : "disabled");
}

VkHostAllocator::~VkHostAllocator()
{

    if (enableBatchProcessing) {

        processDeferredDeallocations(true);
    }

    if (trackLeaks && !allocations.empty()) {
        INK_WARN << "WARNING: " << allocations.size()
        << " Vulkan allocations still active at VkHostAllocator destruction!";

        for (const auto& [ptr, info] : allocations) {
            INK_WARN << "  Leaked: " << ptr << ", size: " << info.size
                     << ", scope: " << static_cast<int>(info.scope)
                     << (info.isPooled ? " (pooled)" : "");
        }
    }

}

size_t VkHostAllocator::getSuitablePoolIndex(size_t size) const
{

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

void* VKAPI_PTR VkHostAllocator::Allocation(
    void* pUserData,
    size_t size,
    size_t alignment,
    VkSystemAllocationScope allocationScope)
{
    VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);

    void* result = allocator->allocateMemory(size, alignment, allocationScope);
    return result;
}

void* VKAPI_PTR VkHostAllocator::Reallocation(
    void* pUserData,
    void* pOriginal,
    size_t size,
    size_t alignment,
    VkSystemAllocationScope allocationScope)
{
    VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);

    void* result = allocator->reallocateMemory(pOriginal, size, alignment, allocationScope);
    return result;
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

void* VkHostAllocator::allocateMemory(size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    if (enableMemoryPools && size <= smallAllocationThreshold) {

        return allocateFromPool(size, scope);
    }

    void* memory = nullptr;
#if defined(_WIN32)
    memory = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&memory, alignment, size) != 0) {
        memory = nullptr;
    }
#endif

    if (memory != nullptr) {

        trackAllocation(memory, size, scope);
    } else {
        INK_ERROR << "Direct allocation failed for size " << size;
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

        return allocateMemory(size, 16, scope);
    }

    void* memory = memoryPools[poolIndex]->allocate();

    if (memory) {

        trackAllocation(memory, size, scope, true, poolIndex);
    } else {
        INK_ERROR << "Pool allocation failed for size " << size;
    }

    return memory;
}

void* VkHostAllocator::reallocateMemory(void* original, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    if (original == nullptr) {

        return allocateMemory(size, alignment, scope);
    }

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
        releaseLock();
        void* newMem = allocateMemory(size, alignment, scope);
        return newMem;
    }
    releaseLock();

    if (isPooled) {

        size_t newPoolIndex = getSuitablePoolIndex(size);

        if (newPoolIndex == poolIndex && size <= memoryPools[poolIndex]->getBlockSize()) {

            acquireLock();
            auto it = allocations.find(original);
            if (it != allocations.end()) {
                it->second.size = size;
                totalAllocatedSize.fetch_add(size - originalSize, std::memory_order_relaxed);

            }
            releaseLock();
            return original;
        } else {

            void* newMemory = (size <= smallAllocationThreshold)
                                  ? allocateFromPool(size, scope)
                                  : allocateMemory(size, alignment, scope);

            if (newMemory) {

                memcpy(newMemory, original, std::min(originalSize, size));
                freeToPool(original, poolIndex);

                acquireLock();
                allocations.erase(original);
                releaseLock();
            } else {
                INK_ERROR << "Failed to allocate memory for reallocation";
            }

            return newMemory;
        }
    }

    void* newMemory = nullptr;
#if defined(_WIN32)
    newMemory = _aligned_realloc(original, size, alignment);
#else
    if (posix_memalign(&newMemory, alignment, size) == 0 && newMemory != nullptr) {
        memcpy(newMemory, original, std::min(originalSize, size));
        free(original);
    } else {
        newMemory = nullptr;
    }
#endif

    if (newMemory != nullptr) {

        updateAllocationOnRealloc(original, newMemory, size, scope);
    } else {
        INK_ERROR << "Reallocation failed";
    }

    return newMemory;
}

void VkHostAllocator::freeMemory(void* memory)
{
    if (!memory) return;


    bool isPooled = false;
    size_t poolIndex = 0;

    acquireLock();
    auto it = allocations.find(memory);
    if (it != allocations.end()) {
        isPooled = it->second.isPooled;
        poolIndex = it->second.poolIndex;


        if (enableBatchProcessing && !isPooled) {

            deferredDeallocations.push_back(memory);

            if (deferredDeallocations.size() >= batchDeallocLimit) {

                releaseLock();
                processDeferredDeallocations(false);
                return;
            }

            allocations.erase(it);
            releaseLock();
            return;
        }

        allocations.erase(it);
    } else {

        releaseLock();
        return;
    }
    releaseLock();

    if (isPooled) {

        freeToPool(memory, poolIndex);
    } else {

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

void VkHostAllocator::trackAllocation(void* memory, size_t size, VkSystemAllocationScope scope, bool isPooled, size_t poolIndex)
{
    acquireLock();
    allocations[memory] = {size, scope, isPooled, poolIndex};
    releaseLock();

    totalAllocatedSize.fetch_add(size, std::memory_order_relaxed);
    totalAllocationCount.fetch_add(1, std::memory_order_relaxed);

}

void VkHostAllocator::processDeferredDeallocations(bool processAll)
{
    if (!enableBatchProcessing)
        return;

    acquireLock();
    std::vector<void*> toProcess;
    if (processAll || deferredDeallocations.size() >= batchDeallocLimit)
        toProcess.swap(deferredDeallocations);

    releaseLock();

    if (toProcess.empty())
        return;

    for (void* memory : toProcess)
    {
        acquireLock();
        auto it = allocations.find(memory);
        if (it != allocations.end())
        {
            totalAllocatedSize.fetch_sub(it->second.size, std::memory_order_relaxed);
            bool isPooled = it->second.isPooled;
            size_t poolIndex = it->second.poolIndex;

            allocations.erase(it);
            releaseLock();

            if (isPooled) {

                freeToPool(memory, poolIndex);
            } else {

#if defined(_WIN32)
                _aligned_free(memory);
#else
                free(memory);
#endif
            }
        }
        else
            releaseLock();
    }

}

void VkHostAllocator::trackDeallocation(void* memory)
{
    acquireLock();
    auto it = allocations.find(memory);
    if (it != allocations.end())
    {
        totalAllocatedSize.fetch_sub(it->second.size, std::memory_order_relaxed);
        allocations.erase(it);
    }
    releaseLock();
}

void VkHostAllocator::updateAllocationOnRealloc(void* oldMemory, void* newMemory, size_t newSize, VkSystemAllocationScope scope)
{
    size_t oldSize = 0;
    acquireLock();

    auto it = allocations.find(oldMemory);
    if (it != allocations.end())
    {
        oldSize = it->second.size;
        allocations.erase(it);
    }

    allocations[newMemory] = {newSize, scope, false, 0};
    releaseLock();

    if (oldSize > 0)
    {
        if (newSize > oldSize)
            totalAllocatedSize.fetch_add(newSize - oldSize, std::memory_order_relaxed);
        else if (oldSize > newSize)
            totalAllocatedSize.fetch_sub(oldSize - newSize, std::memory_order_relaxed);
    }
    else
        totalAllocatedSize.fetch_add(newSize, std::memory_order_relaxed);
}

void VkHostAllocator::trackInternalAllocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope)
{
    totalAllocatedSize.fetch_add(size, std::memory_order_relaxed);
}

void VkHostAllocator::trackInternalDeallocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope)
{
    size_t oldSize = totalAllocatedSize.load(std::memory_order_relaxed);
    while (oldSize >= size)
    {
        if (totalAllocatedSize.compare_exchange_weak(oldSize, oldSize - size, std::memory_order_relaxed, std::memory_order_relaxed))
            break;
    }

    if (oldSize < size)
        INK_WARN << "Internal deallocation of " << size << " bytes would cause negative total!";
}

size_t VkHostAllocator::getTotalAllocatedSize() const
{
    size_t size = totalAllocatedSize.load(std::memory_order_relaxed);
    return size;
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
    size_t allocationCount = 0;
    size_t totalAllocationCountSnapshot = totalAllocationCount.load(std::memory_order_relaxed);
    size_t scopeSizes[5] = {0};
    size_t scopeCounts[5] = {0};
    size_t pooledAllocations = 0;
    size_t pooledSize = 0;

    acquireLock();
    allocationCount = allocations.size();

    for (size_t i = 0; i < NUM_POOLS; ++i) {
        if (enableMemoryPools && memoryPools[i]) {
            pooledAllocations += memoryPools[i]->getAllocatedCount();
        }
    }

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
    INK_LOG << "===== Vulkan Host Memory Stats =====";
    INK_LOG << "Active allocations: " << allocationCount << " ("
              << totalAllocationCountSnapshot << " total)";
    INK_LOG << "Active memory: " << totalSize << " bytes ("
              << (totalSize / 1024.0f / 1024.0f) << " MB)";
    if (enableMemoryPools) {
        INK_LOG << "Pooled allocations: " << pooledAllocations << " ("
                  << (pooledSize / 1024.0f) << " KB)";
    }
    INK_LOG << "Memory by scope:";
    const char* scopeNames[] = {
        "COMMAND", "OBJECT", "CACHE", "DEVICE", "INSTANCE"
    };
    for (int i = 0; i < 5; i++) {
        INK_LOG << "  " << std::left << std::setw(10) << scopeNames[i]
                  << ": " << std::setw(8) << scopeCounts[i] << " allocations, "
                  << scopeSizes[i] << " bytes";
    }
    if (enableMemoryPools) {
        INK_LOG << "Memory pools:";
        size_t poolSize = MIN_POOL_SIZE;
        for (size_t i = 0; i < NUM_POOLS; ++i) {
            if (memoryPools[i]) {
                size_t used = memoryPools[i]->getAllocatedCount();
                size_t total = memoryPools[i]->getTotalCount();
                f32 usagePercent = (total > 0) ? (100.0f * used / total) : 0.0f;
                INK_LOG << "  " << std::setw(4) << poolSize << " bytes: "
                          << std::setw(8) << used << "/" << total
                          << " (" << usagePercent << "%)";
            }
            poolSize *= 2;
        }
    }
    INK_LOG << "===================================";
}

}
}  // namespace aura3d
