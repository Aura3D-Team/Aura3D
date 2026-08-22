#ifndef AURA_BENCHMARKSTATS_H
#define AURA_BENCHMARKSTATS_H

#pragma once

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include "aura/aura.h"

/**
 * @file BenchmarkStats.h
 * @brief Distribution statistics over a run of samples.
 *
 * A mean frame time alone can't distinguish a flat 4ms run from one averaging
 * 4ms with a 40ms hitch every second, so this also computes median, p95/p99,
 * and budget-crossing frequency.
 *
 * Unconditionally compiled (pure functions, no global state), so it stays
 * testable without AURA_ENABLE_DEBUG_MODE.
 */

namespace aura3d {

/**
 * @struct StatSummary
 * @brief One metric's distribution over a run.
 *
 * @note @c stddev is the *population* standard deviation (divides by N, not
 *       N-1). The samples are the whole run rather than a draw from some
 *       larger population, so there is no degree of freedom to give up.
 */
struct StatSummary
{
    u64 count  = 0;    ///< Number of samples the summary was computed from.
    f64 min    = 0.0;
    f64 max    = 0.0;
    f64 mean   = 0.0;  ///< Arithmetic mean.
    f64 median = 0.0;  ///< 50th percentile, interpolated for even counts.
    f64 stddev = 0.0;  ///< Population standard deviation.
    f64 p95    = 0.0;  ///< 95th percentile.
    f64 p99    = 0.0;  ///< 99th percentile.

    //! Median absolute deviation: median(|x - median|). Reported beside stddev
    //! because the two disagreeing is itself informative: small MAD with large
    //! stddev means a smooth run with hitches.
    f64 mad = 0.0;
};

/**
 * @class BenchmarkStats
 * @brief Stateless summarisers over sample buffers, plus an O(1) trend accumulator.
 *
 * Every entry point takes a @c std::span, so a caller may pass a vector, an
 * array or a slice of a ring buffer without copying it into some intermediate
 * container first.
 */
class BenchmarkStats
{
public:
    BenchmarkStats() = delete;

    /// Summarises @p samples, which need not be sorted. Copies and sorts
    /// internally (allocates); prefer summarizeSorted() on a hot path or
    /// under AllocationTracker::ScopedMute.
    /// @return A zeroed summary when @p samples is empty.
    [[nodiscard]] static StatSummary summarize(std::span<const f64> samples)
    {
        if (samples.empty())
            return {};

        std::vector<f64> sorted(samples.begin(), samples.end());
        std::sort(sorted.begin(), sorted.end());

        StatSummary summary = summarizeSorted(sorted);

        //! The MAD needs a second sort, of the deviations rather than of the
        //! samples, so it cannot come out of the sorted-input path.
        std::vector<f64> deviations;
        deviations.reserve(sorted.size());
        for (const f64 sample : sorted)
            deviations.push_back(std::abs(sample - summary.median));

        std::sort(deviations.begin(), deviations.end());
        summary.mad = percentileSorted(deviations, 0.5);

        return summary;
    }

    /// Summarises an already ascending-sorted @p sorted. Leaves
    /// StatSummary::mad at zero (not derivable in one pass); use summarize()
    /// when the MAD is wanted.
    [[nodiscard]] static StatSummary summarizeSorted(std::span<const f64> sorted) noexcept
    {
        StatSummary summary;
        if (sorted.empty())
            return summary;

        const auto n = static_cast<f64>(sorted.size());

        summary.count  = static_cast<u64>(sorted.size());
        summary.min    = sorted.front();
        summary.max    = sorted.back();
        summary.median = percentileSorted(sorted, 0.50);
        summary.p95    = percentileSorted(sorted, 0.95);
        summary.p99    = percentileSorted(sorted, 0.99);

        f64 sum = 0.0;
        for (const f64 sample : sorted)
            sum += sample;
        summary.mean = sum / n;

        //! Two passes rather than the sum-of-squares shortcut: frame times'
        //! large mean + small spread makes E[x^2] - E[x]^2 cancel away its own
        //! significant digits and occasionally go negative.
        f64 sumSquaredError = 0.0;
        for (const f64 sample : sorted)
        {
            const f64 error = sample - summary.mean;
            sumSquaredError += error * error;
        }
        summary.stddev = std::sqrt(sumSquaredError / n);

        return summary;
    }

