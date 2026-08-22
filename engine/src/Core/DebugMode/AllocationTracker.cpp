#include "aura/Core/DebugMode/AllocationTracker.h"

namespace aura3d {
namespace {
constinit AllocationTracker g_tracker{};

} // namespace

AllocationTracker& AllocationTracker::get() noexcept
{
    return g_tracker;
}

void AllocationTracker::recordAllocation(usize bytes) noexcept
{
    const auto amount = static_cast<u64>(bytes);

    _totalAllocatedBytes.fetch_add(amount, std::memory_order_relaxed);
    _allocationCount.fetch_add(1, std::memory_order_relaxed);
    _sizeClasses[sizeClassOf(bytes)].fetch_add(1, std::memory_order_relaxed);

    const u64 live = _liveBytes.fetch_add(amount, std::memory_order_relaxed) + amount;

    u64 peak = _peakBytes.load(std::memory_order_relaxed);
    while (peak < live &&
           !_peakBytes.compare_exchange_weak(peak, live,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed))
    {
    }
}

void AllocationTracker::recordFree(usize bytes) noexcept
{
    const auto amount = static_cast<u64>(bytes);

    _totalFreedBytes.fetch_add(amount, std::memory_order_relaxed);
    _freeCount.fetch_add(1, std::memory_order_relaxed);
    _liveBytes.fetch_sub(amount, std::memory_order_relaxed);
}

AllocationStats AllocationTracker::snapshot() const noexcept
{
    AllocationStats stats;

    stats.liveBytes           = _liveBytes.load(std::memory_order_relaxed);
    stats.peakBytes           = _peakBytes.load(std::memory_order_relaxed);
    stats.totalAllocatedBytes = _totalAllocatedBytes.load(std::memory_order_relaxed);
    stats.totalFreedBytes     = _totalFreedBytes.load(std::memory_order_relaxed);
    stats.allocationCount     = _allocationCount.load(std::memory_order_relaxed);
    stats.freeCount           = _freeCount.load(std::memory_order_relaxed);

    for (usize i = 0; i < AllocationStats::kSizeClassCount; ++i)
        stats.sizeClasses[i] = _sizeClasses[i].load(std::memory_order_relaxed);

    return stats;
}

void AllocationTracker::reset() noexcept
{
    _liveBytes.store(0, std::memory_order_relaxed);
    _peakBytes.store(0, std::memory_order_relaxed);
    _totalAllocatedBytes.store(0, std::memory_order_relaxed);
    _totalFreedBytes.store(0, std::memory_order_relaxed);
    _allocationCount.store(0, std::memory_order_relaxed);
    _freeCount.store(0, std::memory_order_relaxed);

    for (std::atomic<u64>& bucket : _sizeClasses)
        bucket.store(0, std::memory_order_relaxed);
}

} // namespace aura3d
