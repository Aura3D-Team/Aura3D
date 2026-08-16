#ifndef AURA_GPUDEBUGSTATS_H
#define AURA_GPUDEBUGSTATS_H

#pragma once

#include "aura/aura.h"

/**
 * @file GpuDebugStats.h
 * @brief What a renderer backend can tell the benchmark report about the GPU.
 *
 * Backend-agnostic on purpose: DebugMode lives in Core and must not include a
 * Vulkan header to compile, and a backend that cannot answer a question should
 * be able to say so rather than be missing from the interface. Every struct
 * here therefore carries an explicit @c available flag, and the report prints
 * "unavailable" where a backend declines rather than printing a zero that reads
 * like a measurement.
 *
 * Only the Vulkan backend implements this today. OpenGL could
 * (@c glQueryCounter with @c GL_TIMESTAMP), the software rasteriser
 * structurally cannot, and both currently return nullptr from
 * IRenderer::gpuDebugSource().
 */

namespace aura3d {

/**
 * @struct GpuTimingStats
 * @brief GPU-side wall time for the most recently *retired* frame.
 *
 * Not the frame being recorded: reading a timestamp query for the current frame
 * would mean waiting for the GPU to reach it, which is a full pipeline stall and
 * would change the number being measured. Backends resolve queries for a frame
 * slot only once its fence has already been waited on for other reasons, so the
 * value here trails the CPU by the number of frames in flight.
 */
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
 * Two separate axes, which are routinely conflated:
 *   - @b device: real VRAM, the thing that runs out. Counted where the backend
 *     actually calls vkAllocateMemory (for Aura3D, inside VMA).
 *   - @b host: the driver's own CPU-side bookkeeping, counted through
 *     VkAllocationCallbacks. Small, but a leak here is still a leak, and it is
 *     invisible to both the device counters and to AllocationTracker (the
 *     driver uses malloc, not operator new).
 */
struct GpuMemoryStats
{
    bool available = false;

    u64 deviceBytesLive       = 0;  ///< Device memory allocated and not yet freed.
    u64 deviceBytesPeak       = 0;  ///< High-water mark of deviceBytesLive.
    u64 deviceAllocationCount = 0;  ///< Cumulative vkAllocateMemory calls.
    u64 deviceFreeCount       = 0;  ///< Cumulative vkFreeMemory calls.
    u64 deviceBlocksLive      = 0;  ///< Live VkDeviceMemory objects.

    /**
     * @brief Bytes the allocator has handed out of its live blocks.
     *
     * Below @c deviceBytesLive by whatever the suballocator is holding in
     * reserve. The gap is the internal fragmentation of the memory pools, which
     * is a different problem from allocating too much.
     */
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

/**
 * @class IGpuDebugSource
 * @brief Implemented by backends able to report GPU timing and allocations.
 *
 * Queried once per report rather than per frame for the memory counters, and
 * once per frame for the timing -- both accessors are therefore cheap reads of
 * already-collected state, never a device round trip.
 */
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
