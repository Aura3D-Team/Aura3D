#include "aura/Core/DebugMode/DebugMode.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>

#include <ink/Inkogger.h>

#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Renderer/IRenderer.h"

namespace aura3d {
namespace {
constexpr const char* kSchemaVersion = "aura3d.benchmark/1";

constexpr f64 kNanosPerMilli = 1.0e6;

[[nodiscard]] f64 toMillis(i64 nanos) noexcept
{
    return static_cast<f64>(nanos) / kNanosPerMilli;
}

[[nodiscard]] std::string readEnv(const char* name)
{
#ifdef _MSC_VER
    char* buffer = nullptr;
    usize length = 0;

    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr)
        return {};

    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

void applyEnv(const char* name, u32& target)
{
    const std::string value = readEnv(name);
    if (value.empty())
        return;

    try
    {
        target = static_cast<u32>(std::stoul(value));
    }
    catch (...)
    {
        INK_WARN << "DebugMode: ignoring unparseable " << name << "='" << value << "'";
    }
}

void applyEnv(const char* name, f64& target)
{
    const std::string value = readEnv(name);
    if (value.empty())
        return;

    try
    {
        target = std::stod(value);
    }
    catch (...)
    {
        INK_WARN << "DebugMode: ignoring unparseable " << name << "='" << value << "'";
    }
}

void applyEnv(const char* name, bool& target)
{
    const std::string value = readEnv(name);
    if (value.empty())
        return;

    target = (value != "0" && value != "false" && value != "OFF" && value != "off");
}

void applyEnv(const char* name, std::string& target)
{
    const std::string value = readEnv(name);
    if (!value.empty())
        target = value;
}

[[nodiscard]] const char* platformName() noexcept
{
#if defined(__EMSCRIPTEN__)
    return "wasm";
#elif defined(__ANDROID__)
    return "android";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "apple";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

[[nodiscard]] std::string compilerName()
{
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#elif defined(_MSC_VER)
    return std::string("msvc ") + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

[[nodiscard]] std::string utcTimestamp()
{
    const std::time_t now = std::time(nullptr);
    const std::tm* utc = std::gmtime(&now);

    if (utc == nullptr)
        return "unknown";

    char buffer[32] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", utc) == 0)
        return "unknown";

    return buffer;
}

[[nodiscard]] ink::EnhancedJson summaryToJson(const StatSummary& summary)
{
    ink::EnhancedJson json = ink::EnhancedJson::object();

    json["count"]  = summary.count;
    json["min"]    = summary.min;
    json["max"]    = summary.max;
    json["mean"]   = summary.mean;
    json["median"] = summary.median;
    json["stddev"] = summary.stddev;
    json["p95"]    = summary.p95;
    json["p99"]    = summary.p99;
    json["mad"]    = summary.mad;

    return json;
}

[[nodiscard]] ink::EnhancedJson makeCheck(const char* name, bool passed, std::string detail)
{
    ink::EnhancedJson check = ink::EnhancedJson::object();

    check["name"]   = name;
    check["status"] = passed ? "pass" : "fail";
    check["detail"] = std::move(detail);

    return check;
}

} // namespace

DebugModeConfig DebugModeConfig::fromSettings(AuraSettings* settings)
{
    DebugModeConfig config;

    if (settings != nullptr)
    {
        if (ink::EnhancedJson* json = settings->getSettings(); json != nullptr)
        {
            config.reportPath =
                json->getPath<std::string>("/debug/report_path", config.reportPath);
            config.label =
                json->getPath<std::string>("/debug/label", config.label);
            config.warmupFrames =
                json->getPath<u32>("/debug/warmup_frames", config.warmupFrames);
            config.sampleCapacity =
                json->getPath<u32>("/debug/sample_capacity", config.sampleCapacity);
            config.targetFrames =
                json->getPath<u32>("/debug/target_frames", config.targetFrames);
            config.frameBudgetMillis =
                json->getPath<f64>("/debug/frame_budget_ms", config.frameBudgetMillis);
            config.maxOverBudgetRatio =
                json->getPath<f64>("/debug/max_over_budget_ratio", config.maxOverBudgetRatio);
            config.leakSlopeBytesPerFrame =
                json->getPath<f64>("/debug/leak_slope_bytes_per_frame", config.leakSlopeBytesPerFrame);
            config.autoFlushIntervalFrames =
                json->getPath<u32>("/debug/auto_flush_interval_frames", config.autoFlushIntervalFrames);
            config.exitOnComplete =
                json->getPath<bool>("/debug/exit_on_complete", config.exitOnComplete);
        }
    }

    applyEnv("AURA_DEBUG_REPORT",    config.reportPath);
    applyEnv("AURA_DEBUG_LABEL",     config.label);
    applyEnv("AURA_DEBUG_FRAMES",    config.targetFrames);
    applyEnv("AURA_DEBUG_WARMUP",    config.warmupFrames);
    applyEnv("AURA_DEBUG_BUDGET_MS", config.frameBudgetMillis);
    applyEnv("AURA_DEBUG_EXIT",      config.exitOnComplete);

    config.sampleCapacity    = std::max(config.sampleCapacity, 1u);
    config.frameBudgetMillis = std::max(config.frameBudgetMillis, 0.001);

    return config;
}

DebugMode::DebugMode(DebugModeConfig config)
    : _config(std::move(config))
{
    AllocationTracker::ScopedMute mute;

    _samples.resize(_config.sampleCapacity);
    _warmupRemaining = _config.warmupFrames;

    INK_INFO << "DebugMode: capturing to '" << _config.reportPath << "' (warmup "
             << _config.warmupFrames << " frames, budget " << _config.frameBudgetMillis
             << " ms, target " << _config.targetFrames << " frames)";

    if (!AllocationTracker::isHooked())
    {
        INK_WARN << "DebugMode: CPU allocation hooks are not compiled in; "
                    "the memory section will be reported as untracked";
    }
}

DebugMode::~DebugMode()
{
    if (_reportWritten || _capturedFrames == 0)
        return;

    if (flushReport())
        INK_INFO << "DebugMode: report written to '" << _config.reportPath << "'";
    else
        INK_ERROR << "DebugMode: failed to write report to '" << _config.reportPath << "'";
}

void DebugMode::attachRenderer(const IRenderer* renderer) noexcept
{
    _renderer = renderer;
}

bool DebugMode::finished() const noexcept
{
    return _config.targetFrames > 0 && _capturedFrames >= _config.targetFrames;
}

void DebugMode::onFrameSample(const FrameSample& sample) noexcept
{
    if (_warmupRemaining > 0)
    {
        --_warmupRemaining;
        return;
    }

    FrameRecord record;
    record.frameNanos      = sample.frameNanos;
    record.phaseNanos      = sample.phaseNanos;
    record.liveBytes       = AllocationTracker::get().liveBytes();
    record.allocationCount = AllocationTracker::get().allocationCount();

    if (_renderer != nullptr)
    {
        if (const IGpuDebugSource* gpu = _renderer->gpuDebugSource(); gpu != nullptr)
        {
            const GpuTimingStats timing = gpu->gpuTimingStats();
            record.gpuMillis = timing.available ? timing.frameMillis : 0.0;
        }
    }

    if (_capturedFrames == 0)
    {
        _baselineLiveBytes  = record.liveBytes;
        _baselineAllocCount = record.allocationCount;
        _minFrameNanos      = record.frameNanos;
        _maxFrameNanos      = record.frameNanos;
    }
    else
    {
        _minFrameNanos = std::min(_minFrameNanos, record.frameNanos);
        _maxFrameNanos = std::max(_maxFrameNanos, record.frameNanos);
    }

    _totalFrameNanos += record.frameNanos;

    for (u32 phase = 0; phase < kFramePhaseCount; ++phase)
        _phaseTotalNanos[phase] += record.phaseNanos[phase];

    _liveBytesTrend.add(static_cast<f64>(record.liveBytes));

    if (toMillis(record.frameNanos) > _config.frameBudgetMillis)
    {
        ++_overBudgetFrames;
        ++_consecutiveOverBudget;
        _maxConsecutiveOverBudget = std::max(_maxConsecutiveOverBudget, _consecutiveOverBudget);
    }
    else
    {
        _consecutiveOverBudget = 0;
    }

    _samples[_writeIndex] = record;

    if (++_writeIndex == _samples.size())
    {
        _writeIndex = 0;
        _wrapped    = true;
    }

    ++_capturedFrames;
    ++_framesSinceAutoFlush;
}

void DebugMode::update(f32 deltaSeconds) noexcept
{
    _wallSeconds += static_cast<f64>(deltaSeconds);

    if (_config.autoFlushIntervalFrames > 0 &&
        _framesSinceAutoFlush >= _config.autoFlushIntervalFrames)
    {
        _framesSinceAutoFlush = 0;

        const bool written = flushReport();
        _reportWritten = false;

        if (!written)
            INK_WARN << "DebugMode: interim report to '" << _config.reportPath << "' failed";
    }

    if (!finished() || _completionHandled)
        return;

    _completionHandled = true;

    const bool written = flushReport();

    INK_INFO << "DebugMode: captured " << _capturedFrames << " frames in "
             << _wallSeconds << " s; report "
             << (written ? "written to '" : "FAILED for '") << _config.reportPath << "'";

    if (!_config.exitOnComplete)
        return;

    INK_INFO << "DebugMode: exit_on_complete set; ending the process";
    std::_Exit(written ? EXIT_SUCCESS : EXIT_FAILURE);
}

std::vector<DebugMode::FrameRecord> DebugMode::orderedSamples() const
{
    std::vector<FrameRecord> ordered;

    if (_capturedFrames == 0)
        return ordered;

    if (!_wrapped)
    {
        ordered.assign(_samples.begin(),
                       _samples.begin() + static_cast<isize>(_writeIndex));
        return ordered;
    }

    ordered.reserve(_samples.size());
    ordered.insert(ordered.end(),
                   _samples.begin() + static_cast<isize>(_writeIndex), _samples.end());
    ordered.insert(ordered.end(),
                   _samples.begin(), _samples.begin() + static_cast<isize>(_writeIndex));

    return ordered;
}

ink::EnhancedJson DebugMode::buildFrameSection(const std::vector<FrameRecord>& ordered) const
{
    ink::EnhancedJson frame = ink::EnhancedJson::object();

    std::vector<f64> millis;
    millis.reserve(ordered.size());
    for (const FrameRecord& record : ordered)
        millis.push_back(toMillis(record.frameNanos));

    const StatSummary summary = BenchmarkStats::summarize(millis);
    frame["cpu_ms"] = summaryToJson(summary);

    ink::EnhancedJson fps = ink::EnhancedJson::object();
    fps["mean"]                  = summary.mean   > 0.0 ? 1000.0 / summary.mean   : 0.0;
    fps["median"]                = summary.median > 0.0 ? 1000.0 / summary.median : 0.0;
    fps["one_percent_low"]       = summary.p99    > 0.0 ? 1000.0 / summary.p99    : 0.0;
    fps["five_percent_low"]      = summary.p95    > 0.0 ? 1000.0 / summary.p95    : 0.0;
    frame["fps"] = std::move(fps);

    ink::EnhancedJson budget = ink::EnhancedJson::object();
    budget["target_ms"]                   = _config.frameBudgetMillis;
    budget["over_budget_frames"]          = _overBudgetFrames;
    budget["over_budget_ratio"]           = _capturedFrames > 0
                                                ? static_cast<f64>(_overBudgetFrames) /
                                                      static_cast<f64>(_capturedFrames)
                                                : 0.0;
    budget["max_consecutive_over_budget"] = _maxConsecutiveOverBudget;

    budget["probability_empirical"]  = BenchmarkStats::empiricalExceedance(millis, _config.frameBudgetMillis);
    budget["probability_normal_fit"] = BenchmarkStats::normalExceedance(summary, _config.frameBudgetMillis);
    frame["budget"] = std::move(budget);

    ink::EnhancedJson wholeRun = ink::EnhancedJson::object();
    wholeRun["frames"]  = _capturedFrames;
    wholeRun["mean_ms"] = _capturedFrames > 0
                              ? toMillis(_totalFrameNanos) / static_cast<f64>(_capturedFrames)
                              : 0.0;
    wholeRun["min_ms"]  = toMillis(_minFrameNanos);
    wholeRun["max_ms"]  = toMillis(_maxFrameNanos);
    frame["whole_run"] = std::move(wholeRun);

    return frame;
}

ink::EnhancedJson DebugMode::buildPhaseSection(const std::vector<FrameRecord>& ordered) const
{
    ink::EnhancedJson phases = ink::EnhancedJson::object();

    const f64 meanFrameMillis =
        _capturedFrames > 0 ? toMillis(_totalFrameNanos) / static_cast<f64>(_capturedFrames) : 0.0;

    std::vector<f64> millis;
    millis.reserve(ordered.size());

    for (u32 index = 0; index < kFramePhaseCount; ++index)
    {
        millis.clear();
        for (const FrameRecord& record : ordered)
            millis.push_back(toMillis(record.phaseNanos[index]));

        ink::EnhancedJson phase = summaryToJson(BenchmarkStats::summarize(millis));

        const f64 wholeRunMean = _capturedFrames > 0
                                     ? toMillis(_phaseTotalNanos[index]) /
                                           static_cast<f64>(_capturedFrames)
                                     : 0.0;

        phase["whole_run_mean_ms"] = wholeRunMean;
        phase["share_percent"] = meanFrameMillis > 0.0 ? 100.0 * wholeRunMean / meanFrameMillis : 0.0;

        phases[toString(static_cast<FramePhase>(index))] = std::move(phase);
    }

    millis.clear();
    for (const FrameRecord& record : ordered)
    {
        i64 accounted = 0;
        for (const i64 phaseNanos : record.phaseNanos)
            accounted += phaseNanos;

        millis.push_back(toMillis(record.frameNanos - accounted));
    }

    ink::EnhancedJson unscoped = summaryToJson(BenchmarkStats::summarize(millis));

    i64 accountedTotal = 0;
    for (const i64 phaseNanos : _phaseTotalNanos)
        accountedTotal += phaseNanos;

    const f64 unscopedMean = _capturedFrames > 0
                                 ? toMillis(_totalFrameNanos - accountedTotal) /
                                       static_cast<f64>(_capturedFrames)
                                 : 0.0;

    unscoped["whole_run_mean_ms"] = unscopedMean;
    unscoped["share_percent"] = meanFrameMillis > 0.0 ? 100.0 * unscopedMean / meanFrameMillis : 0.0;

    phases["unscoped"] = std::move(unscoped);

    return phases;
}

ink::EnhancedJson DebugMode::buildMemorySection() const
{
    ink::EnhancedJson memory = ink::EnhancedJson::object();
    ink::EnhancedJson cpu    = ink::EnhancedJson::object();

    cpu["tracked"] = AllocationTracker::isHooked();

    const AllocationStats stats = AllocationTracker::get().snapshot();

    cpu["live_bytes"]              = stats.liveBytes;
    cpu["peak_bytes"]              = stats.peakBytes;
    cpu["total_allocated_bytes"]   = stats.totalAllocatedBytes;
    cpu["total_freed_bytes"]       = stats.totalFreedBytes;
    cpu["allocation_count"]        = stats.allocationCount;
    cpu["free_count"]              = stats.freeCount;
    cpu["outstanding_bytes"]       = stats.outstandingBytes();
    cpu["outstanding_allocations"] = stats.outstandingAllocations();

    ink::EnhancedJson window = ink::EnhancedJson::object();

    const auto frames = static_cast<f64>(_capturedFrames);
    const u64 windowAllocations = stats.allocationCount >= _baselineAllocCount
                                      ? stats.allocationCount - _baselineAllocCount
                                      : 0;

    window["live_bytes_delta"]      = static_cast<i64>(stats.liveBytes) -
                                      static_cast<i64>(_baselineLiveBytes);
    window["allocations"]           = windowAllocations;
    window["allocations_per_frame"] = frames > 0.0 ? static_cast<f64>(windowAllocations) / frames : 0.0;
    cpu["window"] = std::move(window);

    ink::EnhancedJson trend = ink::EnhancedJson::object();
    trend["slope_bytes_per_frame"] = _liveBytesTrend.slope();
    trend["samples"]               = _liveBytesTrend.count();
    cpu["trend"] = std::move(trend);

    ink::EnhancedJson sizeClasses = ink::EnhancedJson::array();
    for (usize index = 0; index < AllocationStats::kSizeClassCount; ++index)
    {
        if (stats.sizeClasses[index] == 0)
            continue;

        ink::EnhancedJson bucket = ink::EnhancedJson::object();
        bucket["min_bytes"] = AllocationTracker::sizeClassLowerBound(index);
        bucket["count"]     = stats.sizeClasses[index];

        if (index + 1 < AllocationStats::kSizeClassCount)
            bucket["max_bytes"] = AllocationTracker::sizeClassLowerBound(index + 1) - 1;

        sizeClasses.push_back(std::move(bucket));
    }
    cpu["size_classes"] = std::move(sizeClasses);

    memory["cpu"] = std::move(cpu);

    return memory;
}

ink::EnhancedJson DebugMode::buildGpuSection(const std::vector<FrameRecord>& ordered) const
{
    ink::EnhancedJson gpu = ink::EnhancedJson::object();

    const IGpuDebugSource* source = _renderer != nullptr ? _renderer->gpuDebugSource() : nullptr;
    if (source == nullptr)
    {
        gpu["available"] = false;
        gpu["reason"]    = "backend exposes no GPU debug source";
        return gpu;
    }

    gpu["available"] = true;
    gpu["backend"]   = source->gpuDebugBackendName();

    const GpuTimingStats timing = source->gpuTimingStats();

    ink::EnhancedJson timingJson = ink::EnhancedJson::object();
    timingJson["available"]       = timing.available;
    timingJson["dropped_samples"] = timing.droppedSamples;

    if (timing.available)
    {
        std::vector<f64> millis;
        millis.reserve(ordered.size());
        for (const FrameRecord& record : ordered)
            millis.push_back(record.gpuMillis);

        timingJson["frame_ms"] = summaryToJson(BenchmarkStats::summarize(millis));
    }

    gpu["timing"] = std::move(timingJson);

    const GpuMemoryStats memory = source->gpuMemoryStats();

    ink::EnhancedJson memoryJson = ink::EnhancedJson::object();
    memoryJson["available"] = memory.available;

    if (memory.available)
    {
        ink::EnhancedJson device = ink::EnhancedJson::object();
        device["live_bytes"]       = memory.deviceBytesLive;
        device["peak_bytes"]       = memory.deviceBytesPeak;
        device["in_use_bytes"]     = memory.deviceBytesInUse;
        device["allocation_count"] = memory.deviceAllocationCount;
        device["free_count"]       = memory.deviceFreeCount;
        device["live_blocks"]      = memory.deviceBlocksLive;

        device["reserved_bytes"] = memory.deviceBytesLive >= memory.deviceBytesInUse
                                       ? memory.deviceBytesLive - memory.deviceBytesInUse
                                       : 0;
        memoryJson["device"] = std::move(device);

        ink::EnhancedJson host = ink::EnhancedJson::object();
        host["live_bytes"]       = memory.hostBytesLive;
        host["peak_bytes"]       = memory.hostBytesPeak;
        host["allocation_count"] = memory.hostAllocationCount;
        host["free_count"]       = memory.hostFreeCount;
        memoryJson["host"] = std::move(host);

        ink::EnhancedJson budget = ink::EnhancedJson::object();
        budget["device_local_bytes"] = memory.budgetBytes;
        budget["usage_bytes"]        = memory.budgetUsageBytes;
        memoryJson["budget"] = std::move(budget);
    }

    gpu["memory"] = std::move(memoryJson);

    return gpu;
}

ink::EnhancedJson DebugMode::buildVerdictSection(const ink::EnhancedJson& frame,
                                                  const ink::EnhancedJson& memory) const
{
    ink::EnhancedJson verdict = ink::EnhancedJson::object();
    ink::EnhancedJson checks  = ink::EnhancedJson::array();

    const bool sampled = _capturedFrames > 0;
    checks.push_back(makeCheck("frames_captured", sampled,
                               std::to_string(_capturedFrames) + " frames captured"));

    const f64 overBudgetRatio = frame.getPath<f64>("/budget/over_budget_ratio", 0.0);
    const bool budgetOk = overBudgetRatio <= _config.maxOverBudgetRatio;
    checks.push_back(makeCheck(
        "frame_budget", budgetOk,
        std::to_string(100.0 * overBudgetRatio) + "% of frames over " +
            std::to_string(_config.frameBudgetMillis) + " ms (limit " +
            std::to_string(100.0 * _config.maxOverBudgetRatio) + "%)"));

    const f64 leakSlope = memory.getPath<f64>("/cpu/trend/slope_bytes_per_frame", 0.0);

    const bool leakOk = !AllocationTracker::isHooked() ||
                        leakSlope <= _config.leakSlopeBytesPerFrame;
    checks.push_back(makeCheck(
        "cpu_memory_trend", leakOk,
        std::to_string(leakSlope) + " bytes/frame of retained growth (limit " +
            std::to_string(_config.leakSlopeBytesPerFrame) + ")"));

    const bool passed = sampled && budgetOk && leakOk;

    verdict["status"] = passed ? "pass" : "fail";
    verdict["checks"] = std::move(checks);

    return verdict;
}

ink::EnhancedJson DebugMode::buildReport() const
{
    AllocationTracker::ScopedMute mute;

    const std::vector<FrameRecord> ordered = orderedSamples();

    ink::EnhancedJson report = ink::EnhancedJson::object();

    report["schema"]        = kSchemaVersion;
    report["generated_utc"] = utcTimestamp();
    report["label"]         = _config.label;

    ink::EnhancedJson build = ink::EnhancedJson::object();
    build["engine_version"] = AURA_VERSION_STRING;
    build["platform"]       = platformName();
    build["compiler"]       = compilerName();
    build["cpp_standard"]   = static_cast<u64>(__cplusplus);
#ifdef NDEBUG
    build["configuration"] = "release";
#else
    build["configuration"] = "debug";
#endif
    build["allocation_hooks"] = AllocationTracker::isHooked();
#ifdef AURA_PROFILE_FRAME
    build["frame_profiler"] = true;
#else
    build["frame_profiler"] = false;
#endif
    report["build"] = std::move(build);

    ink::EnhancedJson run = ink::EnhancedJson::object();
    run["backend"] = _renderer != nullptr
                         ? RendererChoiceToString(_renderer->getBackendType())
                         : "none";
    run["frames_captured"] = _capturedFrames;
    run["frames_warmup"]   = _config.warmupFrames;
    run["wall_seconds"]    = _wallSeconds;
    run["sample_window"]   = ordered.size();
    run["sample_capacity"] = _samples.size();

    run["window_truncated"] = _wrapped;
    report["run"] = std::move(run);

    ink::EnhancedJson frame  = buildFrameSection(ordered);
    ink::EnhancedJson memory = buildMemorySection();

    report["verdict"] = buildVerdictSection(frame, memory);
    report["frame"]   = std::move(frame);
    report["phases"]  = buildPhaseSection(ordered);
    report["gpu"]     = buildGpuSection(ordered);
    report["memory"]  = std::move(memory);

    return report;
}

bool DebugMode::flushReport(const std::string& path) const
{
    AllocationTracker::ScopedMute mute;

    const bool written = buildReport().saveToFile(path, /*pretty=*/true, /*indent=*/2);
    _reportWritten = _reportWritten || written;

    return written;
}

} // namespace aura3d
