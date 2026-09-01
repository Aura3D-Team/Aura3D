#ifndef VKDEBUGMETRICS_H
#define VKDEBUGMETRICS_H

#pragma once

#include <atomic>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "aura/Core/AuraCore.h"
#include "aura/Core/DebugMode/GpuDebugStats.h"
#include "aura/Renderer/Vulkan/VkAura/VkDebugMode/VkCountingAllocator.h"
#include "aura/Renderer/Vulkan/VkAura/VkDebugMode/VkTimestampQuery.h"

/**
 * @file VkDebugMetrics.h
 * @brief The Vulkan backend's implementation of aura3d::IGpuDebugSource.
 *
 * Three sources, deliberately kept apart because they measure three different
 * things that are routinely conflated:
 *
 *   - VkDeviceMemoryCounters -- real VRAM, counted where vkAllocateMemory is
 *     actually called. Since every device allocation in this engine goes
 *     through VMA, that is VMA's device-memory callbacks and nowhere else.
 *   - VkCountingAllocator -- the driver's host-side bookkeeping, counted
 *     through VkAllocationCallbacks.
 *   - VkTimestampQuery -- GPU wall time per frame.
 */

namespace aura3d {
namespace vk {

/**
 * @class VkDeviceMemoryCounters
 * @brief Counts device memory at the point VMA allocates and frees it.
 *
 * ## Why here and not VkAllocationCallbacks
 *
 * A `pAllocator` passed to vkAllocateMemory counts the *host* bytes the driver
 * spent describing the allocation, not the device bytes it made -- so the
 * obvious-looking hook measures the wrong quantity by an order of magnitude or
 * three. VMA, on the other hand, calls pfnAllocate with the exact VkDeviceSize
 * of every VkDeviceMemory block it creates, which is the number wanted.
 *
 * ## Why a singleton
 *
 * VMA copies the callback pointers into the allocator at vmaCreateAllocator
 * time and calls them for the allocator's whole life, including from
 * vmaDestroyAllocator during teardown. A process-wide, never-destroyed counter
 * is immune to the ordering questions that raises, and there is at most one
 * Vulkan device in flight anyway -- a backend switch destroys the old allocator
 * before building the new one, so the counters simply carry across it, which is
 * also what makes a leak visible *through* a switch.
 */
class VkDeviceMemoryCounters
{
public:
    constexpr VkDeviceMemoryCounters() noexcept = default;

    VkDeviceMemoryCounters(const VkDeviceMemoryCounters&)            = delete;
    VkDeviceMemoryCounters& operator=(const VkDeviceMemoryCounters&) = delete;

    [[nodiscard]] static VkDeviceMemoryCounters& get() noexcept;

    /**
     * @brief The callbacks block to store in VmaAllocatorCreateInfo.
     *
     * @return Points at storage with static lifetime; safe to keep for the
     *         lifetime of any allocator.
     */
    [[nodiscard]] static const VmaDeviceMemoryCallbacks* vmaCallbacks() noexcept;

    void recordAllocate(u64 bytes) noexcept;
    void recordFree(u64 bytes) noexcept;

    [[nodiscard]] u64 liveBytes()  const noexcept { return _liveBytes.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 peakBytes()  const noexcept { return _peakBytes.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 allocCount() const noexcept { return _allocCount.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 freeCount()  const noexcept { return _freeCount.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 liveBlocks() const noexcept { return _liveBlocks.load(std::memory_order_relaxed); }

    //! For tests and for discarding load-time noise before a measured window.
    void reset() noexcept;

private:
    std::atomic<u64> _liveBytes{0};
    std::atomic<u64> _peakBytes{0};
    std::atomic<u64> _allocCount{0};
    std::atomic<u64> _freeCount{0};
    std::atomic<u64> _liveBlocks{0};
};

/**
 * @class VkDebugMetrics
 * @brief Gathers the Vulkan backend's GPU counters behind IGpuDebugSource.
 *
 * Held by value in VulkanRenderer for the renderer's lifetime, which is what
 * satisfies VkCountingAllocator's requirement to outlive every handle created
 * through it.
 */
class VkDebugMetrics final : public IGpuDebugSource
{
public:
    VkDebugMetrics() noexcept = default;
    ~VkDebugMetrics() override = default;

    VkDebugMetrics(const VkDebugMetrics&)            = delete;
    VkDebugMetrics& operator=(const VkDebugMetrics&) = delete;

    //! The GPU frame timer. See VkTimestampQuery for the resolve protocol.
    [[nodiscard]] VkTimestampQuery& timestamps() noexcept { return _timestamps; }

    /**
     * @brief Points the memory section at @p allocator for detailed statistics.
     *
     * The raw counters come from VkDeviceMemoryCounters regardless; this adds
     * the suballocation and heap-budget detail that only the live allocator can
     * answer. Pass VK_NULL_HANDLE on teardown -- the handle must not outlive
     * vmaDestroyAllocator.
     */
    void setAllocator(VmaAllocator allocator) noexcept { _allocator = allocator; }

    [[nodiscard]] GpuMemoryStats gpuMemoryStats() const noexcept override;
    [[nodiscard]] GpuTimingStats gpuTimingStats() const noexcept override;
    [[nodiscard]] const char* gpuDebugBackendName() const noexcept override { return "vulkan"; }

private:
    /*
     * Only the timestamp pool is owned here. The two allocation counters are
     * process-wide singletons (VkCountingAllocator, VkDeviceMemoryCounters):
     * both are called by Vulkan and by VMA at points where an object owned by
     * the renderer may already be gone, and both must survive a backend switch
     * so that memory retained across one is still visible.
     */
    VkTimestampQuery _timestamps;
    VmaAllocator     _allocator = VK_NULL_HANDLE;
};

} // namespace vk
} // namespace aura3d

#endif // VKDEBUGMETRICS_H
