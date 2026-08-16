#ifndef VKTIMESTAMPQUERY_H
#define VKTIMESTAMPQUERY_H

#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "aura/Core/AuraCore.h"
#include "aura/Core/DebugMode/GpuDebugStats.h"

/**
 * @file VkTimestampQuery.h
 * @brief GPU-side frame timing through a VkQueryPool of timestamps.
 *
 * FrameProfiler measures CPU wall time, which on a GPU backend answers a
 * different question than it appears to: `Submit` is how long vkQueueSubmit
 * took to hand work over, not how long the work took, and a frame that is
 * entirely GPU-bound shows up as time spent waiting on a fence somewhere
 * else entirely. Two timestamps written into the frame's own command buffer are
 * the only way to get the other half of that.
 *
 * ## Why the results trail the CPU
 *
 * Reading a query for the frame just recorded means waiting for the GPU to
 * finish it -- a full pipeline stall, and a measurement of the stall rather
 * than of the frame. Instead each frame slot's results are read at the top of
 * its *next* use, immediately after the fence wait the render loop already
 * performs, at which point the values are guaranteed available and the read
 * costs nothing. The reported timing therefore trails the CPU by the number of
 * frames in flight, which for a benchmark averaging thousands of frames is not
 * a distinction that matters.
 */

namespace aura3d {
namespace vk {

/**
 * @class VkTimestampQuery
 * @brief One timestamp pair per frame slot, resolved a frame in arrears.
 *
 * RAII over the VkQueryPool, matching the rest of VkAura: destroy() is
 * idempotent and the destructor calls it, so an exception on the way up
 * through VulkanRenderer's initialisation cannot leak the pool.
 */
class VkTimestampQuery
{
public:
    VkTimestampQuery() noexcept = default;
    ~VkTimestampQuery();

    VkTimestampQuery(const VkTimestampQuery&)            = delete;
    VkTimestampQuery& operator=(const VkTimestampQuery&) = delete;

    /**
     * @brief Creates a pool of 2 * @p frameSlots timestamps.
     *
     * @param device            Logical device the pool belongs to.
     * @param physicalDevice    Queried for @c timestampPeriod and support.
     * @param queueFamilyIndex  Family the timestamps will be written on; its
     *                          @c timestampValidBits decides the result mask.
     * @param frameSlots        Frames in flight, i.e. the number of slots.
     *
     * @return false when the device cannot timestamp on this queue family, or
     *         pool creation failed. Not an error: the caller carries on with
     *         GPU timing simply reported as unavailable.
     */
    [[nodiscard]] bool initialize(VkDevice device,
                                  VkPhysicalDevice physicalDevice,
                                  u32 queueFamilyIndex,
                                  u32 frameSlots) noexcept;

    //! Destroys the pool. Safe to call twice, and on an uninitialised object.
    void destroy() noexcept;

    [[nodiscard]] bool isReady() const noexcept { return _queryPool != VK_NULL_HANDLE; }

    /**
     * @brief Resets @p frameSlot's two queries and writes the opening timestamp.
     *
     * Must be recorded into an open primary command buffer, before any of the
     * frame's real work. The reset is part of this call because a query must be
     * reset before it is written and doing both here makes the pairing with
     * writeEnd() the only ordering a caller has to get right.
     */
    void writeBegin(VkCommandBuffer commandBuffer, u32 frameSlot) noexcept;

    //! Writes the closing timestamp for @p frameSlot, after the frame's work.
    void writeEnd(VkCommandBuffer commandBuffer, u32 frameSlot) noexcept;

    /**
     * @brief Reads back @p frameSlot's completed pair, if there is one.
     *
     * Call only once the slot's fence has been waited on -- the render loop
     * already does that at the top of every frame, which is where this belongs.
     * Non-blocking: a slot whose results are not ready is counted as a dropped
     * sample rather than waited for.
     */
    void resolve(u32 frameSlot) noexcept;

    //! The most recent successfully resolved frame time, and the drop count.
    [[nodiscard]] GpuTimingStats stats() const noexcept;

private:
    VkDevice   _device    = VK_NULL_HANDLE;
    VkQueryPool _queryPool = VK_NULL_HANDLE;

    //! Nanoseconds per timestamp tick, from VkPhysicalDeviceLimits.
    f64 _nanosPerTick = 0.0;

    /**
     * @brief Mask for the bits of a timestamp the queue actually writes.
     *
     * A queue family reports @c timestampValidBits, and the bits above that are
     * undefined rather than zero -- subtracting two unmasked values on a device
     * reporting 36 valid bits gives noise, not a duration.
     */
    u64 _validBitsMask = 0;

    //! Per slot: has a pair been written that has not yet been resolved? A
    //! slot's queries are unwritten on the first pass and after a device loss,
    //! and reading those back is what the availability bit exists to prevent.
    std::vector<u8> _slotPending;

    f64 _lastFrameMillis  = 0.0;
    u64 _resolvedSamples  = 0;
    u64 _droppedSamples   = 0;
};

} // namespace vk
} // namespace aura3d

#endif // VKTIMESTAMPQUERY_H
