# Issue #5 — Add benchmark/debug mode with CPU/GPU allocation tracking and metrics reporting

> Task description drafted for github.com/Aura3D-Team/Aura3D/issues/5 (kept
> local, not posted). Original title was:
> "Create a debug (benchmark) mode that can be used in pipeline performance
> tests and in dev mode, in a way we can track allocations (CPU and GPU) and
> generate report file with all calculated metrics possible together with
> aritmetic median and propability if needed" — rewritten above for scannability.

## Summary
Add an opt-in `AURA_ENABLE_DEBUG_MODE` build that instruments the engine to
track CPU allocations, Vulkan GPU allocations and GPU frame timing, and
per-phase CPU frame timing (extending the existing Vulkan-only
`FrameProfiler` to the OpenGL and CPU backends too), then aggregates all of
it into a JSON report file — usable both interactively in dev builds and
headlessly in CI performance-regression pipelines.

## Why
Aura3D currently has no way to answer "did this change regress perf or leak
memory?" without manually attaching a profiler. Confirmed by codebase search:
`aura3d::FrameProfiler` (`engine/include/aura/Core/Profiling/FrameProfiler.h`)
already exists and does CPU wall-clock phase timing, but it's wired into
**only** `VulkanRenderer.cpp` (OpenGL/CPU backends have zero hooks), it only
logs a rolling average via `INK_INFO` every 2000 frames, and it has no GPU
timing, no allocation tracking, and no file output. A second, narrower switch,
`AURA_PROFILE_DRAW_RECORDING`, exists only as ad hoc instrumentation in
`apps/Sandbox/main.cpp` around `drawMeshes()`. No `Benchmark`, `Metrics`,
`AllocationTracker`, GPU query wrapper, or stats-aggregation (mean/median/
stddev/percentile) helper exists anywhere in `Aura3D`, `libwma`, or `libink`
— this is greenfield work, not a refactor of something partial.

## Architecture

### 1. CPU allocation tracking
New `engine/include/aura/Core/DebugMode/AllocationTracker.h` + `.cpp`:
`aura3d::AllocationTracker`, a thread-safe (atomic counters) singleton
tracking live bytes, peak bytes, alloc count, and free count. Hooked in via
global `operator new`/`operator delete` overrides, compiled **only** inside
`#ifdef AURA_ENABLE_DEBUG_MODE`-guarded translation unit
(`engine/src/Core/DebugMode/AllocationHooks.cpp`) so release builds never pay
for it and the "no manual `new`/`delete` in engine code" rule stays about
application code, not this tracking shim. Exposes a
`[[nodiscard]] AllocationStats snapshot() const noexcept` struct (current
bytes, peak bytes, alloc/free counts) for the report writer.

### 2. GPU allocation + timing tracking (Vulkan-only for v1)
- **Allocation**: promote the currently comment-only `VkAllocationCallbacks`
  references in `engine/include/aura/Renderer/Vulkan/VkAura/VkTextureManager/VkTextureManager.h`
  and `.../VkDebugger/VkDebugger.h` into a real counting allocator
  (`aura3d::VkCountingAllocator`, new file
  `engine/include/aura/Renderer/Vulkan/VkAura/VkDebugMode/VkCountingAllocator.h`)
  passed as the `pAllocator` to `vkCreate*`/`vkAllocateMemory`/`vkDestroy*`
  calls in `VulkanRenderer.cpp` and its `VkAura/` helpers, tracking
  device-memory bytes allocated/freed and live `VkDeviceMemory` object count.
- **Timing**: new `VkQueryPool`-based timestamp instrumentation
  (`engine/include/aura/Renderer/Vulkan/VkAura/VkDebugMode/VkTimestampQuery.h`)
  used alongside the existing `FrameProfiler::Scope` RAII pattern in
  `VulkanRenderer.cpp`, giving actual GPU-side phase timings (today's
  `FrameProfiler` is CPU wall-clock only, so "record" time and "GPU execute"
  time are currently indistinguishable).
- OpenGL and the software/CPU backend get CPU-side `FrameProfiler` coverage
  (see §3) but **no** GPU timestamp queries in this issue — see Out of scope.

### 3. Extend `FrameProfiler` to OpenGL + CPU backends
Close the existing gap: add the same `AURA_FRAME_SCOPE(phase)`/`AURA_FRAME_END()`
instrumentation already used in `VulkanRenderer.cpp` (lines ~975–1431) to
`engine/src/Renderer/OpenGL/OpenGLRenderer.cpp` and
`engine/src/Renderer/Software/CPURenderer.cpp`, reusing the existing
`FramePhase` enum and `FrameProfiler::get()` singleton — no new profiler
class needed here, just wiring the current one into the other two backends.

