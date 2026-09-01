// BenchmarkStats: distribution statistics over a run of samples.
//
// Every expected value below is hand-computed and written out in the check
// description, so a failure says which number was wrong rather than only that
// one was. No GPU, no window, no files -- this is pure arithmetic, and it runs
// identically in every CI leg.

#include <cmath>
#include <vector>

#include "aura/Core/Profiling/BenchmarkStats.h"

#include "TestUtils.h"

using namespace aura3d;

namespace {

//! Frame times are doubles, so every comparison here is a tolerance one. The
//! bound is far tighter than any error the two-pass variance can introduce and
//! far looser than the last bit of a double.
constexpr f64 kEpsilon = 1e-9;

[[nodiscard]] bool near(f64 lhs, f64 rhs) noexcept
{
    return std::abs(lhs - rhs) <= kEpsilon;
}

void testEmpty()
{
    const StatSummary summary = BenchmarkStats::summarize({});

    AURA_CHECK(summary.count == 0, "empty input: count is 0");
    AURA_CHECK(near(summary.mean, 0.0), "empty input: mean is 0");
    AURA_CHECK(near(summary.median, 0.0), "empty input: median is 0");
    AURA_CHECK(near(summary.stddev, 0.0), "empty input: stddev is 0");
    AURA_CHECK(near(BenchmarkStats::percentileSorted({}, 0.95), 0.0),
               "empty input: percentile is 0");
    AURA_CHECK(near(BenchmarkStats::empiricalExceedance({}, 1.0), 0.0),
               "empty input: empirical exceedance is 0");
}

void testSingleSample()
{
    const std::vector<f64> samples{42.0};
    const StatSummary summary = BenchmarkStats::summarize(samples);

    AURA_CHECK(summary.count == 1, "one sample: count is 1");
    AURA_CHECK(near(summary.min, 42.0), "one sample: min is 42");
    AURA_CHECK(near(summary.max, 42.0), "one sample: max is 42");
    AURA_CHECK(near(summary.mean, 42.0), "one sample: mean is 42");
    AURA_CHECK(near(summary.median, 42.0), "one sample: median is 42");
    AURA_CHECK(near(summary.stddev, 0.0), "one sample: stddev is 0");
    AURA_CHECK(near(summary.p95, 42.0), "one sample: p95 is 42");
    AURA_CHECK(near(summary.p99, 42.0), "one sample: p99 is 42");
    AURA_CHECK(near(summary.mad, 0.0), "one sample: mad is 0");
}

void testAllEqual()
{
    const std::vector<f64> samples{7.0, 7.0, 7.0, 7.0};
    const StatSummary summary = BenchmarkStats::summarize(samples);

    AURA_CHECK(summary.count == 4, "all-equal: count is 4");
    AURA_CHECK(near(summary.mean, 7.0), "all-equal: mean is 7");
    AURA_CHECK(near(summary.median, 7.0), "all-equal: median is 7");
    AURA_CHECK(near(summary.stddev, 0.0), "all-equal: stddev is 0");
    AURA_CHECK(near(summary.mad, 0.0), "all-equal: mad is 0");

    // A zero-variance run has no distribution to extrapolate, so the normal fit
    // degenerates to which side of the threshold the constant value is on.
    AURA_CHECK(near(BenchmarkStats::normalExceedance(summary, 5.0), 1.0),
               "all-equal: P(x > 5) is 1 for a constant 7");
    AURA_CHECK(near(BenchmarkStats::normalExceedance(summary, 9.0), 0.0),
               "all-equal: P(x > 9) is 0 for a constant 7");
}

void testOddCount()
{
    // 1..5: mean 3, median 3, population variance (4+1+0+1+4)/5 = 2.
    const std::vector<f64> samples{3.0, 1.0, 5.0, 2.0, 4.0}; // deliberately unsorted
    const StatSummary summary = BenchmarkStats::summarize(samples);

    AURA_CHECK(summary.count == 5, "1..5: count is 5");
    AURA_CHECK(near(summary.min, 1.0), "1..5: min is 1");
    AURA_CHECK(near(summary.max, 5.0), "1..5: max is 5");
    AURA_CHECK(near(summary.mean, 3.0), "1..5: mean is 3");
    AURA_CHECK(near(summary.median, 3.0), "1..5: median is 3");
    AURA_CHECK(near(summary.stddev, std::sqrt(2.0)), "1..5: stddev is sqrt(2)");

    // Interpolated between closest ranks: rank = 0.95 * 4 = 3.8, so
    // 0.2 * samples[3] + 0.8 * samples[4] = 0.2*4 + 0.8*5 = 4.8.
    AURA_CHECK(near(summary.p95, 4.8), "1..5: p95 is 4.8");
    AURA_CHECK(near(summary.p99, 4.96), "1..5: p99 is 4.96");

    // Deviations from the median are {2,1,0,1,2}; their median is 1.
    AURA_CHECK(near(summary.mad, 1.0), "1..5: mad is 1");
}

void testEvenCount()
{
    // 1..4: the median falls between 2 and 3 and is interpolated to 2.5.
    const std::vector<f64> samples{1.0, 2.0, 3.0, 4.0};
    const StatSummary summary = BenchmarkStats::summarize(samples);

    AURA_CHECK(near(summary.mean, 2.5), "1..4: mean is 2.5");
    AURA_CHECK(near(summary.median, 2.5), "1..4: median is 2.5");
    AURA_CHECK(near(summary.stddev, std::sqrt(1.25)), "1..4: stddev is sqrt(1.25)");
}

void testPercentileBounds()
{
    const std::vector<f64> sorted{1.0, 2.0, 3.0, 4.0, 5.0};

    AURA_CHECK(near(BenchmarkStats::percentileSorted(sorted, 0.0), 1.0),
               "percentile: 0.0 is the minimum");
    AURA_CHECK(near(BenchmarkStats::percentileSorted(sorted, 1.0), 5.0),
               "percentile: 1.0 is the maximum");
    AURA_CHECK(near(BenchmarkStats::percentileSorted(sorted, 0.5), 3.0),
               "percentile: 0.5 is the median");

    // Out-of-range fractions clamp rather than reading past the buffer.
    AURA_CHECK(near(BenchmarkStats::percentileSorted(sorted, -1.0), 1.0),
               "percentile: negative fraction clamps to the minimum");
    AURA_CHECK(near(BenchmarkStats::percentileSorted(sorted, 7.0), 5.0),
               "percentile: fraction above 1 clamps to the maximum");
}

void testExceedance()
{
    const std::vector<f64> samples{1.0, 2.0, 3.0, 4.0, 5.0};
    const StatSummary summary = BenchmarkStats::summarize(samples);

    // Strictly above: 4 and 5 exceed 3, so 2/5.
    AURA_CHECK(near(BenchmarkStats::empiricalExceedance(samples, 3.0), 0.4),
               "empirical exceedance: 2 of 5 samples above 3");
    AURA_CHECK(near(BenchmarkStats::empiricalExceedance(samples, 5.0), 0.0),
               "empirical exceedance: nothing above the maximum");
    AURA_CHECK(near(BenchmarkStats::empiricalExceedance(samples, 0.0), 1.0),
               "empirical exceedance: everything above 0");

    // A threshold exactly at the mean is the centre of the fitted normal.
    AURA_CHECK(near(BenchmarkStats::normalExceedance(summary, 3.0), 0.5),
               "normal exceedance: P(x > mean) is 0.5");

    // And the tails are on the correct sides of it.
    AURA_CHECK(BenchmarkStats::normalExceedance(summary, 10.0) < 1e-6,
               "normal exceedance: far above the mean is ~0");
    AURA_CHECK(BenchmarkStats::normalExceedance(summary, -10.0) > 0.999999,
               "normal exceedance: far below the mean is ~1");
}

void testTrendSlope()
{
    AURA_CHECK(near(BenchmarkStats::trendSlope({}), 0.0),
               "trend: no samples gives slope 0");

    const std::vector<f64> single{5.0};
    AURA_CHECK(near(BenchmarkStats::trendSlope(single), 0.0),
               "trend: one sample gives slope 0");

    const std::vector<f64> flat{5.0, 5.0, 5.0, 5.0};
    AURA_CHECK(near(BenchmarkStats::trendSlope(flat), 0.0),
               "trend: a flat series has slope 0");

    // The leak signal: a series that ends where it started has slope 0 no
    // matter how much it moved in between.
    const std::vector<f64> churn{100.0, 900.0, 100.0, 900.0, 100.0};
    AURA_CHECK(std::abs(BenchmarkStats::trendSlope(churn)) < 1.0,
               "trend: churn without retention has a near-zero slope");

    const std::vector<f64> rising{0.0, 1.0, 2.0, 3.0, 4.0};
    AURA_CHECK(near(BenchmarkStats::trendSlope(rising), 1.0),
               "trend: +1 per sample gives slope 1");

    const std::vector<f64> falling{10.0, 8.0, 6.0};
    AURA_CHECK(near(BenchmarkStats::trendSlope(falling), -2.0),
               "trend: -2 per sample gives slope -2");
}

void testStreamingTrendMatchesBatch()
{
    // The whole point of LinearTrend is that a run longer than any buffer can
    // still be measured across all of it, so it must agree exactly with the
    // batch computation on data small enough to hold.
    const std::vector<f64> samples{3.0, 5.5, 8.0, 10.5, 13.0, 15.5};

    BenchmarkStats::LinearTrend trend;
    for (const f64 sample : samples)
        trend.add(sample);

    AURA_CHECK(trend.count() == samples.size(), "streaming trend: counts every sample");
    AURA_CHECK(near(trend.slope(), 2.5), "streaming trend: +2.5 per sample gives slope 2.5");
    AURA_CHECK(near(trend.slope(), BenchmarkStats::trendSlope(samples)),
               "streaming trend: agrees with the batch computation");
}

} // namespace

int main()
{
    testEmpty();
    testSingleSample();
    testAllEqual();
    testOddCount();
    testEvenCount();
    testPercentileBounds();
    testExceedance();
    testTrendSlope();
    testStreamingTrendMatchesBatch();

    AURA_TEST_MAIN_RETURN();
}
