#include "aura/Renderer/Vulkan/VkAura/VkDebugMode/VkCountingAllocator.h"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace aura3d {
namespace vk {

namespace {

/**
 * @brief Bookkeeping stored in front of every block this allocator hands out.
 *
 * Vulkan's free callback is given only the pointer -- no size, no alignment --
 * so both have to travel with the block. The same shape as the one in
 * AllocationHooks.cpp, including its size being 16 bytes on a 64-bit target and
 * 12 on a 32-bit one and nothing depending on which; see that file for why the
 * layout is portable.
 */
struct BlockHeader
{
    usize bytes;    ///< Size the driver asked for.
    u32   offset;   ///< Distance from the std::malloc base to the payload.
    u32   reserved; ///< Unused; keeps the two 32-bit fields a matched pair.
};

//! See AllocationHooks.cpp: guards the 32-bit offset field against a header
//! large enough to overflow it.
static_assert(sizeof(BlockHeader) <= std::numeric_limits<u32>::max() / 2,
              "BlockHeader must fit comfortably in the 32-bit offset field");

//! Header loads must be aligned, so the payload alignment is floored at the
//! header's own whatever the driver asked for.
constexpr usize kMinAlign = alignof(BlockHeader);

[[nodiscard]] constexpr usize roundUp(usize value, usize alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] BlockHeader* headerOf(void* payload) noexcept
{
    return reinterpret_cast<BlockHeader*>(static_cast<std::byte*>(payload) - sizeof(BlockHeader));
}

/*
 * Constant-initialised and never destroyed; see the class note for why that
 * matters more here than tidiness.
 */
constinit VkCountingAllocator g_hostAllocator{};

/*
 * The trampolines Vulkan actually calls. pUserData is deliberately null: with
 * one process-wide allocator there is nothing to carry, and a null pUserData is
 * one fewer pointer that can be stale in a driver's copy of the callbacks.
 */

void* VKAPI_PTR onAllocation(void*, usize size, usize alignment, VkSystemAllocationScope) noexcept
{
    //! Vulkan permits a zero-size allocation and requires nullptr back for it,
    //! which is the one case where nullptr is not an out-of-memory report.
    if (size == 0)
        return nullptr;

    return VkCountingAllocator::get().allocate(size, alignment);
}

void VKAPI_PTR onFree(void*, void* memory) noexcept
{
    VkCountingAllocator::get().free(memory);
}

void* VKAPI_PTR onReallocation(void* userData, void* original, usize size, usize alignment,
                               VkSystemAllocationScope scope) noexcept
{
    VkCountingAllocator& allocator = VkCountingAllocator::get();

    //! The two degenerate cases the spec defines in terms of the other
    //! callbacks, handled first so the copy below always has both blocks.
    if (original == nullptr)
        return onAllocation(userData, size, alignment, scope);

    if (size == 0)
    {
        allocator.free(original);
        return nullptr;
    }

    void* replacement = allocator.allocate(size, alignment);
    if (replacement == nullptr)
    {
        //! Contract: on failure the original block must survive untouched.
        return nullptr;
    }

    const usize previousSize = headerOf(original)->bytes;
    std::memcpy(replacement, original, previousSize < size ? previousSize : size);

    allocator.free(original);

    return replacement;
}

void VKAPI_PTR onInternalAllocation(void*, usize size, VkInternalAllocationType,
                                    VkSystemAllocationScope) noexcept
{
    VkCountingAllocator::get().recordInternalAllocation(size);
}

void VKAPI_PTR onInternalFree(void*, usize size, VkInternalAllocationType,
                              VkSystemAllocationScope) noexcept
{
    VkCountingAllocator::get().recordInternalFree(size);
}

constinit VkAllocationCallbacks g_callbacks{
    .pUserData             = nullptr,
    .pfnAllocation         = &onAllocation,
    .pfnReallocation       = &onReallocation,
    .pfnFree               = &onFree,
    .pfnInternalAllocation = &onInternalAllocation,
    .pfnInternalFree       = &onInternalFree,
};

} // namespace

VkCountingAllocator& VkCountingAllocator::get() noexcept
{
    return g_hostAllocator;
}

const VkAllocationCallbacks* VkCountingAllocator::callbacks() noexcept
{
    return &g_callbacks;
}

VkHostAllocationStats VkCountingAllocator::snapshot() const noexcept
{
    VkHostAllocationStats stats;

    stats.liveBytes         = _liveBytes.load(std::memory_order_relaxed);
    stats.peakBytes         = _peakBytes.load(std::memory_order_relaxed);
    stats.allocationCount   = _allocationCount.load(std::memory_order_relaxed);
    stats.freeCount         = _freeCount.load(std::memory_order_relaxed);
    stats.internalBytesLive = _internalBytesLive.load(std::memory_order_relaxed);

    return stats;
}

void VkCountingAllocator::reset() noexcept
{
    _liveBytes.store(0, std::memory_order_relaxed);
    _peakBytes.store(0, std::memory_order_relaxed);
    _allocationCount.store(0, std::memory_order_relaxed);
    _freeCount.store(0, std::memory_order_relaxed);
    _internalBytesLive.store(0, std::memory_order_relaxed);
}

void VkCountingAllocator::recordPeak(u64 live) noexcept
{
    u64 peak = _peakBytes.load(std::memory_order_relaxed);
    while (peak < live &&
           !_peakBytes.compare_exchange_weak(peak, live,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed))
    {
        //! compare_exchange_weak refreshed `peak`; re-test against `live`.
    }
}

void* VkCountingAllocator::allocate(usize size, usize alignment) noexcept
{
    const usize align = alignment < kMinAlign ? kMinAlign : alignment;

    //! Worst case the payload lands sizeof(BlockHeader) + align - 1 past the
    //! base, so that much has to be reserved on top of the payload itself.
    const usize overhead = sizeof(BlockHeader) + align - 1;
    if (size > std::numeric_limits<usize>::max() - overhead)
        return nullptr;

    auto* base = static_cast<std::byte*>(std::malloc(size + overhead));
    if (base == nullptr)
        return nullptr;

    auto* payload = reinterpret_cast<std::byte*>(
        roundUp(reinterpret_cast<usize>(base) + sizeof(BlockHeader), align));

    const auto offset = static_cast<usize>(payload - base);
    if (offset > std::numeric_limits<u32>::max())
    {
        std::free(base);
        return nullptr;
    }

    BlockHeader* header = headerOf(payload);
    header->bytes    = size;
    header->offset   = static_cast<u32>(offset);
    header->reserved = 0;

    _allocationCount.fetch_add(1, std::memory_order_relaxed);
    recordPeak(_liveBytes.fetch_add(size, std::memory_order_relaxed) + size);

    return payload;
}

void VkCountingAllocator::free(void* memory) noexcept
{
    if (memory == nullptr)
        return;

    BlockHeader* header = headerOf(memory);

    _freeCount.fetch_add(1, std::memory_order_relaxed);
    _liveBytes.fetch_sub(header->bytes, std::memory_order_relaxed);

    std::free(static_cast<std::byte*>(memory) - header->offset);
}

void VkCountingAllocator::recordInternalAllocation(usize size) noexcept
{
    /*
     * Purely a notification: the driver has already allocated this by other
     * means and is telling us so. Nothing to return and nothing to own -- only
     * a number worth having, because it is part of the process's footprint and
     * appears in no other counter here.
     */
    _internalBytesLive.fetch_add(size, std::memory_order_relaxed);
}

void VkCountingAllocator::recordInternalFree(usize size) noexcept
{
    _internalBytesLive.fetch_sub(size, std::memory_order_relaxed);
}

} // namespace vk
} // namespace aura3d
