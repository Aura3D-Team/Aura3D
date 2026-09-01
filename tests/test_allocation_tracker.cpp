// AllocationTracker: the CPU allocation counters behind debug mode.
//
// Two halves, because the class and the global operator new/delete hooks that
// feed it are compiled independently (see AllocationTracker.h), and each half
// can only be asserted on in its own way:
//
//   - The counter arithmetic and the size-class mapping are checked on a
//     *private* AllocationTracker instance. The class has a public constexpr
//     constructor precisely because it is an ordinary object; only the hooks
//     insist on the singleton. An instance nothing else in the process can
//     reach makes these checks exact, and they run in every build.
//   - The hooks have no such luxury: they feed the singleton, and this binary
//     links libink and libwma, whose threads allocate on their own schedule.
//     Exact equality against a process-wide counter would be a CI flake waiting
//     to happen, so those cases assert bounds -- wide enough to ignore a few
//     hundred bytes of concurrent noise, far narrower than the 1 MiB block they
//     are looking for. They are additionally guarded on
//     AllocationTracker::isHooked(), which reports the skip in a default build
//     rather than passing silently.
//
// Measurements are taken first and the checks run afterwards either way:
// AURA_CHECK writes to stdout, which allocates.

#include <cstddef>
#include <memory>

#include "aura/Core/DebugMode/AllocationTracker.h"

#include "TestUtils.h"

using namespace aura3d;