### 4. Stats aggregation
New header `engine/include/aura/Core/Profiling/BenchmarkStats.h`:
`aura3d::BenchmarkStats`, a small `constexpr`-friendly utility computing
min/max/arithmetic-mean/median/stddev/p95 over a `std::span<const float>` of
frame-time (or any metric) samples. The original title's "probability if
needed" is interpreted here as an optional normal-distribution fit
(mean/stddev) yielding a rough `P(frame_time_ms > threshold)` estimate via
the error function — explicitly a stretch item (see Acceptance criteria),
not required for the issue to be considered done.

### 5. `aura3d::DebugMode` subsystem + report writer
New `engine/include/aura/Core/DebugMode/DebugMode.h` + `.cpp`, following the
same subsystem shape `Engine` already uses for `_renderer`/`_resources`
(`engine/include/aura/Core/Engine.h`): constructed as
`std::unique_ptr<aura3d::DebugMode> _debugMode` in `Engine`'s constructor
(only when `AURA_ENABLE_DEBUG_MODE` is compiled in), exposed via
`Engine::debugMode() const`. Collects `AllocationTracker::snapshot()`,
`FrameProfiler` phase breakdowns, `BenchmarkStats` aggregates, and (Vulkan
only) `VkCountingAllocator`/`VkTimestampQuery` results into one
`aura3d::EnhancedJson` document (reusing `ink::EnhancedJson`, the JSON type
already used to *read* `settings.json` — this is the first *writer* of it in
the codebase) and flushes it to disk — on `DebugMode::flushReport(path)` and
automatically on `Engine` shutdown when active. `apps/Sandbox/main.cpp` calls
`engine.debugMode()->update(dt)` once per frame inside the existing
`IRenderer::run(...)` loop lambda, same pattern the audio-engine plan
(issue #19) already established for per-frame subsystem updates.

### 6. CI / pipeline usage
A headless CI job (extending `.github/workflows/ci.yml`) builds with
`-DAURA_ENABLE_DEBUG_MODE=ON`, runs Sandbox (or a dedicated benchmark target)
for a fixed frame count against `NullAudioDevice`-style headless config, and
uploads the resulting JSON report as a build artifact — giving the "pipeline
performance tests" half of the original title a concrete home without
inventing a new CI concept.

## Platform notes
- **Linux (native)**: full coverage — CPU allocation tracking, Vulkan GPU
  allocation + timestamp queries, `FrameProfiler` on all three backends.
- **WASM (Emscripten)**: no `VkQueryPool`/Vulkan path (software/GL backends
  only there); CPU allocation tracking still works via the `operator new`/
  `delete` hooks, but wall-clock CPU timing precision depends on the browser
  environment — document as a caveat in `docs/`, not a blocker.
- **Android**: same as WASM — CPU tracking and `FrameProfiler` on whichever
  backend is active; Vulkan GPU tracking works if `AURA_ENABLE_VULKAN` is on.

## C++23 / style conformance
- `[[nodiscard]]` on `AllocationTracker::snapshot()`, `BenchmarkStats`'s
  computed-value accessors, `DebugMode::flushReport()`.
- `noexcept` on the allocation-hook functions and any stats getters that
  can't fail.
- `std::span<const float>` for sample buffers passed into `BenchmarkStats`,
  no pointer+length pairs.
- `std::atomic<u64>` counters in `AllocationTracker`, no manual locking on
  the hot allocation path.
- RAII throughout: `VkTimestampQuery`/`FrameProfiler::Scope` stay RAII
  scope-timers, matching the existing pattern; `DebugMode`'s destructor
  flushes any pending report before the owning `Engine` tears down
  `_renderer`.
- Doxygen `///` on every public class/method, matching `FrameProfiler`'s and
  `ResourceManager`'s existing `@class`/`@brief`/`@note` style.
- All new code strictly behind `#ifdef AURA_ENABLE_DEBUG_MODE` (or the
  narrower Vulkan-specific pieces additionally behind `AURA_HAS_VULKAN`) so a
  default (`OFF`) build has zero footprint — matches the existing
  `AURA_PROFILE_FRAME`/`AURA_PROFILE_DRAW_RECORDING` opt-in convention rather
  than the always-on `AURA_HAS_*` backend-selection convention.

## Build system
- New CMake option `option(AURA_ENABLE_DEBUG_MODE "Enable CPU/GPU allocation tracking and benchmark report generation" OFF)`
  in the root `CMakeLists.txt`, next to the existing
  `AURA_PROFILE_FRAME`/`AURA_PROFILE_DRAW_RECORDING` options, following their
  same direct-macro-reuse pattern (`target_compile_definitions(Aura3D PUBLIC AURA_ENABLE_DEBUG_MODE)`,
  no `AURA_HAS_` indirection — that indirection is reserved for renderer
  backend selection).
- `engine/CMakeLists.txt`: add the new `Core/DebugMode/*` and
  `Renderer/Vulkan/VkAura/VkDebugMode/*` sources unconditionally to the
  target (guarded internally by `#ifdef`), matching how `FrameProfiler.cpp`
  is already always compiled in and internally gated.
- No changes needed in `libwma`/`libink` — `wma::FrameTimer` (already
  provides `deltaTime`/`fps`) and `ink::EnhancedJson` (already a transitive
  dependency) are reused as-is.

## Testing
- `tests/test_allocation_tracker.cpp`: alloc/free a known set of objects,
  assert `AllocationTracker::snapshot()` byte/count deltas match exactly.
- `tests/test_benchmark_stats.cpp`: feed `BenchmarkStats` known sample sets
  (including edge cases: single sample, all-equal samples) and assert
  mean/median/stddev/p95 against hand-computed expected values.
- `tests/test_debug_mode_report.cpp`: drive `DebugMode` with synthetic
  `FrameProfiler`/`AllocationTracker` data, call `flushReport()`, parse the
  written JSON back with `ink::EnhancedJson` and assert required fields
  exist and round-trip correctly.
- Both new suites registered in `tests/CMakeLists.txt`'s `AURA_TEST_SUITES`
  list, using the existing `AURA_CHECK`/`AURA_CHECK_THROWS` macros from
  `tests/TestUtils.h` — no new test framework.
- `.github/workflows/ci.yml`: add a build+run leg with
  `-DAURA_ENABLE_DEBUG_MODE=ON`, uploading the generated report JSON as a
  workflow artifact (see Architecture §6).

## Documentation
- New `docs/13-debug-benchmark-mode.md` (next number after the audio doc
  proposed in issue #19's plan): how to enable the build option, read the
  JSON report, and wire a new metric into `BenchmarkStats`. Added to
  `docs/README.md`'s index and the root `README.md` feature list.
- `CHANGELOG.md` entry under `Unreleased`/`Added`.

## Explicitly out of scope for this issue
- GPU timestamp queries / GPU allocation tracking for the OpenGL and
  software (CPU) renderer backends — Vulkan only for v1 (`glQueryCounter`/
  `GL_TIMESTAMP` support is a natural follow-up issue).
- Any in-engine visual profiler UI/overlay (this issue is report-file output
  only, not a live inspector).
- Cross-run regression diffing/trend tooling in CI (this issue produces the
  raw JSON artifact; comparing it across runs is a separate issue).
- Per-allocation call-stack capture (only aggregate counters, not a full
  memory-leak-locator).
- The normal-distribution "probability" estimate in `BenchmarkStats` is a
  stretch goal, not required for acceptance.

## Acceptance criteria
- [ ] `AURA_ENABLE_DEBUG_MODE` CMake option added, default `OFF`, zero
      footprint when disabled.
- [ ] `aura3d::AllocationTracker` tracks CPU allocation byte/count deltas
      correctly (verified by unit test).
- [ ] `FrameProfiler` instrumentation extended to `OpenGLRenderer.cpp` and
      `CPURenderer.cpp` (currently Vulkan-only).
- [ ] Vulkan GPU allocation tracking (`VkCountingAllocator`) and GPU
      timestamp queries (`VkTimestampQuery`) implemented and wired into
      `VulkanRenderer.cpp`.
- [ ] `BenchmarkStats` computes min/max/mean/median/stddev/p95 correctly
      against known sample sets.
- [ ] `aura3d::DebugMode` subsystem wired into `Engine`
      (`engine.debugMode()`), updated once per frame from Sandbox's loop.
- [ ] `DebugMode::flushReport()` writes a valid JSON report file combining
      allocation stats, frame timing, and benchmark stats.
- [ ] CI job builds with `AURA_ENABLE_DEBUG_MODE=ON` and uploads a report
      artifact from a headless run.
- [ ] Unit tests for `AllocationTracker`, `BenchmarkStats`, and
      `DebugMode` report round-trip pass.
- [ ] `docs/13-debug-benchmark-mode.md` written; `CHANGELOG.md` updated.
