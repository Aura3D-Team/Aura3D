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
 * Compiled out unless AURA_PROFILE_FRAME is defined: AURA_FRAME_SCOPE then
 * expands to nothing and endFrame() is an empty inline, so a release build
 * pays no clock reads, counters, or branches for this.
 *
 * With AURA_ENABLE_DEBUG_MODE also on, a FrameObserver receives every frame's
 * phase breakdown as it closes -- used by DebugMode for percentiles, on top
 * of the periodic log this file prints on its own. Gated separately from
 * AURA_PROFILE_FRAME so a plain profiling build keeps its original cost.
 */

namespace aura3d {

/// Phases of one frame, in render-loop order.
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

//! Number of real phases, i.e. FramePhase::COUNT as an array bound.
inline constexpr u32 kFramePhaseCount = static_cast<u32>(FramePhase::COUNT);

/**
 * @struct FrameSample
 * @brief One frame's timings, as handed to a FrameObserver.
 *
 * Declared outside the AURA_PROFILE_FRAME guard so consumers can be written
 * and tested without the profiler being compiled in.
 */
struct FrameSample
{
    //! Per-phase nanoseconds for this frame alone, indexed by FramePhase.
    std::array<i64, kFramePhaseCount> phaseNanos{};

    /// Wall-clock nanoseconds since the previous frame closed. Not the sum of
    /// @c phaseNanos -- the gap is time spent outside any scope (event pump,
    /// application logic, a frame limiter's sleep). Zero on the first frame.
    i64 frameNanos = 0;
};

/**
 * @class FrameObserver
 * @brief Receives every closed frame, in order.
 *
 * @note Runs on the render thread inside AURA_FRAME_END(). Keep implementations
 *       cheap (append to a preallocated buffer); this is on the frame path.
 */
class FrameObserver
{
public:
    virtual ~FrameObserver() = default;

    virtual void onFrameSample(const FrameSample& sample) noexcept = 0;
};

#ifdef AURA_PROFILE_FRAME

/**
 * @class FrameProfiler
 * @brief Accumulates per-phase nanoseconds and logs a breakdown periodically.
 *
 * Single-threaded by construction: only the render thread opens scopes, so
 * the accumulators are plain integers, not atomics. Worker threads are
 * measured through the phase that waits on them (RecordScene blocks until
 * every chunk is recorded).
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
#ifdef AURA_ENABLE_DEBUG_MODE
        _current[static_cast<u32>(phase)] += nanos;
#endif
    }

    //! Call once per frame, after the last phase has closed.
    void endFrame() noexcept;

#ifdef AURA_ENABLE_DEBUG_MODE
    /**
     * @brief Installs @p observer, or clears it with nullptr.
     *
     * Non-owning: must outlive the render loop. One at a time -- a second
     * install replaces the first.
     */
    void setObserver(FrameObserver* observer) noexcept { _observer = observer; }

    [[nodiscard]] FrameObserver* observer() const noexcept { return _observer; }
#endif

    /// RAII timer for one phase. Not nestable within the same phase: a phase
    /// is a span of the frame, and overlapping scopes would double-count.
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

    std::array<i64, kFramePhaseCount> _totals{};
    u32 _frames = 0;
    Clock::time_point _windowStart = Clock::now();

#ifdef AURA_ENABLE_DEBUG_MODE
    //! This frame's phases alone; _totals accumulates across the report window.
    std::array<i64, kFramePhaseCount> _current{};
    Clock::time_point _lastFrameEnd = Clock::now();
    FrameObserver* _observer = nullptr;
#endif
};

//! Two levels of indirection so __LINE__ expands to a value before pasting;
//! without them, two scopes in one block would both be named the same thing.
#define AURA_FRAME_SCOPE_CAT_(a, b) a##b
#define AURA_FRAME_SCOPE_NAME_(line) AURA_FRAME_SCOPE_CAT_(_auraFrameScope, line)

//! Times the enclosing block as @p phase. Zero cost when profiling is off.
#define AURA_FRAME_SCOPE(phase) \
    ::aura3d::FrameProfiler::Scope AURA_FRAME_SCOPE_NAME_(__LINE__) { phase }

//! Closes a frame for reporting purposes.
#define AURA_FRAME_END() ::aura3d::FrameProfiler::get().endFrame()

#else // !AURA_PROFILE_FRAME

#define AURA_FRAME_SCOPE(phase) ((void)0)
#define AURA_FRAME_END()        ((void)0)

#endif // AURA_PROFILE_FRAME

/**
 * @brief Installs @p observer as the frame sink, if this build has one.
 *
 * No-op when the profiler is compiled out. Pass nullptr to detach, which the
 * owner must do before destroying the observer -- this holds a raw pointer.
 */
inline void installFrameObserver([[maybe_unused]] FrameObserver* observer) noexcept
{
#if defined(AURA_PROFILE_FRAME) && defined(AURA_ENABLE_DEBUG_MODE)
    FrameProfiler::get().setObserver(observer);
#endif
}

} // namespace aura3d

#endif // AURA_FRAMEPROFILER_H