    /**
     * @brief The @p fraction-quantile of ascending-sorted @p sorted, linearly
     *        interpolated between the two bracketing ranks.
     *
     * @param fraction Clamped to [0, 1]; 0.5 gives the median.
     */
    [[nodiscard]] static f64 percentileSorted(std::span<const f64> sorted, f64 fraction) noexcept
    {
        if (sorted.empty())
            return 0.0;

        const f64 clamped = std::clamp(fraction, 0.0, 1.0);
        const f64 rank    = clamped * static_cast<f64>(sorted.size() - 1);

        const auto lower  = static_cast<usize>(rank);
        const auto upper  = std::min(lower + 1, sorted.size() - 1);
        const f64 weight  = rank - static_cast<f64>(lower);

        return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
    }

    /// Fraction of @p samples strictly above @p threshold: the non-parametric
    /// "how often did this run blow the budget", no distribution assumed.
    /// Compare against normalExceedance() -- disagreement means non-normal
    /// samples, which frame times usually are.
    [[nodiscard]] static f64 empiricalExceedance(std::span<const f64> samples, f64 threshold) noexcept
    {
        if (samples.empty())
            return 0.0;

        u64 over = 0;
        for (const f64 sample : samples)
        {
            if (sample > threshold)
                ++over;
        }

        return static_cast<f64>(over) / static_cast<f64>(samples.size());
    }

    /**
     * @brief P(X > @p threshold) for a normal fit of @p summary.
     *
     * Extrapolates past the longest sample actually observed, which is the
     * only way to put a number on a budget never once crossed. Treat as
     * order-of-magnitude only: frame times are right-skewed, so a normal fit
     * understates the far tail.
     *
     * @return 0 or 1 for a degenerate (zero-variance) run.
     */
    [[nodiscard]] static f64 normalExceedance(const StatSummary& summary, f64 threshold) noexcept
    {
        if (summary.count == 0)
            return 0.0;

        if (summary.stddev <= 0.0)
            return summary.mean > threshold ? 1.0 : 0.0;

        //! 0.5 * erfc(z / sqrt(2)) is the upper tail of the standard normal,
        //! and erfc keeps its precision out where the naive 1 - cdf(z) has
        //! already cancelled itself down to zero.
        const f64 z = (threshold - summary.mean) / summary.stddev;
        return 0.5 * std::erfc(z * kInvSqrt2);
    }

    /// Least-squares slope of @p samples against their own index (units:
    /// sample units per sample). Fed live allocation bytes, answers the leak
    /// question directly. @return 0 for fewer than two samples.
    [[nodiscard]] static f64 trendSlope(std::span<const f64> samples) noexcept
    {
        LinearTrend trend;
        for (const f64 sample : samples)
            trend.add(sample);

        return trend.slope();
    }

    /// Streaming least-squares slope against the sample index: trendSlope()
    /// without keeping the samples, just four running sums.
    class LinearTrend
    {
    public:
        //! Appends @p value at the next index.
        void add(f64 value) noexcept
        {
            const f64 x = static_cast<f64>(_count);

            _sumX  += x;
            _sumY  += value;
            _sumXY += x * value;
            _sumXX += x * x;
            ++_count;
        }

        //! Slope in value-units per sample; 0 for fewer than two samples.
        [[nodiscard]] f64 slope() const noexcept
        {
            if (_count < 2)
                return 0.0;

            const auto n = static_cast<f64>(_count);
            const f64 denominator = n * _sumXX - _sumX * _sumX;

            //! Cannot be zero for n >= 2 distinct integer x, but the samples
            //! reaching this are unbounded and the division is not worth a
            //! surprise.
            if (denominator == 0.0)
                return 0.0;

            return (n * _sumXY - _sumX * _sumY) / denominator;
        }

        [[nodiscard]] u64 count() const noexcept { return _count; }

    private:
        f64 _sumX  = 0.0;
        f64 _sumY  = 0.0;
        f64 _sumXY = 0.0;
        f64 _sumXX = 0.0;
        u64 _count = 0;
    };

private:
    //! std::numbers::inv_sqrt2 by another name, spelled out so this header does
    //! not pull <numbers> in for one constant.
    static constexpr f64 kInvSqrt2 = 0.70710678118654752440;
};

} // namespace aura3d

#endif // AURA_BENCHMARKSTATS_H
