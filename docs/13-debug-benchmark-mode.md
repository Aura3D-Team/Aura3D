# 13. Debug & Benchmark Mode

Aura3D can be built to instrument itself: count every CPU and GPU allocation,
time every phase of every frame, and write the whole thing out as one JSON
report with a machine-readable pass/fail verdict. It is meant for two jobs at
once — answering *"did this change regress?"* in a CI pipeline, and answering
*"where is this frame going?"* at your desk.

It is off by default and costs nothing when off.

```bash
cmake -S . -B build -DAURA_ENABLE_DEBUG_MODE=ON
cmake --build build --parallel
```

Enabling it implies [`AURA_PROFILE_FRAME`](#what-gets-measured) — the report's
per-phase breakdown *is* the frame profiler's output, so turning one on without
the other would produce a report with an empty timing section.

---

## The 60-second version

Run any Aura3D application built this way and it writes
`aura3d-benchmark.json` next to itself when it exits. For a headless
pipeline run, point it somewhere and give it a frame budget:

```bash
AURA_DEBUG_REPORT=perf.json \
AURA_DEBUG_FRAMES=2000 \
AURA_DEBUG_EXIT=1 \
./Sandbox

jq '.verdict.status' perf.json     # "pass" or "fail"
```

No recompilation and no config edit: the environment variables below override
`settings.json`, so one built binary serves every job.

---

## What gets measured

| Source | What it counts | Backends |
|---|---|---|
| `aura3d::AllocationTracker` | Every `new`/`delete` in the process: live bytes, peak, counts, and a power-of-two size histogram | all |
| `aura3d::FrameProfiler` | Per-phase CPU wall time, per frame | all (Vulkan, OpenGL, software) |
| `vk::VkDeviceMemoryCounters` | Real VRAM, hooked at VMA's device-memory callbacks | Vulkan |
| `vk::VkCountingAllocator` | The driver's *host* bookkeeping, via `VkAllocationCallbacks` | Vulkan |
| `vk::VkTimestampQuery` | GPU wall time per frame, via a `VkQueryPool` | Vulkan |

Two of those distinctions matter more than they look:

**Device memory is not host memory.** Passing a `VkAllocationCallbacks` to
`vkAllocateMemory` counts the bytes of `malloc` the driver needed to *describe*
a device allocation — not the VRAM it made, which is usually three orders of
magnitude larger. Aura3D therefore counts device memory where it is really
allocated, in VMA's `pfnAllocate`/`pfnFree` callbacks, and reports host memory
separately. Both appear in the report; neither is labelled as the other.

**CPU frame time is not GPU frame time.** `FrameProfiler` measures wall clock on
the render thread, so on a GPU backend `Submit` is how long it took to *hand the
work over*, not how long the work took. The GPU's own answer comes from
timestamps written into the frame's command buffer. A frame that is GPU-bound
shows a small `Submit` and a large `gpu.timing.frame_ms`; one that is CPU-bound
shows the reverse. You cannot tell those apart from either number alone.

### Frame phases

The eight phases come from `aura3d::FramePhase`, and each backend fills in the
ones it has. A phase reported as `0.0` means *this backend has no such step* —
OpenGL has no fence to wait on — not that the step was free.

| Phase | Vulkan | OpenGL | Software |
|---|---|---|---|
| `WaitFence` | fence wait | — | — |
| `Acquire` | `vkAcquireNextImageKHR` | — | — |
| `BeginPass` | begin buffer + render pass | clear + uniform upload | framebuffer clear |
| `RecordScene` | threaded `drawMeshes()` | `drawMeshes()` | `drawMeshes()` |
| `RecordOverlay` | 2D batch | 2D batch | 2D batch |
| `EndPass` | replay secondaries, end pass | `glFlush` + unbind | **rasterise the whole queue** |
| `Submit` | `vkQueueSubmit` | — | — |
| `Present` | `vkQueuePresentKHR` | `SDL_GL_SwapWindow` | blit to the window |
| `unscoped` | everything outside any scope: the event pump, your game logic, a frame limiter's sleep |

`unscoped` is routinely the largest entry, and that is the point of reporting it
rather than quietly folding it into the others.

---

## Reading the report

```jsonc
{
  "schema": "aura3d.benchmark/1",
  "generated_utc": "2026-08-16T04:01:50Z",
  "label": "headless-smoke",
  "build":   { "platform": "linux", "compiler": "gcc 15.2", "configuration": "debug", ... },
  "run":     { "backend": "SOFTWARE", "frames_captured": 400, "wall_seconds": 3.36, ... },
  "verdict": { "status": "fail", "checks": [ ... ] },
  "frame":   { "cpu_ms": {...}, "fps": {...}, "budget": {...}, "whole_run": {...} },
  "phases":  { "RecordScene": {...}, "EndPass": {...}, "unscoped": {...}, ... },
  "gpu":     { "available": true, "timing": {...}, "memory": {...} },
  "memory":  { "cpu": { "trend": {...}, "window": {...}, "size_classes": [...] } }
}
```

### `verdict` — start here

```json
{
  "status": "fail",
  "checks": [
    { "name": "frames_captured",  "status": "pass", "detail": "400 frames captured" },
    { "name": "frame_budget",     "status": "fail", "detail": "5.5% of frames over 16.667 ms (limit 5.0%)" },
    { "name": "cpu_memory_trend", "status": "pass", "detail": "0.0059 bytes/frame of retained growth (limit 1024)" }
  ]
}
```

Three checks, each carrying the threshold it was judged against so the verdict
can be reproduced from the file alone. A run that captured **zero** frames fails
— an artifact full of zeroes that claimed to pass would be worse than no
artifact at all.

### `frame` — the distribution, not the average

A mean frame time is close to useless on its own: a run that averages 4 ms with
a 40 ms hitch every second and a run that is flat at 4 ms report the same
number and feel nothing alike. Every distribution here therefore carries
`min`/`max`/`mean`/`median`/`stddev`/`p95`/`p99`/`mad`.

- **`median` far below `mean`** — a few slow frames are dragging the average.
- **`mad` small while `stddev` is large** — a smooth run *with hitches*, which
  is exactly the shape a mean hides. (`mad` is the median absolute deviation:
  robust to outliers where `stddev` is defined by them.)
- **`fps.one_percent_low`** — the frame rate at the 99th percentile frame time,
  i.e. the speed of the slowest 1% of frames. This is the number a player
  notices. Note that FPS is derived from the frame-time percentiles, because
  the mean of `1/t` is not `1/mean(t)`, and the difference between them is
  precisely the hitches.

`frame.budget` reports two exceedance probabilities on purpose:

| Field | What it is | When to trust it |
|---|---|---|
| `probability_empirical` | The fraction of frames actually over budget | Always — it makes no assumptions |
| `probability_normal_fit` | `P(x > budget)` for a normal fit of the samples | Only as an order of magnitude, and mainly to put a number on a budget the run never once crossed |

They disagree exactly when the distribution is skewed, which for frame times it
always is. The pair is more informative than either alone.

`frame.whole_run` is exact over every frame captured. Everything else in
`frame` describes the sample **window** — see [Bounded memory](#bounded-memory-unbounded-runs).

### `memory.cpu` — the leak signal

```json
"trend":  { "slope_bytes_per_frame": 0.0059, "samples": 400 },
"window": { "live_bytes_delta": 24, "allocations": 57791, "allocations_per_frame": 144.5 }
```

`slope_bytes_per_frame` is a least-squares slope of live bytes against frame
index over the **whole** run, not a difference between endpoints. That
distinction is the whole point:

- A run that allocates and frees a megabyte every frame ends where it started
  and has a slope near **zero**. It is churning, not leaking.
- A run retaining a hundred bytes a frame has a slope of **100**, however small
  its totals look next to the megabytes above.

`allocations_per_frame` catches the other failure the byte totals hide: two
runs that allocate the same number of megabytes behave nothing alike if one
does it in a thousand large blocks and the other in ten million 32-byte ones.
`size_classes` shows which of those you have.

If `"tracked": false`, the global allocation hooks were not compiled in and
every number in this section is zero *because nothing was counting* — not
because nothing was allocated.

### `gpu`

`"available": false` with a `reason` is the honest answer from the software
rasteriser (no device) and from OpenGL (no timestamp queries wired up yet). On
Vulkan you get:

- `gpu.timing.frame_ms` — the GPU's own frame time distribution. It trails the
  CPU by the number of frames in flight, because reading a query for the frame
  just recorded would mean stalling the pipeline to measure it.
- `gpu.memory.device` — real VRAM. `reserved_bytes` is what the suballocator is
  holding but not handing out; a large value is fragmentation, which is a
  different problem from allocating too much.
- `gpu.memory.host` — the driver's CPU-side bookkeeping. Invisible to
  `AllocationTracker` (drivers use `malloc`, not `operator new`) and to every
  device-memory tool, so a leak here shows up in nothing but RSS without it.
- `gpu.memory.budget` — the driver's view of the device-local heaps.

---

## Configuration

Every key is optional. `settings.json` sets the defaults; the environment
overrides them.

### `settings.json`

```json
"debug": {
  "report_path": "aura3d-benchmark.json",
  "label": "",
  "warmup_frames": 60,
  "sample_capacity": 20000,
  "target_frames": 0,
  "frame_budget_ms": 16.667,
  "max_over_budget_ratio": 0.05,
  "leak_slope_bytes_per_frame": 1024.0,
  "auto_flush_interval_frames": 0,
  "exit_on_complete": false
}
```

| Key | Default | Notes |
|---|---|---|
| `report_path` | `aura3d-benchmark.json` | Written on shutdown, and whenever `target_frames` is reached. |
| `label` | `""` | Free-form tag copied into the report — a commit SHA, a scene name. |
| `warmup_frames` | 60 | Frames discarded before sampling. The first frames of any run are shader compilation, texture upload and cold pages; including them makes `p99` a measure of startup. |
| `sample_capacity` | 20000 | Ring capacity, ~96 bytes per frame (about 5 minutes at 60 FPS, 1.9 MB). |
| `target_frames` | 0 | Capture this many, then flush. 0 runs until the application stops. |
| `frame_budget_ms` | 16.667 | 60 FPS. What a frame must stay under to count as on budget. |
| `max_over_budget_ratio` | 0.05 | Fraction of over-budget frames above which the verdict fails. |
| `leak_slope_bytes_per_frame` | 1024.0 | Retained growth above which a leak is called. |
| `auto_flush_interval_frames` | 0 | Write an interim report every N frames. Non-zero is what makes a report survive a CI job's `timeout`. |
| `exit_on_complete` | `false` | End the process once `target_frames` are captured. See below. |

### Environment overrides

| Variable | Overrides |
|---|---|
| `AURA_DEBUG_REPORT` | `report_path` |
| `AURA_DEBUG_LABEL` | `label` |
| `AURA_DEBUG_FRAMES` | `target_frames` |
| `AURA_DEBUG_WARMUP` | `warmup_frames` |
| `AURA_DEBUG_BUDGET_MS` | `frame_budget_ms` |
| `AURA_DEBUG_EXIT` | `exit_on_complete` (`0`/`false`/`off` for false) |

### About `exit_on_complete`

It ends the process with `std::_Exit` — no destructors, no unwinding — right
after the report has been flushed and closed.

That is blunt, and deliberately so. There is no way to leave
`IRenderer::run()`'s loop from inside a frame: the window manager owns the loop
condition, and tearing the window down mid-frame would leave the renderer
presenting to a destroyed surface. `std::exit` is no better, since it runs
static destructors from a point where the renderer still holds live GPU objects.
`_Exit` runs nothing at all, which is exactly right when the only artifact of
the run is a file already on disk. It stays opt-in because of how blunt it is —
a dev session wants the window to stay up.

---

## Using it from your own application

`Engine::debugMode()` returns `nullptr` in a normal build, so the call site
needs no `#ifdef`:

```cpp
#include "aura/Core/DebugMode/DebugMode.h"

r->run([&]() {
    const float dt = ...;

    if (aura3d::DebugMode* debug = engine.debugMode())
        debug->update(dt);

    // ... the rest of your frame
});
```

`update()` advances the run's clock, writes any interim report, and handles
`exit_on_complete`. The frame samples themselves arrive through `FrameProfiler`
without any help from you.

The final report is written by `Engine`'s destructor whether or not anything
asked for it, so a session that ends by closing the window still produces one.

### Adding a metric

`aura3d::BenchmarkStats` is a plain utility over `std::span<const f64>` — no
state, no setup:

```cpp
#include "aura/Core/Profiling/BenchmarkStats.h"

std::vector<f64> samples = collectSomething();

const aura3d::StatSummary summary = aura3d::BenchmarkStats::summarize(samples);
// summary.median, .p99, .stddev, .mad ...

const f64 overBudget = aura3d::BenchmarkStats::empiricalExceedance(samples, 16.667);
const f64 drift      = aura3d::BenchmarkStats::trendSlope(samples);
```

For a metric sampled over a run longer than any buffer you want to keep, use the
streaming accumulator instead — four running sums, no samples retained:

```cpp
aura3d::BenchmarkStats::LinearTrend trend;
for (;;) trend.add(currentValue());   // over a million frames if you like
const f64 slope = trend.slope();
```

To put a new metric in the report itself, add it to a `build*Section()` in
`engine/src/Core/DebugMode/DebugMode.cpp` and bump `kSchemaVersion` if you
change what an existing field means.

---

## Bounded memory, unbounded runs

A benchmark runs for minutes and a dev session for hours, so raw samples live in
a fixed-capacity ring holding the most recent `sample_capacity` frames.
Everything a bounded window would distort is kept instead in O(1) accumulators
updated per frame:

| Exact over the whole run | Describes the recent window only |
|---|---|
| `run.frames_captured`, `run.wall_seconds` | `frame.cpu_ms.*`, `frame.fps.*` |
| `frame.whole_run.*` | `phases.*.{min,max,mean,median,stddev,p95,p99,mad}` |
| `frame.budget.*` | `gpu.timing.frame_ms` |
| `memory.cpu.trend.slope_bytes_per_frame` | |
| `phases.*.whole_run_mean_ms`, `phases.*.share_percent` | |

`run.window_truncated` is `true` once the ring has wrapped. When it is, compare
the windowed percentiles against `frame.whole_run` before drawing conclusions
about the run as a whole.

---

## CI usage

The `linux-benchmark` job in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)
is the worked example: it builds with `-DAURA_ENABLE_DEBUG_MODE=ON`, runs the
Sandbox headlessly against the software backend with `SDL_VIDEODRIVER=dummy`,
and uploads the JSON as a workflow artifact.

Two things make that reliable:

- `auto_flush_interval_frames` (via `settings.json`, or leave it and rely on
  `target_frames`) means a job killed by `timeout` still leaves a valid report.
- The job reads `.verdict.status` but does **not** fail the build on it. A
  shared CI runner has no GPU and unpredictable neighbours, so its absolute
  frame times are not a regression signal. The artifact is the deliverable;
  gating on it belongs on dedicated hardware with a `frame_budget_ms` chosen
  for that machine.

---

## Platform notes

- **Linux (native)** — everything: CPU allocation tracking, `FrameProfiler` on
  all three backends, Vulkan device/host memory and GPU timestamps.
- **Windows** — same as Linux.
- **WebAssembly (Emscripten)** — CPU allocation tracking and `FrameProfiler`
  work on the software and WebGL backends. There is no Vulkan path, so the
  `gpu` section reports unavailable. Wall-clock precision depends on the
  browser: `performance.now()` is deliberately coarsened against timing attacks,
  so treat sub-millisecond phase numbers there as indicative rather than exact.
- **Android** — CPU tracking and `FrameProfiler` on whichever backend is
  active; the full Vulkan section when `AURA_ENABLE_VULKAN` is on and the
  device's graphics queue family reports non-zero `timestampValidBits`.
- **Apple (Metal)** — CPU tracking and `FrameProfiler` only; the Metal backend
  does not implement `IGpuDebugSource`.

---

## Cost when enabled

Worth stating plainly, because it decides where you can use this:

- Global `operator new`/`delete` are **replaced**. Every allocation carries a
  16-byte header and a handful of relaxed atomic increments. Allocation-heavy
  code gets measurably slower — which is itself a reason not to compare
  absolute timings between a debug-mode build and a normal one.
- Two `steady_clock` reads per phase per frame, plus one sample (~96 bytes)
  appended to the ring per frame.
- Vulkan only: two timestamp writes per frame and one non-blocking query
  readback, all recorded into command buffers that already exist.

With `AURA_ENABLE_DEBUG_MODE` off, none of that is compiled: no allocation
hooks, no counters, no ring, and `Engine::debugMode()` is a `return nullptr`.

---

Back to the **[index](README.md)**.
