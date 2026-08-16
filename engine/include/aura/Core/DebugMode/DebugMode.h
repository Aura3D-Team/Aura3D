#ifndef AURA_DEBUGMODE_H
#define AURA_DEBUGMODE_H

#pragma once

#include <array>
#include <string>
#include <vector>

#include <ink/EnhancedJson.h>

#include "aura/aura.h"
#include "aura/Core/DebugMode/AllocationTracker.h"
#include "aura/Core/DebugMode/GpuDebugStats.h"
#include "aura/Core/Profiling/BenchmarkStats.h"
#include "aura/Core/Profiling/FrameProfiler.h"

/**
 * @file DebugMode.h
 * @brief The benchmark subsystem: collects, aggregates and writes the report.
 *
 * One object joins four independent sources -- FrameProfiler's per-phase
 * timings, AllocationTracker's CPU counters, the active backend's
 * IGpuDebugSource, and its own wall clock -- into a single JSON document that
 * answers "did this change regress performance or leak memory" without anyone
 * having to attach a profiler.
 *
 * ## Bounded memory, unbounded run
 *
 * A benchmark can run for minutes and a dev session for hours, so raw samples
 * live in a fixed-capacity ring holding the most recent
 * DebugModeConfig::sampleCapacity frames. Everything a bounded window would
 * distort -- the run's true frame count, its wall time, its allocation trend --
 * is kept instead in O(1) accumulators updated per frame, so those stay exact
 * over any run length while percentiles describe the recent window.
 *
 * ## Why the class is compiled unconditionally
 *
 * Only the *instance* is gated: Engine builds one when AURA_ENABLE_DEBUG_MODE
 * is set and Engine::debugMode() returns null otherwise. The class itself is
 * always compiled so that a default build can still unit-test it (feeding
 * synthetic frames through onFrameSample()), and so application code needs no
 * `#ifdef` around a null check it has to write regardless.
 */

namespace aura3d {

class AuraSettings;
class IRenderer;

/**
 * @struct DebugModeConfig
 * @brief What to measure, for how long, and what counts as a failure.
 *
 * Resolved from `settings.json`'s `debug` object and then from the environment,
 * in that order -- see fromSettings(). A CI job can therefore point an
 * unmodified application at a different report path and frame count without
 * editing a config file into the image.
 */
struct DebugModeConfig
{
    //! Where flushReport() writes when given no path of its own.
    std::string reportPath = "aura3d-benchmark.json";

    //! Free-form tag copied into the report, e.g. a commit SHA or a scene name.
    std::string label;

    /**
     * @brief Frames discarded before sampling begins.
     *
     * The first frames of any run are shader compilation, texture upload,
     * swapchain settling and page faults. Including them turns every
     * distribution into a bimodal one and makes p99 a measure of startup.
     */
    u32 warmupFrames = 60;

    //! Ring capacity for raw samples. ~96 bytes each; 20000 is about 5 minutes
    //! at 60 FPS and 1.9 MB.
    u32 sampleCapacity = 20000;

    //! Frames to capture before the run is finished(); 0 runs until the
    //! application stops on its own.
    u32 targetFrames = 0;

    //! Frame time a frame must stay under to count as on budget. 16.667 ms is
    //! 60 FPS.
    f64 frameBudgetMillis = 1000.0 / 60.0;

    //! Fraction of over-budget frames above which the verdict is a failure.
    f64 maxOverBudgetRatio = 0.05;

    /**
     * @brief Live-bytes growth per frame above which a leak is called.
     *
     * Compared against the least-squares slope of live bytes over the whole
     * run, not against the endpoints: a run that allocates and frees a
     * megabyte every frame has a slope of zero, and one that retains a hundred
     * bytes a frame has a slope of a hundred however small its totals look.
     */
    f64 leakSlopeBytesPerFrame = 1024.0;

    //! Write an interim report every N captured frames; 0 writes only at the
    //! end. Non-zero is what makes a report survive a CI job's timeout.
    u32 autoFlushIntervalFrames = 0;

    /**
     * @brief End the process once targetFrames have been captured.
     *
     * For headless benchmark runs, whose whole purpose is the report. Off by
     * default -- a dev session wants the window to stay up.
     */
    bool exitOnComplete = false;

    /**
     * @brief Reads the `debug` object of @p settings, then the environment.
     *
     * Environment overrides, each taking precedence over the file:
     *   - `AURA_DEBUG_REPORT`        -> reportPath
     *   - `AURA_DEBUG_LABEL`         -> label
     *   - `AURA_DEBUG_FRAMES`        -> targetFrames
     *   - `AURA_DEBUG_WARMUP`        -> warmupFrames
     *   - `AURA_DEBUG_BUDGET_MS`     -> frameBudgetMillis
     *   - `AURA_DEBUG_EXIT`          -> exitOnComplete (0/1)
     *
     * @param settings May be null, in which case only the environment applies.
     *                 Non-const to match AuraSettings::getSettings(), the same
     *                 way VulkanMemoryManager::loadConfig() takes it.
     */
    [[nodiscard]] static DebugModeConfig fromSettings(AuraSettings* settings);
};

/**
 * @class DebugMode
 * @brief Per-frame collection and report generation for benchmark runs.
 *
 * Owned by Engine, which installs it as the FrameProfiler's observer for its
 * lifetime. The destructor flushes a final report, so a run that ends by the
 * window closing still produces one.
 */
class DebugMode final : public FrameObserver
{
public:
    explicit DebugMode(DebugModeConfig config);
    ~DebugMode() override;

