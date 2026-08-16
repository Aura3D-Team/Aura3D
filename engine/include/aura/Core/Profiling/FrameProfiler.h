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
 *
 * AURA_ENABLE_DEBUG_MODE adds a second, per-frame outlet on top of the periodic
 * log: a FrameObserver installed here is handed every frame's phase breakdown
 * as it closes, which is what lets aura3d::DebugMode compute percentiles rather
 * than only the rolling average this file prints. The observer machinery is
 * gated separately from AURA_PROFILE_FRAME so that a plain profiling build
 * keeps exactly the cost it had before.
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

//! Number of real phases, i.e. FramePhase::COUNT as an array bound.
inline constexpr u32 kFramePhaseCount = static_cast<u32>(FramePhase::COUNT);

/**
 * @struct FrameSample
 * @brief One frame's timings, as handed to a FrameObserver.
 *
 * Declared outside the AURA_PROFILE_FRAME guard so that a consumer -- the
 * benchmark report writer, a test feeding synthetic frames -- can be written
 * and tested without the profiler being compiled in.
 */
struct FrameSample
{
    //! Per-phase nanoseconds for this frame alone, indexed by FramePhase.
    std::array<i64, kFramePhaseCount> phaseNanos{};

    /**
     * @brief Wall-clock nanoseconds from the previous frame's close to this one's.
     *
     * Not the sum of @c phaseNanos, and deliberately: the difference between
     * the two is everything the loop spent outside any scope -- the event pump,
     * application logic, a frame limiter's sleep -- which is a finding rather
     * than an error term. Zero for the very first frame, which has no
     * predecessor to measure against.
     */
    i64 frameNanos = 0;
};

/**
 * @class FrameObserver
 * @brief Receives every closed frame, in order.
 *
 * @note onFrameSample() runs on the render thread inside AURA_FRAME_END(),
 *       between one frame and the next. It is on the frame path, so an
 *       implementation belongs in the "append to a preallocated buffer"
 *       category, not the "sort and write a file" one.
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
     * Non-owning: the observer must outlive the render loop, which is what
     * Engine's ownership of DebugMode gives it. One at a time -- a second
     * install replaces the first rather than fanning out, since the only
     * consumer is the report writer and a list would put an indirect call per
     * entry on the frame path for it.
     */
    void setObserver(FrameObserver* observer) noexcept { _observer = observer; }

    [[nodiscard]] FrameObserver* observer() const noexcept { return _observer; }
#endif

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

    std::array<i64, kFramePhaseCount> _totals{};
    u32 _frames = 0;
    Clock::time_point _windowStart = Clock::now();

#ifdef AURA_ENABLE_DEBUG_MODE
    //! This frame's phases alone. _totals accumulates across the report window
    //! and cannot be differenced back into per-frame values once summed.
    std::array<i64, kFramePhaseCount> _current{};
    Clock::time_point _lastFrameEnd = Clock::now();
    FrameObserver* _observer = nullptr;
#endif
};

//! Two levels of indirection so that __LINE__ is expanded to its value before
//! being pasted, rather than pasted literally: without them every scope in a
//! translation unit is named _auraFrameScope__LINE__ and two in one block are a
//! redefinition.
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
 * The unconditional face of FrameProfiler::setObserver(): a no-op when the
 * profiler is compiled out, so a caller (Engine) needs no `#ifdef` around
 * wiring up something that may simply never be called. Pass nullptr to detach,
 * which the owner must do before destroying the observer -- the profiler holds
 * a raw pointer.
 */
inline void installFrameObserver([[maybe_unused]] FrameObserver* observer) noexcept
{
#if defined(AURA_PROFILE_FRAME) && defined(AURA_ENABLE_DEBUG_MODE)
    FrameProfiler::get().setObserver(observer);
#endif
}

} // namespace aura3d

#endif // AURA_FRAMEPROFILER_H
