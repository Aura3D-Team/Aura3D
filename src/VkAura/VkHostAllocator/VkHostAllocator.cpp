#include "VkHostAllocator.h"
#include <iostream>
#include <iomanip>
#include <plog/Log.h>
#include <string.h> // For memcpy

// Platform-specific includes for aligned memory allocation
#if defined(_WIN32)
#include <malloc.h>
#else
#include <stdlib.h>
#endif

namespace aura3d {

std::unique_ptr<VkHostAllocator> VkHostAllocator::instance = nullptr;
std::once_flag VkHostAllocator::initInstanceFlag;
VkAllocationCallbacks VkHostAllocator::callbacks = {};

VkHostAllocator::VkHostAllocator()
    : totalAllocatedSize(0), totalAllocationCount(0)
{
    callbacks.pUserData = this;
    callbacks.pfnAllocation = &VkHostAllocator::Allocation;
    callbacks.pfnReallocation = &VkHostAllocator::Reallocation;
    callbacks.pfnFree = &VkHostAllocator::Free;
    callbacks.pfnInternalAllocation = &VkHostAllocator::InternalAllocation;
    callbacks.pfnInternalFree = &VkHostAllocator::InternalFree;
}

VkHostAllocator::~VkHostAllocator()
{
    if (!allocations.empty()) {
        PLOG_ERROR << "WARNING: " << allocations.size()
        << " Vulkan allocations still active at VkHostAllocator destruction!";
        // Print details of leaked allocations
        for (const auto& [ptr, info] : allocations) {
            PLOG_ERROR << "  Leaked: " << ptr << ", size: " << info.size
                       << ", scope: " << static_cast<int>(info.scope);
        }
    }
}

VkHostAllocator& VkHostAllocator::getInstance() {
    std::call_once(initInstanceFlag, []() {
        instance = std::unique_ptr<VkHostAllocator>(new VkHostAllocator());
    });
    return *instance;
}

VkAllocationCallbacks* VkHostAllocator::getCallbacks()
{
    getInstance();
    return &callbacks;
}

void* VKAPI_PTR VkHostAllocator::Allocation(
    void* pUserData,
    size_t size,
    size_t alignment,
    VkSystemAllocationScope allocationScope)
{
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
        VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);
        allocator->trackAllocation(memory, size, allocationScope);
    }
    return memory;
}

void* VKAPI_PTR VkHostAllocator::Reallocation(
    void* pUserData,
    void* pOriginal,
    size_t size,
    size_t alignment,
    VkSystemAllocationScope allocationScope)
{
    VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);

    // If original is null, this is just an allocation
    if (pOriginal == nullptr) {
        return Allocation(pUserData, size, alignment, allocationScope);
    }

    // Get original size from our tracking map
    size_t originalSize = 0;
    {
        std::lock_guard<std::mutex> lock(allocator->allocationMutex);
        auto it = allocator->allocations.find(pOriginal);
        if (it != allocator->allocations.end()) {
            originalSize = it->second.size;
        } else {
            PLOG_WARNING << "Reallocation called on untracked memory: " << pOriginal;
            // Fall back to regular allocation
            void* newMem = Allocation(pUserData, size, alignment, allocationScope);
            return newMem;
        }
    }

    // Perform platform-specific reallocation
    void* newMemory = nullptr;

#if defined(_WIN32)
    // Windows has _aligned_realloc
    newMemory = _aligned_realloc(pOriginal, size, alignment);
#else
    // POSIX doesn't have aligned realloc, so we need to implement it
    int result = posix_memalign(&newMemory, alignment, size);
    if (result == 0 && newMemory != nullptr) {
        // Copy the original data to the new location
        memcpy(newMemory, pOriginal, originalSize < size ? originalSize : size);
        // Free the original memory
        free(pOriginal);
    } else {
        newMemory = nullptr;
    }
#endif

    if (newMemory != nullptr) {
        // Update tracking - remove old and add new
        allocator->updateAllocationOnRealloc(pOriginal, newMemory, size, allocationScope);

        // PLOG_DEBUG << "Vulkan reallocated: " << pOriginal << " -> " << newMemory
        //            << ", old size: " << originalSize << ", new size: " << size
        //            << ", scope: " << static_cast<int>(allocationScope);
    }

    return newMemory;
}