    DebugMode(const DebugMode&)            = delete;
    DebugMode& operator=(const DebugMode&) = delete;

    /**
     * @brief Records one frame. Called by FrameProfiler from AURA_FRAME_END().
     *
     * On the frame path, so it does no allocation (the ring is sized in the
     * constructor), no sorting and no I/O -- everything expensive waits for
     * flushReport().
     */
    void onFrameSample(const FrameSample& sample) noexcept override;

    /**
     * @brief Per-frame bookkeeping the application drives.
     *
     * Handles the interim auto-flush and, when configured, ending the run.
     * Kept out of onFrameSample() because both of those can write a file, and
     * a file write belongs in the application's frame, not inside the
     * profiler's frame-close.
     *
     * @param deltaSeconds The frame delta the application already computed.
     */
    void update(f32 deltaSeconds) noexcept;

    /**
     * @brief Binds the renderer whose GPU counters the report should include.
     *
     * Non-owning, and re-called after a backend switch. Pass nullptr to detach
     * before the renderer is destroyed.
     */
    void attachRenderer(const IRenderer* renderer) noexcept;

    //! True once targetFrames have been captured. Always false when
    //! targetFrames is 0.
    [[nodiscard]] bool finished() const noexcept;

    //! Frames captured since warm-up ended.
    [[nodiscard]] u64 capturedFrames() const noexcept { return _capturedFrames; }

    [[nodiscard]] const DebugModeConfig& config() const noexcept { return _config; }

    //! The whole report as JSON, without writing it. The report writer
    //! allocates; every path into it mutes AllocationTracker first, so building
    //! a report does not change the numbers in it.
    [[nodiscard]] ink::EnhancedJson buildReport() const;

    //! Writes buildReport() to DebugModeConfig::reportPath.
    [[nodiscard]] bool flushReport() const { return flushReport(_config.reportPath); }

    /**
     * @brief Writes buildReport() to @p path.
     * @return false if the file could not be opened or written.
     */
    [[nodiscard]] bool flushReport(const std::string& path) const;

private:
    /**
     * @struct FrameRecord
     * @brief One captured frame, as stored in the ring.
     */
    struct FrameRecord
    {
        i64 frameNanos = 0;
        std::array<i64, kFramePhaseCount> phaseNanos{};
        u64 liveBytes       = 0;
        u64 allocationCount = 0;
        f64 gpuMillis       = 0.0;
    };

    //! Ring contents oldest-first, as a flat copy for the summarisers.
    [[nodiscard]] std::vector<FrameRecord> orderedSamples() const;

    [[nodiscard]] ink::EnhancedJson buildFrameSection(const std::vector<FrameRecord>& ordered) const;
    [[nodiscard]] ink::EnhancedJson buildPhaseSection(const std::vector<FrameRecord>& ordered) const;
    [[nodiscard]] ink::EnhancedJson buildMemorySection() const;
    [[nodiscard]] ink::EnhancedJson buildGpuSection(const std::vector<FrameRecord>& ordered) const;
    [[nodiscard]] ink::EnhancedJson buildVerdictSection(const ink::EnhancedJson& frame,
                                                        const ink::EnhancedJson& memory) const;

    DebugModeConfig _config;

    //! Non-owning; see attachRenderer().
    const IRenderer* _renderer = nullptr;

    /// @name Ring of recent samples
    /// @{
    std::vector<FrameRecord> _samples;  ///< Fixed size, never reallocated.
    usize _writeIndex = 0;
    bool  _wrapped    = false;
    /// @}

    /// @name Whole-run accumulators
    /// Exact over any run length, unlike anything derived from the ring.
    /// @{
    u64 _warmupRemaining = 0;
    u64 _capturedFrames  = 0;
    i64 _totalFrameNanos = 0;
    i64 _minFrameNanos   = 0;
    i64 _maxFrameNanos   = 0;
    std::array<i64, kFramePhaseCount> _phaseTotalNanos{};

    u64 _overBudgetFrames            = 0;
    u32 _consecutiveOverBudget       = 0;
    u32 _maxConsecutiveOverBudget    = 0;

    //! Live bytes against frame index, for the leak verdict. See
    //! DebugModeConfig::leakSlopeBytesPerFrame.
    BenchmarkStats::LinearTrend _liveBytesTrend;

    //! Allocation counters at the first captured frame, so the report can
    //! attribute allocations to the measured window rather than to startup.
    u64 _baselineLiveBytes  = 0;
    u64 _baselineAllocCount = 0;
    /// @}

    f64 _wallSeconds = 0.0;
    u64 _framesSinceAutoFlush = 0;

    //! Set once the run has ended, so the destructor does not write a second,
    //! identical report over the one flushReport() already produced.
    mutable bool _reportWritten = false;

    /**
     * @brief Set once finished() has been handled by update(), so the
     *        completion branch runs exactly once.
     *
     * Deliberately a separate flag from _reportWritten rather than reusing it:
     * an interim auto-flush clears _reportWritten so the destructor still
     * writes a final report over the top, and that clear must not re-arm the
     * completion branch -- without a flag of its own, finished() staying true
     * after an interim flush would make update() rebuild and rewrite the whole
     * report (and log a line) on every single frame from then on.
     */
    bool _completionHandled = false;
};

} // namespace aura3d

#endif // AURA_DEBUGMODE_H
