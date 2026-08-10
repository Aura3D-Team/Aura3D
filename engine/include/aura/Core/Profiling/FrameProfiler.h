#ifndef AURA_FRAMEPROFILER_H
#define AURA_FRAMEPROFILER_H

#pragma once

#include <array>
#include <chrono>

#include "aura/aura.h"

/**
 * @file FrameProfiler.h
 * @brief Per-phase frame timing for the render loop.
 *
 * Exists because the interesting question in a render loop is almost never
 * "how long is a frame" but "which phase owns it", and the two most expensive
 * phases here -- waiting on a fence and presenting -- cost nothing in CPU
 * cycles while dominating wall time. A sampling profiler under-reports exactly
 * those; explicit scopes do not.
 *
 * Compiled out entirely unless AURA_PROFILE_FRAME is defined (see the CMake
 * option of the same name): with it off, AURA_FRAME_SCOPE expands to nothing
 * and FrameProfiler::report() is an empty inline function, so a release build
 * carries no clock reads, no counters and no branch.
 */

namespace aura3d {

/**
 * @brief Phases of one frame, in the order the render loop executes them.
 *
 * Kept deliberately coarse: each entry is a phase you could act on. Splitting
 * finer would cost more clock reads than the phases it separates.
 */
enum class FramePhase : u32
{
    WaitFence = 0,  ///< Blocking on the previous submission for this frame slot.
    Acquire,        ///< vkAcquireNextImageKHR (swapchain/WSI).
    BeginPass,      ///< Command buffer begin + render pass begin.
    RecordScene,    ///< drawMeshes(): resolving and recording scene draws.
    RecordOverlay,  ///< The 2D overlay batch (text, UI).
    EndPass,        ///< Replaying secondaries + ending the pass/buffer.
    Submit,         ///< vkQueueSubmit.
    Present,        ///< vkQueuePresentKHR (hands the frame to the compositor).
    COUNT
};

[[nodiscard]] constexpr const char* toString(FramePhase phase) noexcept
{
    switch (phase)
    {
        case FramePhase::WaitFence:     return "WaitFence";
        case FramePhase::Acquire:       return "Acquire";
        case FramePhase::BeginPass:     return "BeginPass";
        case FramePhase::RecordScene:   return "RecordScene";
        case FramePhase::RecordOverlay: return "RecordOverlay";
        case FramePhase::EndPass:       return "EndPass";
        case FramePhase::Submit:        return "Submit";
        case FramePhase::Present:       return "Present";
        case FramePhase::COUNT:         break;
    }
    return "?";
}

#ifdef AURA_PROFILE_FRAME

/**
 * @class FrameProfiler
 * @brief Accumulates per-phase nanoseconds and logs a breakdown periodically.
 *
 * Single-threaded by construction: only the render thread opens scopes, which
 * is what lets the accumulators be plain integers rather than atomics. Worker
 * threads are measured through the phase that waits on them (RecordScene
 * blocks until every chunk is recorded), so their cost is already accounted
 * for without any cross-thread bookkeeping.
 */
class FrameProfiler
{
public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] static FrameProfiler& get() noexcept
    {
        static FrameProfiler instance;
        return instance;
    }

    void add(FramePhase phase, i64 nanos) noexcept
    {
        _totals[static_cast<u32>(phase)] += nanos;
    }

    //! Call once per frame, after the last phase has closed.
    void endFrame() noexcept;

    /**
     * @brief RAII timer for one phase.
     *
     * Deliberately not nestable within the same phase: a phase is a span of
     * the frame, not a call count, so overlapping scopes would double-count.
     */
    class Scope
    {
    public:
        explicit Scope(FramePhase phase) noexcept
            : _phase(phase), _start(Clock::now()) {}

        ~Scope()
        {
            const auto elapsed = Clock::now() - _start;
            FrameProfiler::get().add(
                _phase, std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        FramePhase _phase;
        Clock::time_point _start;
    };

private:
    FrameProfiler() = default;

    std::array<i64, static_cast<u32>(FramePhase::COUNT)> _totals{};
    u32 _frames = 0;
    Clock::time_point _windowStart = Clock::now();
};

//! Times the enclosing block as @p phase. Zero cost when profiling is off.
#define AURA_FRAME_SCOPE(phase) \
    ::aura3d::FrameProfiler::Scope _auraFrameScope##__LINE__ { phase }

//! Closes a frame for reporting purposes.
#define AURA_FRAME_END() ::aura3d::FrameProfiler::get().endFrame()

#else // !AURA_PROFILE_FRAME

#define AURA_FRAME_SCOPE(phase) ((void)0)
#define AURA_FRAME_END()        ((void)0)

#endif // AURA_PROFILE_FRAME

} // namespace aura3d

#endif // AURA_FRAMEPROFILER_H
