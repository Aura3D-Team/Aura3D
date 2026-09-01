#include "aura/Renderer/Vulkan/VkAura/VkDebugMode/VkDebugMetrics.h"

#include <array>

namespace aura3d {
namespace vk {

namespace {

/*
 * Constant-initialised at load time, and never destroyed. VMA calls the
 * callbacks below from vmaDestroyAllocator, which for a backend switch or a
 * process teardown can run at a point where a function-local static's
 * destructor has already fired.
 */
constinit VkDeviceMemoryCounters g_deviceMemory{};

void VKAPI_PTR onVmaAllocate(VmaAllocator, u32, VkDeviceMemory, VkDeviceSize size, void*) noexcept
{
    VkDeviceMemoryCounters::get().recordAllocate(static_cast<u64>(size));
}

void VKAPI_PTR onVmaFree(VmaAllocator, u32, VkDeviceMemory, VkDeviceSize size, void*) noexcept
{
    VkDeviceMemoryCounters::get().recordFree(static_cast<u64>(size));
}

constinit VmaDeviceMemoryCallbacks g_vmaCallbacks{
    .pfnAllocate = &onVmaAllocate,
    .pfnFree     = &onVmaFree,
    .pUserData   = nullptr,
};

} // namespace

VkDeviceMemoryCounters& VkDeviceMemoryCounters::get() noexcept
{
    return g_deviceMemory;
}

const VmaDeviceMemoryCallbacks* VkDeviceMemoryCounters::vmaCallbacks() noexcept
{
    return &g_vmaCallbacks;
}

void VkDeviceMemoryCounters::recordAllocate(u64 bytes) noexcept
{
    _allocCount.fetch_add(1, std::memory_order_relaxed);
    _liveBlocks.fetch_add(1, std::memory_order_relaxed);

    const u64 live = _liveBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;

    u64 peak = _peakBytes.load(std::memory_order_relaxed);
    while (peak < live &&
           !_peakBytes.compare_exchange_weak(peak, live,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed))
    {
        //! compare_exchange_weak refreshed `peak`; re-test against `live`.
    }
}

void VkDeviceMemoryCounters::recordFree(u64 bytes) noexcept
{
    _freeCount.fetch_add(1, std::memory_order_relaxed);
    _liveBlocks.fetch_sub(1, std::memory_order_relaxed);
    _liveBytes.fetch_sub(bytes, std::memory_order_relaxed);
}

void VkDeviceMemoryCounters::reset() noexcept
{
    _liveBytes.store(0, std::memory_order_relaxed);
    _peakBytes.store(0, std::memory_order_relaxed);
    _allocCount.store(0, std::memory_order_relaxed);
    _freeCount.store(0, std::memory_order_relaxed);
    _liveBlocks.store(0, std::memory_order_relaxed);
}

GpuMemoryStats VkDebugMetrics::gpuMemoryStats() const noexcept
{
    GpuMemoryStats stats;
    stats.available = true;

    const VkDeviceMemoryCounters& device = VkDeviceMemoryCounters::get();

    stats.deviceBytesLive       = device.liveBytes();
    stats.deviceBytesPeak       = device.peakBytes();
    stats.deviceAllocationCount = device.allocCount();
    stats.deviceFreeCount       = device.freeCount();
    stats.deviceBlocksLive      = device.liveBlocks();

    const VkHostAllocationStats host = VkCountingAllocator::get().snapshot();

    stats.hostBytesLive       = host.liveBytes;
    stats.hostBytesPeak       = host.peakBytes;
    stats.hostAllocationCount = host.allocationCount;
    stats.hostFreeCount       = host.freeCount;

    if (_allocator == VK_NULL_HANDLE)
        return stats;

    /*
     * vmaCalculateStatistics walks every block and every allocation, so it is
     * comfortably the most expensive thing in this file. Acceptable because
     * this runs once per report, not once per frame -- and the number it
     * produces, how much of the memory held is actually in use, is not
     * derivable from the block counters at all.
     */
    VmaTotalStatistics totals{};
    vmaCalculateStatistics(_allocator, &totals);
    stats.deviceBytesInUse = static_cast<u64>(totals.total.statistics.allocationBytes);

    /*
     * Budget is per heap, and only the device-local heaps are the VRAM anyone
     * means by the word. Summing them gives the number to compare a peak
     * against; including the host-visible system-memory heaps would inflate it
     * by however much RAM the machine has.
     */
    const VkPhysicalDeviceMemoryProperties* memoryProperties = nullptr;
    vmaGetMemoryProperties(_allocator, &memoryProperties);

    if (memoryProperties == nullptr)
        return stats;

    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(_allocator, budgets.data());

    for (u32 heap = 0; heap < memoryProperties->memoryHeapCount; ++heap)
    {
        if ((memoryProperties->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0)
            continue;

        stats.budgetBytes      += static_cast<u64>(budgets[heap].budget);
        stats.budgetUsageBytes += static_cast<u64>(budgets[heap].usage);
    }

    return stats;
}

GpuTimingStats VkDebugMetrics::gpuTimingStats() const noexcept
{
    return _timestamps.stats();
}

} // namespace vk
} // namespace aura3d