namespace {

void testDirectCounters()
{
    // Private instance: no hook feeds it and no other thread can reach it, so
    // every number below is exact rather than a delta against process noise.
    AllocationTracker tracker;

    tracker.recordAllocation(1024);
    tracker.recordAllocation(64);
    tracker.recordFree(1024);

    const AllocationStats stats = tracker.snapshot();

    AURA_CHECK(stats.allocationCount == 2, "counters: two allocations recorded");
    AURA_CHECK(stats.freeCount == 1, "counters: one free recorded");
    AURA_CHECK(stats.totalAllocatedBytes == 1088, "counters: 1024 + 64 bytes allocated");
    AURA_CHECK(stats.totalFreedBytes == 1024, "counters: 1024 bytes freed");
    AURA_CHECK(stats.liveBytes == 64, "counters: 64 bytes still live");

    // The peak is a high-water mark, so it remembers the moment before the
    // free rather than tracking liveBytes back down.
    AURA_CHECK(stats.peakBytes == 1088, "counters: peak covers both allocations before the free");

    // The 1024-byte allocation belongs to class 10 and the 64-byte one to
    // class 6; the free must not move either.
    AURA_CHECK(stats.sizeClasses[10] == 1, "counters: one allocation filed under the 1 KiB size class");
    AURA_CHECK(stats.sizeClasses[6] == 1, "counters: one allocation filed under the 64 B size class");
    AURA_CHECK(stats.sizeClasses[0] == 0, "counters: unused size classes stay empty");
}

void testReset()
{
    AllocationTracker tracker;

    tracker.recordAllocation(4096);
    tracker.recordAllocation(8);
    tracker.reset();

    const AllocationStats stats = tracker.snapshot();

    AURA_CHECK(stats.liveBytes == 0 && stats.peakBytes == 0,
               "reset: byte counters return to zero");
    AURA_CHECK(stats.allocationCount == 0 && stats.freeCount == 0,
               "reset: operation counters return to zero");
    AURA_CHECK(stats.totalAllocatedBytes == 0 && stats.totalFreedBytes == 0,
               "reset: cumulative totals return to zero");
    AURA_CHECK(stats.sizeClasses[12] == 0 && stats.sizeClasses[3] == 0,
               "reset: the size histogram is cleared too");
}

void testOutstandingHelpers()
{
    AllocationStats stats;
    stats.totalAllocatedBytes = 5000;
    stats.totalFreedBytes     = 1500;
    stats.allocationCount     = 9;
    stats.freeCount           = 4;

    AURA_CHECK(stats.outstandingBytes() == 3500, "outstanding: 5000 - 1500 bytes");
    AURA_CHECK(stats.outstandingAllocations() == 5, "outstanding: 9 - 4 allocations");

    // Free counts can momentarily overtake allocation counts when a snapshot
    // races an allocating thread; the helpers must clamp rather than wrap into
    // an eighteen-quintillion-byte "leak".
    AllocationStats skewed;
    skewed.totalAllocatedBytes = 10;
    skewed.totalFreedBytes     = 40;
    skewed.allocationCount     = 1;
    skewed.freeCount           = 3;

    AURA_CHECK(skewed.outstandingBytes() == 0,
               "outstanding: a racing snapshot clamps to 0 rather than wrapping");
    AURA_CHECK(skewed.outstandingAllocations() == 0,
               "outstanding: allocation count clamps the same way");
}

void testSizeClassMapping()
{
    AURA_CHECK(AllocationTracker::sizeClassOf(0) == 0, "size class: 0 bytes maps to class 0");
    AURA_CHECK(AllocationTracker::sizeClassOf(1) == 0, "size class: 1 byte maps to class 0");
    AURA_CHECK(AllocationTracker::sizeClassOf(2) == 1, "size class: 2 bytes maps to class 1");
    AURA_CHECK(AllocationTracker::sizeClassOf(3) == 1, "size class: 3 bytes maps to class 1");
    AURA_CHECK(AllocationTracker::sizeClassOf(4) == 2, "size class: 4 bytes maps to class 2");
    AURA_CHECK(AllocationTracker::sizeClassOf(1023) == 9, "size class: 1023 bytes maps to class 9");
    AURA_CHECK(AllocationTracker::sizeClassOf(1024) == 10, "size class: 1 KiB maps to class 10");
    AURA_CHECK(AllocationTracker::sizeClassOf(1025) == 10, "size class: 1 KiB + 1 stays in class 10");

    // Saturating rather than overflowing: the top bucket is open-ended.
    AURA_CHECK(AllocationTracker::sizeClassOf(~usize{0}) == AllocationStats::kSizeClassCount - 1,
               "size class: the largest possible size saturates in the last class");

    AURA_CHECK(AllocationTracker::sizeClassLowerBound(0) == 0,
               "size class bound: class 0 starts at 0");
    AURA_CHECK(AllocationTracker::sizeClassLowerBound(10) == 1024,
               "size class bound: class 10 starts at 1 KiB");
}

void testHooksCountRealAllocations()
{
    AllocationTracker& tracker = AllocationTracker::get();

    constexpr usize kBlockBytes = 1u << 20; // 1 MiB, far above any incidental noise

    if constexpr (!AllocationTracker::isHooked())
    {
        // The counters are still writable through the direct API above -- what
        // is absent without the hooks is anything *feeding* them. Assert
        // exactly that: a real allocation must move nothing.
        const u64 countBefore = tracker.allocationCount();
        const u64 liveBefore  = tracker.liveBytes();

        const auto block = std::make_unique<std::byte[]>(kBlockBytes);
        const u64 countAfter = tracker.allocationCount();
        const u64 liveAfter  = tracker.liveBytes();

        AURA_CHECK(block[0] == std::byte{0}, "hooks: the block is value-initialised");
        AURA_CHECK(countAfter == countBefore && liveAfter == liveBefore,
                   "hooks: not compiled in, so a real allocation moves no counter");
        return;
    }

    const u64 liveBefore   = tracker.liveBytes();
    const u64 countBefore  = tracker.allocationCount();

    u64 liveDuring  = 0;
    u64 countDuring = 0;
    {
        // make_unique rather than a bare new: this exercises operator new[]
        // through the same route engine code would take.
        const auto block = std::make_unique<std::byte[]>(kBlockBytes);
        liveDuring  = tracker.liveBytes();
        countDuring = tracker.allocationCount();

        // Touch it so nothing is optimised away.
        AURA_CHECK(block[0] == std::byte{0}, "hooks: the block is value-initialised");
    }

    const u64 liveAfter = tracker.liveBytes();

    AURA_CHECK(liveDuring - liveBefore >= kBlockBytes,
               "hooks: live bytes rose by at least the block size");
    AURA_CHECK(countDuring > countBefore,
               "hooks: the allocation count rose");
    AURA_CHECK(liveAfter <= liveBefore + (kBlockBytes / 2),
               "hooks: live bytes came back down when the block was freed");
}

void testScopedMuteExcludesItsOwnAllocations()
{
    AllocationTracker& tracker = AllocationTracker::get();

    if constexpr (!AllocationTracker::isHooked())
    {
        AURA_CHECK(true, "mute: not compiled in, case skipped");
        return;
    }

    constexpr usize kBlockBytes = 1u << 20;

    //! Half the block. Wide enough to absorb whatever libink's and libwma's
    //! threads allocate in the microseconds this takes, and far narrower than
    //! the megabyte a broken mute would let through.
    constexpr i64 kNoiseMargin = static_cast<i64>(kBlockBytes / 2);

    const u64 liveBefore = tracker.liveBytes();

    // The reason ScopedMute exists: the report writer allocates, and counting
    // that would mean the act of measuring moved the number.
    std::unique_ptr<std::byte[]> block;
    {
        const AllocationTracker::ScopedMute mute;
        block = std::make_unique<std::byte[]>(kBlockBytes);
    }

    const u64 liveDuring = tracker.liveBytes();

    // The other half of the invariant, and the one that would otherwise walk
    // the counter downwards forever: a block allocated muted is freed unmuted
    // here, and must not be subtracted either. That is what the per-block
    // `counted` flag in AllocationHooks.cpp exists to guarantee.
    const u64 liveBeforeFree = tracker.liveBytes();
    block.reset();
    const u64 liveAfterFree = tracker.liveBytes();

    const i64 duringDelta = static_cast<i64>(liveDuring) - static_cast<i64>(liveBefore);
    const i64 freeDelta   = static_cast<i64>(liveAfterFree) - static_cast<i64>(liveBeforeFree);

    AURA_CHECK(duringDelta < kNoiseMargin,
               "mute: a muted allocation is not counted");
    AURA_CHECK(freeDelta > -kNoiseMargin,
               "mute: freeing a muted block outside the mute does not decrement either");
}

} // namespace

int main()
{
    testDirectCounters();
    testReset();
    testOutstandingHelpers();
    testSizeClassMapping();
    testHooksCountRealAllocations();
    testScopedMuteExcludesItsOwnAllocations();

    AURA_TEST_MAIN_RETURN();
}
