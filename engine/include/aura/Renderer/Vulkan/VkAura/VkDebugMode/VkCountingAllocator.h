#ifndef VKCOUNTINGALLOCATOR_H
#define VKCOUNTINGALLOCATOR_H

#pragma once

#include <atomic>

#include <vulkan/vulkan.h>

#include "aura/Core/AuraCore.h"

/**
 * @file VkCountingAllocator.h
 * @brief A VkAllocationCallbacks that counts the driver's host allocations.
 *
 * @warning This measures *host* memory -- the CPU-side bookkeeping a Vulkan
 * driver keeps for the objects it creates -- and not VRAM. The name
 * VkAllocationCallbacks suggests otherwise often enough to be worth stating
 * plainly: passing this to vkAllocateMemory tells you how many bytes of malloc
 * the driver needed to *describe* a device allocation, never how large the
 * device allocation itself was. Device memory is counted separately, from VMA's
 * device-memory callbacks -- see VkDeviceMemoryCounters in VkDebugMetrics.h.
 *
 * It is still worth counting. Driver host memory is invisible to
 * aura3d::AllocationTracker (the driver uses malloc, not operator new) and to
 * every device-memory tool, so a leak of it shows up in nothing but RSS.
 */

namespace aura3d {
namespace vk {

/**
 * @struct VkHostAllocationStats
 * @brief Snapshot of the host allocation counters.
 */
struct VkHostAllocationStats
{
    u64 liveBytes       = 0;
    u64 peakBytes       = 0;
    u64 allocationCount = 0;
    u64 freeCount       = 0;

    //! Memory the driver reports allocating outside these callbacks
    //! (pfnInternalAllocation), which it neither asks us for nor lets us own.
    //! Informational: already part of the process's RSS, counted nowhere else.
    u64 internalBytesLive = 0;
};

/**
 * @class VkCountingAllocator
 * @brief Process-wide host allocator handed to Vulkan as @c pAllocator.
 *
 * ## Why a singleton
 *
 * Vulkan's rule is that a handle created with a @c pAllocator must be destroyed
 * with a *compatible* one, so the allocator has to outlive every object ever
 * passed to it -- including the VkInstance, which is torn down from
 * VulkanRenderer::cleanup() and could in principle run during static
 * destruction. A constant-initialised object that is never destroyed makes that
 * requirement unconditionally true instead of a lifetime argument that has to
 * be re-checked whenever ownership moves.
 *
 * It also removes the plumbing problem: VkInstanceManager and VkDeviceManager
 * own their own create/destroy pairs and have no path to a renderer member.
 *
 * Thread-safe. Vulkan may call the callbacks from any thread, so the counters
 * are relaxed atomics, exactly as in AllocationTracker.
 */
class VkCountingAllocator
{
public:
    constexpr VkCountingAllocator() noexcept = default;

    VkCountingAllocator(const VkCountingAllocator&)            = delete;
    VkCountingAllocator& operator=(const VkCountingAllocator&) = delete;

    [[nodiscard]] static VkCountingAllocator& get() noexcept;

    /**
     * @brief The callbacks structure to pass as @c pAllocator.
     *
     * @return Points at storage with static lifetime, so it is safe to hand to
     *         any Vulkan create call. Never null.
     */
    [[nodiscard]] static const VkAllocationCallbacks* callbacks() noexcept;

    [[nodiscard]] VkHostAllocationStats snapshot() const noexcept;

    //! For tests and for discarding load-time noise before a measured window.
    void reset() noexcept;

    /// @name Callback bodies
    /// Public because the free-function trampolines in the .cpp call them; not
    /// part of the interface a caller uses.
    /// @{

    [[nodiscard]] void* allocate(usize size, usize alignment) noexcept;
    void free(void* memory) noexcept;
    void recordInternalAllocation(usize size) noexcept;
    void recordInternalFree(usize size) noexcept;

    /// @}

private:
    void recordPeak(u64 live) noexcept;

    std::atomic<u64> _liveBytes{0};
    std::atomic<u64> _peakBytes{0};
    std::atomic<u64> _allocationCount{0};
    std::atomic<u64> _freeCount{0};
    std::atomic<u64> _internalBytesLive{0};
};

/**
 * @brief The @c pAllocator this engine's Vulkan create/destroy pairs pass.
 *
 * Returns nullptr without AURA_ENABLE_DEBUG_MODE, which is Vulkan's "use the
 * driver's own allocator" and precisely what those call sites passed before.
 * That is the point: the call sites stay unconditional, so the compiler can see
 * that a create and its matching destroy use the same expression -- which is
 * the one invariant a custom Vulkan allocator can violate catastrophically, and
 * the one an `#ifdef` at each site would put at the mercy of an edit.
 */
[[nodiscard]] inline const VkAllocationCallbacks* hostAllocationCallbacks() noexcept
{
#ifdef AURA_ENABLE_DEBUG_MODE
    return VkCountingAllocator::callbacks();
#else
    return nullptr;
#endif
}

} // namespace vk
} // namespace aura3d

#endif // VKCOUNTINGALLOCATOR_H
