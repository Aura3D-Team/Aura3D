#ifndef VKHOSTALLOCATOR_H
#define VKHOSTALLOCATOR_H
#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
// #include <memory>

namespace aura3d {

class VkHostAllocator
{
public:
    VkHostAllocator();

    // static VkHostAllocator& getInstance();

    // Destructor
    ~VkHostAllocator();

    // Static method to get callbacks
    VkAllocationCallbacks* getCallbacks();

    // Memory statistics
    size_t getTotalAllocatedSize() const;
    size_t getAllocationCount() const;
    void printMemoryStats() const;

private:
    // Static singleton instance
    // static std::unique_ptr<VkHostAllocator> instance;
    // static std::once_flag initInstanceFlag;

    VkAllocationCallbacks callbacks;

    // Memory tracking
    struct AllocationInfo {
        size_t size;
        VkSystemAllocationScope scope;
    };

    std::unordered_map<void*, AllocationInfo> allocations;
    size_t totalAllocatedSize;
    size_t totalAllocationCount;
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

    // Tracking methods
    void trackAllocation(void* memory, size_t size, VkSystemAllocationScope scope);
    void trackDeallocation(void* memory);
    void updateAllocationOnRealloc(void* oldMemory, void* newMemory, size_t newSize, VkSystemAllocationScope scope);
    void trackInternalAllocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope);
    void trackInternalDeallocation(size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope);
};

}  // namespace aura3d

#endif // VKHOSTALLOCATOR_H
