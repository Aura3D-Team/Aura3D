// Port of Redixel's tools/fps-bench to Aura3D: same scene, tiers, schedule, timing windows and statistics,
// and the same JSON report, so the two engines are measured against one standard.
#include "aura/Core/Engine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <vector>

using namespace aura3d;
using Clock = std::chrono::steady_clock;

// Counts every operator new in the process (engine included); the bench reports the per-frame delta.
std::atomic<unsigned long long> g_allocCount{0}, g_allocBytes{0};

void *operator new(std::size_t n)
{
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    g_allocBytes.fetch_add(n, std::memory_order_relaxed);
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return operator new(n); }
void *operator new(std::size_t n, std::align_val_t a)
{
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    g_allocBytes.fetch_add(n, std::memory_order_relaxed);
    const auto al = static_cast<std::size_t>(a);
    if (void *p = std::aligned_alloc(al, (n + al - 1) / al * al))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n, std::align_val_t a) { return operator new(n, a); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { std::free(p); }

namespace
{
constexpr double kWarmupS = 0.5;
constexpr int kWarmupFrames = 30;
constexpr double kMeasureS = 2.0;
constexpr int kRounds = 3;
constexpr std::size_t kSampleCapacity = 1u << 17;

struct Tier
{
    int quads;
    const char *scene;
};
constexpr std::array<Tier, 5> kTiers{{{100, "100 quads"},
                                       {500, "500 quads"},
                                       {2000, "2000 quads"},
                                       {5000, "5000 quads"},
                                       {8000, "8000 quads"}}};

struct Sample
{
    double avgFps, avgFrameMs, p95Ms, p99Ms, low1Fps, frames, allocsPerFrame, bytesPerFrame;
};

double round2(double v) { return std::round(v * 100.0) / 100.0; }
double round3(double v) { return std::round(v * 1000.0) / 1000.0; }

double low1pctFps(const std::vector<double> &samples)
{
    std::vector<double> sorted(samples);
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    const std::size_t n = (samples.size() + 99) / 100;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        sum += sorted[i];
    return static_cast<double>(n) / sum;
}

double percentileMs(const std::vector<double> &samples, double p)
{
    std::vector<double> sorted(samples);
    std::sort(sorted.begin(), sorted.end());
    const auto idx = static_cast<std::size_t>(std::llround((static_cast<double>(sorted.size()) - 1.0) * p));
    return sorted[idx] * 1000.0;
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const std::size_t mid = v.size() / 2;
    return v.size() % 2 == 0 ? (v[mid - 1] + v[mid]) / 2.0 : v[mid];
}
} // namespace

int main(int argc, char **argv)
{
    const std::string outPath = argc > 1 ? argv[1] : "benchmark_result.json";
    std::vector<std::size_t> schedule;
    const char *only = std::getenv("FPS_BENCH_QUADS");
    for (int r = 0; r < kRounds; ++r)
        for (std::size_t t = 0; t < kTiers.size(); ++t)
            if (!only || kTiers[t].quads == std::atoi(only))
                schedule.push_back(t);

    AuraConfig config;
    config.set(setting::rendererApi, std::string{"vulkan"})
        .set(setting::windowWidth, 1280)
        .set(setting::windowHeight, 720)
        .set(setting::windowTitle, std::string{"aura-fps-bench"})
        .set(setting::presentMode, VSyncMode::Uncapped)
        .set(setting::presentFpsLimit, 0)
        .set(setting::audioBackend, wma::AudioBackend::Null)
        .set(setting::loggingLevel, ink::LogLevel::INFO);
    wma::WindowBackend backend{};
    const char *window = std::getenv("AURA_BENCH_WINDOW");
    if (WindowBackendFromString(window ? window : "wayland", backend))
        config.set(setting::windowBackend, backend);

    Engine engine(config);
    IRenderer *renderer = engine.getRenderer();
    renderer->setClearColor(0.05f, 0.05f, 0.08f);

    std::vector<std::vector<Sample>> collected(kTiers.size());
    std::vector<double> samples;
    samples.reserve(kSampleCapacity);
    constexpr int kMaxQuads = 8000;
    std::vector<gfx::BatchVertex> vertices(kMaxQuads * 4);
    std::vector<u32> indices(kMaxQuads * 6);
    std::size_t step = 0;
    bool stepStarted = false;
    Clock::time_point stepStart{}, last = Clock::now();
    int warmupFrames = 0;
    double measured = 0.0;
    unsigned long long allocStartCount = 0, allocStartBytes = 0;

    renderer->run(
        [&]
        {
            const Clock::time_point now = Clock::now();
            const double delta = std::chrono::duration<double>(now - last).count();
            last = now;

            if (step < schedule.size())
            {
                if (!stepStarted)
                {
                    stepStart = now;
                    stepStarted = true;
                }
                if (std::chrono::duration<double>(now - stepStart).count() < kWarmupS || warmupFrames < kWarmupFrames)
                    ++warmupFrames;
                else
                {
                    if (samples.empty())
                    {
                        allocStartCount = g_allocCount.load();
                        allocStartBytes = g_allocBytes.load();
                    }
                    samples.push_back(delta);
                    measured += delta;
                    if (measured >= kMeasureS)
                    {
                        const double frames = static_cast<double>(samples.size());
                        collected[schedule[step]].push_back(
                            {frames / measured, measured / frames * 1000.0, percentileMs(samples, 0.95),
                             percentileMs(samples, 0.99), low1pctFps(samples), frames,
                             static_cast<double>(g_allocCount.load() - allocStartCount) / frames,
                             static_cast<double>(g_allocBytes.load() - allocStartBytes) / frames});
                        samples.clear();
                        measured = 0.0;
                        stepStarted = false;
                        warmupFrames = 0;
                        if (++step >= schedule.size())
                        {
                            renderer->getWindowManager()->close();
                            return;
                        }
                    }
                }
            }

            const int quads = kTiers[schedule[std::min(step, schedule.size() - 1)]].quads;
            constexpr int kCols = 80;
            constexpr float kCell = 16.0f;
            constexpr glm::vec4 kColor{0.9f, 0.4f, 0.2f, 1.0f};
            for (int i = 0; i < quads; ++i)
            {
                const glm::vec2 o{static_cast<float>(i % kCols) * kCell, static_cast<float>(i / kCols) * kCell};
                const glm::vec2 e = o + glm::vec2{kCell - 2.0f};
                gfx::BatchVertex *v = &vertices[static_cast<std::size_t>(i) * 4];
                v[0] = {{o, 0.0f}, {}, kColor};
                v[1] = {{e.x, o.y, 0.0f}, {}, kColor};
                v[2] = {{e, 0.0f}, {}, kColor};
                v[3] = {{o.x, e.y, 0.0f}, {}, kColor};
                const auto b = static_cast<u32>(i * 4);
                u32 *q = &indices[static_cast<std::size_t>(i) * 6];
                q[0] = b, q[1] = b + 1, q[2] = b + 2, q[3] = b + 2, q[4] = b + 3, q[5] = b;
            }
            if (!renderer->beginRenderPass())
                return;
            renderer->drawBatch(std::span{vertices}.first(static_cast<std::size_t>(quads) * 4),
                                std::span{indices}.first(static_cast<std::size_t>(quads) * 6), {});
            renderer->endRenderPass();
        });

    std::FILE *f = std::fopen(outPath.c_str(), "w");
    if (!f)
        return 1;
    std::fprintf(f, "{\n  \"rounds\": %d,\n  \"warmup_ms\": 500,\n  \"warmup_frames\": %d,\n  \"measure_ms\": 2000,\n"
                    "  \"tiers\": [\n", kRounds, kWarmupFrames);
    bool first = true;
    for (std::size_t t = 0; t < kTiers.size(); ++t)
    {
        const auto &s = collected[t];
        if (s.empty())
            continue;
        const auto field = [&](double Sample::*m)
        {
            std::vector<double> v;
            for (const Sample &x : s)
                v.push_back(x.*m);
            return median(v);
        };
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const Sample &x : s)
            lo = std::min(lo, x.avgFps), hi = std::max(hi, x.avgFps);
        const double med = field(&Sample::avgFps);
        std::fprintf(f,
                     "%s    {\"scene\": \"%s\", \"rounds\": %zu, \"median_fps\": \"%.2f\", \"fps_min\": \"%.2f\", "
                     "\"fps_max\": \"%.2f\", \"spread_pct\": \"%.1f\", \"median_low_1pct_fps\": \"%.2f\", "
                     "\"median_frame_ms\": \"%.3f\", \"median_p95_frame_ms\": \"%.3f\", \"median_p99_frame_ms\": "
                     "\"%.3f\", \"median_frames\": %.1f, \"allocs_per_frame\": \"%.2f\", \"bytes_per_frame\": \"%.2f\"}",
                     first ? "" : ",\n", kTiers[t].scene, s.size(), round2(med), round2(lo), round2(hi),
                     (hi - lo) / med * 100.0, round2(field(&Sample::low1Fps)), round3(field(&Sample::avgFrameMs)),
                     round3(field(&Sample::p95Ms)), round3(field(&Sample::p99Ms)), field(&Sample::frames),
                     round2(field(&Sample::allocsPerFrame)), round2(field(&Sample::bytesPerFrame)));
        first = false;
        std::printf("%s: %.2f fps median (range %.2f-%.2f, 1%% low %.2f)\n", kTiers[t].scene, med, lo, hi,
                    field(&Sample::low1Fps));
    }
    std::fprintf(f, "\n  ]\n}\n");
    std::fclose(f);
    return 0;
}
