#ifndef AURA_GPUDEBUGSTATS_H
#define AURA_GPUDEBUGSTATS_H

#pragma once

#include "aura/aura.h"

/**
 * @file GpuDebugStats.h
 * @brief What a renderer backend can tell the benchmark report about the GPU.
 *
 * Backend-agnostic: DebugMode lives in Core and must not include a Vulkan
 * header. Every struct carries an explicit @c available flag so a backend
 * that can't answer says so, rather than reporting a zero that reads like a
 * measurement.
 *
 * Only Vulkan implements this today; OpenGL and the software rasteriser
 * return nullptr from IRenderer::gpuDebugSource().
 */

namespace aura3d {

/// GPU-side wall time for the most recently *retired* frame, not the one
/// being recorded -- querying the current frame would stall the pipeline
/// waiting for the GPU to reach it. Trails the CPU by the frames in flight.
struct GpuTimingStats
{
    bool available = false;  ///< False when the device cannot timestamp at all.
    f64  frameMillis = 0.0;  ///< GPU time from the frame's first to last command.

    //! Frames whose queries came back unusable (slot never submitted, device
    //! reset, results not ready). A steadily rising count means the timing
    //! section of the report is measuring less than it appears to.
    u64 droppedSamples = 0;
};

/**
 * @struct GpuMemoryStats
 * @brief Device- and host-side allocation counters for the active backend.
 *
 * @b device is real VRAM (counted at vkAllocateMemory, inside VMA). @b host is
 * the driver's own CPU-side bookkeeping (via VkAllocationCallbacks) -- small,
 * but invisible to AllocationTracker since the driver uses malloc.
 */
struct GpuMemoryStats
{
    bool available = false;

    u64 deviceBytesLive       = 0;  ///< Device memory allocated and not yet freed.
    u64 deviceBytesPeak       = 0;  ///< High-water mark of deviceBytesLive.
    u64 deviceAllocationCount = 0;  ///< Cumulative vkAllocateMemory calls.
    u64 deviceFreeCount       = 0;  ///< Cumulative vkFreeMemory calls.
    u64 deviceBlocksLive      = 0;  ///< Live VkDeviceMemory objects.

    //! Bytes handed out of the live blocks. Below deviceBytesLive by whatever
    //! the suballocator holds in reserve (internal fragmentation).
    u64 deviceBytesInUse = 0;

    u64 hostBytesLive       = 0;  ///< Driver host allocations, live.
    u64 hostBytesPeak       = 0;
    u64 hostAllocationCount = 0;
    u64 hostFreeCount       = 0;

    //! Driver/OS view of the memory heaps, when the backend can query it.
    //! Zero when unavailable; @c budgetBytes is what the process is allowed,
    //! @c budgetUsageBytes what it (and its share of the system) is using.
    u64 budgetBytes      = 0;
    u64 budgetUsageBytes = 0;
};

/// Implemented by backends able to report GPU timing and allocations. Both
/// accessors are cheap reads of already-collected state, never a device round
/// trip: memory stats are queried once per report, timing once per frame.
class IGpuDebugSource
{
public:
    virtual ~IGpuDebugSource() = default;

    [[nodiscard]] virtual GpuMemoryStats gpuMemoryStats() const noexcept = 0;

    //! The most recently resolved GPU frame time. See GpuTimingStats.
    [[nodiscard]] virtual GpuTimingStats gpuTimingStats() const noexcept = 0;

    //! Backend name for the report, e.g. "vulkan".
    [[nodiscard]] virtual const char* gpuDebugBackendName() const noexcept = 0;
};

} // namespace aura3d

#endif // AURA_GPUDEBUGSTATS_H
