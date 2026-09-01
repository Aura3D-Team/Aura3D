// DebugMode: the benchmark report's shape, arithmetic and verdict.
//
// Driven entirely through synthetic FrameSamples, which is what DebugMode's
// FrameObserver interface is for -- no window, no renderer, no GPU, so this
// runs in every CI leg exactly as the loader suites do. What it checks is that
// a run of known frames produces a report whose numbers can be derived by hand
// and which parses back through the same JSON type that wrote it.

#include <cmath>
#include <cstdio>
#include <string>

#include <ink/EnhancedJson.h>

#include "aura/Core/DebugMode/DebugMode.h"
#include "aura/Core/Profiling/FrameProfiler.h"

#include "TestUtils.h"

using namespace aura3d;

namespace {

constexpr f64 kEpsilon = 1e-9;

[[nodiscard]] bool near(f64 lhs, f64 rhs) noexcept
{
    return std::abs(lhs - rhs) <= kEpsilon;
}

//! One frame of exactly @p frameMillis, of which @p sceneMillis is RecordScene.
[[nodiscard]] FrameSample makeSample(f64 frameMillis, f64 sceneMillis) noexcept
{
    FrameSample sample;

    sample.frameNanos = static_cast<i64>(frameMillis * 1.0e6);
    sample.phaseNanos[static_cast<u32>(FramePhase::RecordScene)] =
        static_cast<i64>(sceneMillis * 1.0e6);

    return sample;
}

[[nodiscard]] DebugModeConfig baseConfig(std::string path)
{
    DebugModeConfig config;

    config.reportPath        = std::move(path);
    config.label             = "unit-test";
    config.warmupFrames      = 2;
    config.sampleCapacity    = 8;
    config.frameBudgetMillis = 10.0;

    //! Left at zero deliberately: a target frame count would make update() end
    //! the run, and this suite never calls update().
    config.targetFrames = 0;

    return config;
}

/**
 * @brief A steady 5 ms run, long enough to wrap the 8-frame ring.
 *
 * Checks the two things the ring makes non-obvious: that the whole-run
 * accumulators still see every frame, and that the windowed percentiles are
 * labelled as covering only the window.
 */
void testSteadyRunReport()
{
    const std::string path = "aura3d-test-report-steady.json";

    u64 captured = 0;
    {
        DebugMode debug(baseConfig(path));

        const FrameSample sample = makeSample(/*frameMillis=*/5.0, /*sceneMillis=*/2.0);

        // Two warm-up frames, deliberately slow, that must not reach the
        // distribution at all.
        const FrameSample warmup = makeSample(/*frameMillis=*/500.0, /*sceneMillis=*/400.0);
        debug.onFrameSample(warmup);
        debug.onFrameSample(warmup);

        for (int frame = 0; frame < 10; ++frame)
            debug.onFrameSample(sample);

        captured = debug.capturedFrames();

        AURA_CHECK(debug.flushReport(path), "steady run: report written");
    }

    const ink::EnhancedJson report = ink::EnhancedJson::loadFromFile(path);

    AURA_CHECK(captured == 10, "steady run: 10 frames captured, 2 dropped as warm-up");

    AURA_CHECK(report.getPath<std::string>("/schema", "") == "aura3d.benchmark/1",
               "steady run: schema version round-trips");
    AURA_CHECK(report.getPath<std::string>("/label", "") == "unit-test",
               "steady run: label round-trips");
    AURA_CHECK(report.getPath<u64>("/run/frames_captured", 0) == 10,
               "steady run: frames_captured is 10");
    AURA_CHECK(report.getPath<u64>("/run/frames_warmup", 0) == 2,
               "steady run: frames_warmup is 2");

    // 10 frames into an 8-frame ring: the window holds the last 8 and says so.
    AURA_CHECK(report.getPath<u64>("/run/sample_window", 0) == 8,
               "steady run: the window holds the ring's 8 most recent frames");
    AURA_CHECK(report.getPath<bool>("/run/window_truncated", false),
               "steady run: window_truncated flags the wrap");

    // The warm-up frames were 500 ms; if any had leaked into the distribution
    // the maximum would show it.
    AURA_CHECK(near(report.getPath<f64>("/frame/cpu_ms/mean", 0.0), 5.0),
               "steady run: mean frame time is 5 ms");
    AURA_CHECK(near(report.getPath<f64>("/frame/cpu_ms/max", 0.0), 5.0),
               "steady run: warm-up frames are excluded from the maximum");
    AURA_CHECK(near(report.getPath<f64>("/frame/cpu_ms/stddev", 1.0), 0.0),
               "steady run: a constant run has zero deviation");

    // Whole-run accumulators are exact over all 10 frames, not just the 8 in
    // the ring -- which for a constant run means they agree with the window.
    AURA_CHECK(report.getPath<u64>("/frame/whole_run/frames", 0) == 10,
               "steady run: whole_run counts every captured frame");
    AURA_CHECK(near(report.getPath<f64>("/frame/whole_run/mean_ms", 0.0), 5.0),
               "steady run: whole_run mean is 5 ms");

    AURA_CHECK(near(report.getPath<f64>("/frame/fps/mean", 0.0), 200.0),
               "steady run: 5 ms per frame is 200 FPS");
    AURA_CHECK(near(report.getPath<f64>("/frame/fps/one_percent_low", 0.0), 200.0),
               "steady run: a constant run's 1% low equals its mean");

    // 2 ms of the 5 is RecordScene; the other 3 are outside any scope.
    AURA_CHECK(near(report.getPath<f64>("/phases/RecordScene/whole_run_mean_ms", 0.0), 2.0),
               "steady run: RecordScene averages 2 ms");
    AURA_CHECK(near(report.getPath<f64>("/phases/RecordScene/share_percent", 0.0), 40.0),
               "steady run: RecordScene is 40% of the frame");
    AURA_CHECK(near(report.getPath<f64>("/phases/unscoped/whole_run_mean_ms", 0.0), 3.0),
               "steady run: the unscoped remainder is 3 ms");
    AURA_CHECK(near(report.getPath<f64>("/phases/Present/whole_run_mean_ms", 1.0), 0.0),
               "steady run: an unrecorded phase reports 0 ms");

    // No renderer attached, so the GPU section must say "not measured" rather
    // than present zeroes as a measurement.
    AURA_CHECK(!report.getPath<bool>("/gpu/available", true),
               "steady run: GPU section is unavailable with no renderer attached");

    // Every frame is 5 ms against a 10 ms budget.
    AURA_CHECK(report.getPath<u64>("/frame/budget/over_budget_frames", 1) == 0,
               "steady run: no frame exceeds the 10 ms budget");
    AURA_CHECK(near(report.getPath<f64>("/frame/budget/probability_empirical", 1.0), 0.0),
               "steady run: empirical exceedance is 0");
    AURA_CHECK(report.getPath<std::string>("/verdict/status", "") == "pass",
               "steady run: verdict passes");

    std::remove(path.c_str());
}

/**
 * @brief A run that blows the budget, to prove the verdict is not decorative.
 */
void testOverBudgetVerdict()
{
    const std::string path = "aura3d-test-report-slow.json";

    {
        DebugModeConfig config = baseConfig(path);
        config.warmupFrames       = 0;
        config.sampleCapacity     = 16;
        config.frameBudgetMillis  = 10.0;
        config.maxOverBudgetRatio = 0.10;

        DebugMode debug(config);

        // 6 frames on budget, 4 well over it: 40% against a 10% limit.
        for (int frame = 0; frame < 6; ++frame)
            debug.onFrameSample(makeSample(5.0, 1.0));

        for (int frame = 0; frame < 4; ++frame)
            debug.onFrameSample(makeSample(40.0, 30.0));

        AURA_CHECK(debug.flushReport(path), "over budget: report written");
    }

    const ink::EnhancedJson report = ink::EnhancedJson::loadFromFile(path);

    AURA_CHECK(report.getPath<u64>("/frame/budget/over_budget_frames", 0) == 4,
               "over budget: 4 frames exceeded the budget");
    AURA_CHECK(near(report.getPath<f64>("/frame/budget/over_budget_ratio", 0.0), 0.4),
               "over budget: the ratio is 0.4");

    // The four slow frames were consecutive, which is what turns a statistic
    // into a visible stutter.
    AURA_CHECK(report.getPath<u64>("/frame/budget/max_consecutive_over_budget", 0) == 4,
               "over budget: the longest over-budget streak is 4 frames");
    AURA_CHECK(near(report.getPath<f64>("/frame/budget/probability_empirical", 0.0), 0.4),
               "over budget: empirical exceedance matches the ratio");
    AURA_CHECK(report.getPath<std::string>("/verdict/status", "") == "fail",
               "over budget: verdict fails");

    // The threshold it was judged against is in the artifact, so the verdict
    // can be reproduced from the file alone.
    AURA_CHECK(near(report.getPath<f64>("/frame/budget/target_ms", 0.0), 10.0),
               "over budget: the budget the run was judged against is recorded");

    std::remove(path.c_str());
}

/**
 * @brief A run shorter than the ring, checked against the un-wrapped path.
 */
void testShortRunIsNotTruncated()
{
    const std::string path = "aura3d-test-report-short.json";

    {
        DebugModeConfig config = baseConfig(path);
        config.warmupFrames   = 0;
        config.sampleCapacity = 64;

        DebugMode debug(config);

        for (int frame = 0; frame < 3; ++frame)
            debug.onFrameSample(makeSample(8.0, 3.0));

        AURA_CHECK(debug.flushReport(path), "short run: report written");
    }

    const ink::EnhancedJson report = ink::EnhancedJson::loadFromFile(path);

    AURA_CHECK(report.getPath<u64>("/run/sample_window", 0) == 3,
               "short run: the window holds all 3 frames");
    AURA_CHECK(!report.getPath<bool>("/run/window_truncated", true),
               "short run: window_truncated is false when the ring never wrapped");
    AURA_CHECK(report.getPath<u64>("/frame/cpu_ms/count", 0) == 3,
               "short run: the distribution covers all 3 frames");

    std::remove(path.c_str());
}

/**
 * @brief A run with no frames at all still produces a parseable, failing report.
 *
 * The case a CI job hits when the application crashed before rendering: an
 * artifact full of zeroes that claimed to pass would be worse than no artifact.
 */
void testEmptyRunFailsRatherThanLies()
{
    const std::string path = "aura3d-test-report-empty.json";

    {
        DebugMode debug(baseConfig(path));
        AURA_CHECK(debug.capturedFrames() == 0, "empty run: nothing captured");
        AURA_CHECK(debug.flushReport(path), "empty run: report still written");
    }

    const ink::EnhancedJson report = ink::EnhancedJson::loadFromFile(path);

    AURA_CHECK(report.getPath<u64>("/run/frames_captured", 1) == 0,
               "empty run: frames_captured is 0");
    AURA_CHECK(report.getPath<std::string>("/verdict/status", "") == "fail",
               "empty run: verdict fails rather than passing on no evidence");

    std::remove(path.c_str());
}

/**
 * @brief finished() only fires once a target is configured and reached.
 */
void testFinishedTracksTargetFrames()
{
    DebugModeConfig config = baseConfig("aura3d-test-report-target.json");
    config.warmupFrames = 0;
    config.targetFrames = 4;

    DebugMode debug(config);

    AURA_CHECK(!debug.finished(), "target: not finished before any frame");

    for (int frame = 0; frame < 3; ++frame)
        debug.onFrameSample(makeSample(5.0, 1.0));

    AURA_CHECK(!debug.finished(), "target: not finished one frame short");

    debug.onFrameSample(makeSample(5.0, 1.0));
    AURA_CHECK(debug.finished(), "target: finished once the target is reached");

    // The destructor writes a final report; drop it rather than leaving the
    // build directory littered.
    (void)debug.flushReport(config.reportPath);
    std::remove(config.reportPath.c_str());
}

} // namespace

int main()
{
    testSteadyRunReport();
    testOverBudgetVerdict();
    testShortRunIsNotTruncated();
    testEmptyRunFailsRatherThanLies();
    testFinishedTracksTargetFrames();

    AURA_TEST_MAIN_RETURN();
}
