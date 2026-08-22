#ifndef AURA_ALLOCATIONTRACKER_H
#define AURA_ALLOCATIONTRACKER_H

#pragma once

#include <array>
#include <atomic>

#include "aura/aura.h"

/**
 * @file AllocationTracker.h
 * @brief Process-wide CPU allocation counters.
 *
 * The counters are always compiled; AURA_ENABLE_DEBUG_MODE only switches on
 * the global operator new/delete hooks (AllocationHooks.cpp) that feed them,
 * so a test can drive recordAllocation()/recordFree() directly without that
 * build flag, and a release build has no allocation hook at all.
 *
 * Check isHooked() rather than assuming: with hooks compiled out, every
 * counter reads zero forever, which a report must say explicitly rather than
 * present as "no allocations".
 */

namespace aura3d {

/**
 * @struct AllocationStats
 * @brief A consistent-enough snapshot of the allocation counters.
 *
 * @note Fields are independent relaxed atomics, so a snapshot taken during
 *       concurrent allocation can be off by a few (liveBytes not exactly
 *       totalAllocatedBytes - totalFreedBytes). Not locked: that would put a
 *       mutex on every malloc in the process just to tidy a debug report.
 */
struct AllocationStats
{
    //! Power-of-two size classes: bucket i holds allocations of
    //! [2^i, 2^(i+1)) bytes, with the last bucket catching everything larger.
    static constexpr usize kSizeClassCount = 32;

    u64 liveBytes            = 0;  ///< Allocated and not yet freed.
    u64 peakBytes            = 0;  ///< High-water mark of liveBytes.
    u64 totalAllocatedBytes  = 0;  ///< Cumulative, never decreases.
    u64 totalFreedBytes      = 0;  ///< Cumulative, never decreases.
    u64 allocationCount      = 0;
    u64 freeCount            = 0;

    //! Histogram of allocation sizes by power-of-two class. Distinguishes runs
    //! the byte totals can't: the same total in a thousand large blocks vs. ten
    //! million tiny ones behave nothing alike.
    std::array<u64, kSizeClassCount> sizeClasses{};

    //! Bytes allocated and never freed. Zero for a balanced run.
    [[nodiscard]] constexpr u64 outstandingBytes() const noexcept
    {
        return totalAllocatedBytes >= totalFreedBytes
                   ? totalAllocatedBytes - totalFreedBytes
                   : 0;
    }

    //! Allocations that were never matched by a free.
    [[nodiscard]] constexpr u64 outstandingAllocations() const noexcept
    {
        return allocationCount >= freeCount ? allocationCount - freeCount : 0;
    }
};

/**
 * @class AllocationTracker
 * @brief Thread-safe, lock-free counters behind the global allocation hooks.
 *
 * A singleton with a constexpr constructor (constant-initialised, not a
 * function-local static): the hooks that feed it can run before main() during
 * other translation units' dynamic initialisation, where a function-local
 * static is not guaranteed to exist yet.
 */
class AllocationTracker
{
public:
    //! Constant-initialised at load time; see the class note.
    constexpr AllocationTracker() noexcept = default;

    AllocationTracker(const AllocationTracker&)            = delete;
    AllocationTracker& operator=(const AllocationTracker&) = delete;

    [[nodiscard]] static AllocationTracker& get() noexcept;

    /// True when the global operator new/delete hooks were compiled in.
    /// Constant-folds to the build's answer.
    [[nodiscard]] static constexpr bool isHooked() noexcept
    {
#ifdef AURA_ENABLE_DEBUG_MODE
        return true;
#else
        return false;
#endif
    }

    /// @name Hot path
    /// Called from the global allocation hooks, so both are lock-free, relaxed
    /// and branch-light. Relaxed ordering is sufficient: these are counters, and
    /// nothing is published through them.
    /// @{

    void recordAllocation(usize bytes) noexcept;
    void recordFree(usize bytes) noexcept;

    /// @}

    //! A snapshot of every counter. See AllocationStats for its consistency limits.
    [[nodiscard]] AllocationStats snapshot() const noexcept;

    /// @name Single-counter reads
    /// For the per-frame sampling path, which wants two numbers rather than the
    /// thirty-eight loads a full snapshot() costs.
    /// @{

    [[nodiscard]] u64 liveBytes() const noexcept
    {
        return _liveBytes.load(std::memory_order_relaxed);
    }

    [[nodiscard]] u64 allocationCount() const noexcept
    {
        return _allocationCount.load(std::memory_order_relaxed);
    }

    /// @}

    /// Zeroes every counter, for tests and for discarding startup noise. Racy
    /// by nature (in-flight allocations on other threads); call from a
    /// quiescent point.
    void reset() noexcept;

    /// The size class recordAllocation() would file @p bytes under:
    /// floor(log2(bytes)), saturating at the last bucket; 0 bytes -> bucket 0.
    [[nodiscard]] static constexpr usize sizeClassOf(usize bytes) noexcept
    {
        usize klass = 0;
        while (bytes > 1 && klass + 1 < AllocationStats::kSizeClassCount)
        {
            bytes >>= 1;
            ++klass;
        }
        return klass;
    }

    /// Inclusive lower bound in bytes of size class @p klass. The report
    /// writer's counterpart to sizeClassOf(), for labelling a histogram bucket.
    [[nodiscard]] static constexpr u64 sizeClassLowerBound(usize klass) noexcept
    {
        return klass == 0 ? 0 : (u64{1} << klass);
    }

    /**
     * @class ScopedMute
     * @brief Suspends tracking on the calling thread for the enclosing block.
     *
     * Wrap every allocation the debug subsystem makes on its own behalf (e.g.
     * building the report), so measuring does not itself change the numbers.
     * Thread-local and reentrant.
     *
     * @warning Do not remove the signal fences. GCC at -O3 can hoist an
     *          inlined allocation above the plain store to the mute flag,
     *          silently un-muting the region -- verified: without the fences,
     *          a 1 MiB allocation inside a mute is counted at -O3 and not -O2.
     *          The fences are pure compiler barriers (zero runtime cost).
     */
    class ScopedMute
    {
    public:
        ScopedMute() noexcept : _previous(muted())
        {
            muted() = true;
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }

        ~ScopedMute()
        {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            muted() = _previous;
        }

        ScopedMute(const ScopedMute&)            = delete;
        ScopedMute& operator=(const ScopedMute&) = delete;

    private:
        bool _previous;
    };

    /// The calling thread's mute flag, as an lvalue. Constant-initialised
    /// (no guard variable, no dynamic init) since the allocation hooks read
    /// it and any allocation to answer would recurse.
    [[nodiscard]] static bool& muted() noexcept
    {
        static thread_local bool flag = false;
        return flag;
    }

private:
    //! Relaxed atomics throughout: written on every thread's malloc path, so
    //! ordering is kept as close to a plain increment as the ISA allows.
    std::atomic<u64> _liveBytes{0};
    std::atomic<u64> _peakBytes{0};
    std::atomic<u64> _totalAllocatedBytes{0};
    std::atomic<u64> _totalFreedBytes{0};
    std::atomic<u64> _allocationCount{0};
    std::atomic<u64> _freeCount{0};
    std::array<std::atomic<u64>, AllocationStats::kSizeClassCount> _sizeClasses{};
};

} // namespace aura3d

#endif // AURA_ALLOCATIONTRACKER_H