void VKAPI_PTR VkHostAllocator::Free(
    void* pUserData,
    void* pMemory) {
    if (pMemory) {
        // Track deallocation before freeing
        VkHostAllocator* allocator = static_cast<VkHostAllocator*>(pUserData);
        allocator->trackDeallocation(pMemory);

        // Platform-specific aligned memory deallocation
#if defined(_WIN32)
        _aligned_free(pMemory);
#else
        free(pMemory);
#endif
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

void VkHostAllocator::trackAllocation(void* memory, size_t size, VkSystemAllocationScope scope) {
    std::lock_guard<std::mutex> lock(allocationMutex);
    // Record allocation details
    allocations[memory] = {size, scope};
    // Update statistics
    totalAllocatedSize += size;
    totalAllocationCount++;
    // Debug logging for allocations
    // PLOG_DEBUG << "Vulkan allocated: " << memory << ", size: " << size
    //            << ", scope: " << static_cast<int>(scope);
}

void VkHostAllocator::trackDeallocation(void* memory) {
    std::lock_guard<std::mutex> lock(allocationMutex);
    // Find the allocation in our tracking map
    auto it = allocations.find(memory);
    if (it != allocations.end()) {
        // Update statistics
        totalAllocatedSize -= it->second.size;
        // Debug logging for deallocations
        // PLOG_DEBUG << "Vulkan freed: " << memory << ", size: " << it->second.size
        //            << ", scope: " << static_cast<int>(it->second.scope);
        // Remove from tracking
        allocations.erase(it);
    }
    else {
        // This should never happen if Vulkan is behaving correctly
        PLOG_WARNING << "WARNING: Attempted to free untracked Vulkan allocation: " << memory;
    }
}

void VkHostAllocator::updateAllocationOnRealloc(void* oldMemory, void* newMemory, size_t newSize, VkSystemAllocationScope scope) {
    std::lock_guard<std::mutex> lock(allocationMutex);

    // Find the old allocation
    auto it = allocations.find(oldMemory);
    if (it != allocations.end()) {
        size_t oldSize = it->second.size;

        // Update statistics
        totalAllocatedSize = totalAllocatedSize - oldSize + newSize;

        // Remove old entry
        allocations.erase(it);
    }

    // Add new entry
    allocations[newMemory] = {newSize, scope};
}

void VkHostAllocator::trackInternalAllocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope) {
    std::lock_guard<std::mutex> lock(allocationMutex);

    // Update statistics - we track these separately from the regular allocations
    totalAllocatedSize += size;

    // const char* typeStr;
    // switch (type) {
    // case VK_INTERNAL_ALLOCATION_TYPE_EXECUTABLE: typeStr = "EXECUTABLE"; break;
    // default: typeStr = "UNKNOWN"; break;
    // }

    // PLOG_DEBUG << "Vulkan internal allocation: " << size << " bytes, type: "
    //            << typeStr << ", scope: " << static_cast<int>(scope);
}

void VkHostAllocator::trackInternalDeallocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope) {
    std::lock_guard<std::mutex> lock(allocationMutex);

    // Update statistics
    if (totalAllocatedSize >= size) {
        totalAllocatedSize -= size;
    } else {
        PLOG_WARNING << "Internal deallocation of " << size << " bytes would cause negative total!";
        totalAllocatedSize = 0;
    }

    // const char* typeStr;
    // switch (type) {
    // case VK_INTERNAL_ALLOCATION_TYPE_EXECUTABLE: typeStr = "EXECUTABLE"; break;
    // default: typeStr = "UNKNOWN"; break;
    // }

    // PLOG_DEBUG << "Vulkan internal deallocation: " << size << " bytes, type: "
    //            << typeStr << ", scope: " << static_cast<int>(scope);
}

size_t VkHostAllocator::getTotalAllocatedSize() const {
    std::lock_guard<std::mutex> lock(allocationMutex);
    return totalAllocatedSize;
}

size_t VkHostAllocator::getAllocationCount() const {
    std::lock_guard<std::mutex> lock(allocationMutex);
    return allocations.size();
}

void VkHostAllocator::printMemoryStats() const
{
    std::lock_guard<std::mutex> lock(allocationMutex);
    std::cout << "===== Vulkan Host Memory Stats =====" << std::endl;
    std::cout << "Active allocations: " << allocations.size() << " ("
              << totalAllocationCount << " total)" << std::endl;
    std::cout << "Active memory: " << totalAllocatedSize << " bytes ("
              << (totalAllocatedSize / 1024.0f / 1024.0f) << " MB)" << std::endl;

    // Group by allocation scope
    size_t scopeSizes[5] = {0};
    size_t scopeCounts[5] = {0};
    for (const auto& [ptr, info] : allocations) {
        if (info.scope < 5) {
            scopeSizes[info.scope] += info.size;
            scopeCounts[info.scope]++;
        }
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

    std::cout << "===================================" << std::endl;
}

}  // namespace aura3d
