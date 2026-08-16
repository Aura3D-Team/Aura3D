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
 * The counters themselves are always compiled; what AURA_ENABLE_DEBUG_MODE
 * switches on is the global operator new/delete pair in AllocationHooks.cpp
 * that feeds them. Splitting it that way keeps the tracker unit-testable in a
 * default build (a test can drive recordAllocation()/recordFree() directly)
 * while a release build still contains no allocation hook, no branch on the
 * malloc path and nothing referencing this at all.
 *
 * Ask isHooked() rather than assuming: with the hooks compiled out, every
 * counter here reads zero forever, and a report that silently presented that as
 * "no allocations" would be worse than one that says it did not measure.
 */

namespace aura3d {

/**
 * @struct AllocationStats
 * @brief A consistent-enough snapshot of the allocation counters.
 *
 * @note Fields are read from independent relaxed atomics, so a snapshot taken
 *       while other threads are allocating can be internally inconsistent by a
 *       few allocations (liveBytes not exactly totalAllocatedBytes -
 *       totalFreedBytes, say). Locking to close that window would put a mutex
 *       on every malloc in the process to make a debug report tidier, which is
 *       not a trade worth making.
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

    /**
     * @brief Histogram of allocation sizes by power-of-two class.
     *
     * Cheap (one count-leading-zeros and one increment per allocation) and it
     * answers the question the byte totals cannot: two runs that allocate the
     * same number of megabytes behave nothing alike if one does it in a
     * thousand large blocks and the other in ten million 32-byte ones.
     */
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
 * A singleton with a constexpr constructor and constant initialisation (see
 * AllocationTracker.cpp), not a function-local static: the hooks that feed it
 * run before main() during dynamic initialisation of other translation units
 * and after main() during their destruction, and a function-local static is
 * guaranteed to exist for neither.
 */
class AllocationTracker
{
public:
    //! Constant-initialised at load time; see the class note.
    constexpr AllocationTracker() noexcept = default;

    AllocationTracker(const AllocationTracker&)            = delete;
    AllocationTracker& operator=(const AllocationTracker&) = delete;

    [[nodiscard]] static AllocationTracker& get() noexcept;

    /**
     * @brief True when the global operator new/delete hooks were compiled in.
     *
     * Constant-folds to the build's answer: with AURA_ENABLE_DEBUG_MODE off the
     * counters are never written and every report should say so rather than
     * present zeroes as a measurement.
     */
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

    /**
     * @brief Zeroes every counter.
     *
     * For tests and for discarding startup noise before a measured window.
     * Racy by nature -- allocations in flight on other threads land on either
     * side of it -- so call it from a quiescent point.
     */
    void reset() noexcept;

    /**
     * @brief The size class recordAllocation() would file @p bytes under.
     *
     * floor(log2(bytes)), saturating at the last bucket; zero-byte allocations
     * (which operator new must still honour) go in bucket 0.
     */
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

    /**
     * @brief Inclusive lower bound in bytes of size class @p klass.
     *
     * The report writer's counterpart to sizeClassOf(), so a histogram bucket
     * can be labelled with the range it covers rather than an index.
     */
    [[nodiscard]] static constexpr u64 sizeClassLowerBound(usize klass) noexcept
    {
        return klass == 0 ? 0 : (u64{1} << klass);
    }

    /**
     * @class ScopedMute
     * @brief Suspends tracking on the calling thread for the enclosing block.
     *
     * The report writer allocates -- a JSON document, a sorted copy of the
     * sample buffer -- and counting that against the run being measured would
     * mean the act of reporting the numbers changed them. Every allocation the
     * debug subsystem makes on its own behalf goes inside one of these.
     *
     * Thread-local and reentrant, so a muted region may call into anything.
     *
     * @note The signal fences are load-bearing, not decoration. A compiler is
     *       entitled to treat the replaceable global @c operator new as
     *       malloc-like -- allocating storage and reading nothing -- and GCC at
     *       @c -O3 duly hoists an inlined allocation above a plain store to
     *       this flag, which silently un-mutes the region. Both fences are pure
     *       compiler barriers: they emit no instruction and cost nothing at
     *       runtime, they simply forbid that reordering in either direction.
     *       Verified: without them, a 1 MiB allocation inside a mute is counted
     *       at @c -O3 and not at @c -O2.
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

    /**
     * @brief The calling thread's mute flag, as an lvalue.
     *
     * A function-local thread_local bool with constant initialisation: no
     * guard variable and no dynamic init, which matters because the allocation
     * hooks read it and anything that allocated to answer would recurse.
     */
    [[nodiscard]] static bool& muted() noexcept
    {
        static thread_local bool flag = false;
        return flag;
    }

private:
    /*
     * Relaxed atomics throughout. Every one of these is written on the malloc
     * path of every thread in the process, so the ordering is chosen to make
     * that path as close to a plain increment as the ISA allows; nothing is
     * published through them, only counted.
     */
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
