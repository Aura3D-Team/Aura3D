# 2. Project Configuration

An Aura3D app is configured from code, from a JSON file, or from both:

```cpp
Engine engine("settings.json");        // file only
Engine engine(config);                 // code only — no settings.json anywhere
Engine engine(config, "settings.json"); // code, overridden key by key by the file
```

Resolution runs in three layers, each overriding the one below it:

| Layer | Set by | Wins over |
|---|---|---|
| JSON document | `settings.json` | everything, key by key |
| `aura3d::AuraConfig` | your code | the built-ins |
| Built-in defaults | the engine | — |

A key the file omits reads from your `AuraConfig`; one that names no
`AuraConfig` field reads the engine's own default. A file that is missing or
unreadable is a warning, not an error — the app runs on the layers beneath it.

The engine reads it into an `aura3d::AuraSettings` singleton
(`AuraSettings::get()`), reachable from your own code via
`engine.getSettings()`. All engine subsystems read their configuration
through typed accessors on `AuraSettings` — you should too, rather than
reaching for the raw JSON, so a renamed key only breaks one place.

This is the complete, current schema. Every field below is marked **live**
(actually consumed) or **informational** (accepted but not yet wired to
behavior) — Aura3D had a history of settings that looked functional but did
nothing, so this doc is deliberately exact about which is which.

## Full example

```json
{
    "application": {
        "name": "My Game",
        "version": "1.0.0",
        "headless": false
    },
    "window": {
        "width": 1280,
        "height": 720,
        "title": "My Game",
        "backend": "SDL3",
        "resizable": true,
        "fullscreen": false,
        "vsync": true,
        "vsync_mode": "Fifo",
        "fps_limit": 60
    },
    "renderer": {
        "backend": "vulkan",
        "validation_layers": true
    },
    "graphics": {
        "gpu_preference": "discrete",
        "msaa_samples": 4,
        "cpu_threads": 0
    },
    "audio": {
        "backend": "auto",
        "master_volume": 1.0,
        "sample_rate": 48000,
        "channels": 2,
        "buffer_frames": 1024,
        "max_voices": 32
    },
    "paths": {
        "shaders": "./resources/shaders/",
        "textures": "./resources/textures/",
        "models": "./resources/models/",
        "audio": "./resources/audio/",
        "logs": "./logs/"
    },
    "memory": {
        "vma": {
            "buffer_device_address": true,
            "prefer_device_memory": true,
            "persistently_map_upload_buffers": true,
            "vulkan_api_version": "1.4",
            "preferred_large_heap_block_size_mb": 128
        }
    },
    "logging": {
        "level": "info",
        "write_to_file": false
    }
}
```

## `application` — *informational*

| Key | Type | Notes |
|---|---|---|
| `name`, `version` | string | Metadata only; not read by the engine. Put your own build tooling here if useful. |
| `headless` | bool | Not read by the engine — there is no headless/off-screen render path today. |

## `window` — live

| Key | Type | Default | Notes |
|---|---|---|---|
| `width`, `height` | int | 1280, 720 | Initial window size. |
| `title` | string | `"Aura3D"` | Window title, used verbatim by every backend. |
| `backend` | string | `"SDL3"` | Windowing library `wma` creates the window through: `"SDL3"`, `"GLFW"`, `"X11"`, or `"WAYLAND"` (case-insensitive). SDL3 is the only one exercised by all three renderer backends on every platform this engine targets — pick another only if you have a specific reason to (e.g. testing wma's X11/Wayland backends directly). An unrecognized value falls back to SDL3 with a warning, the same pattern `renderer.backend` uses. |
| `resizable` | bool | `true` | |
| `fullscreen` | bool | `false` | |
| `vsync` | bool | `false` | Legacy on/off switch. When `true` and `vsync_mode` is absent, resolves to `Fifo`. Also zeroes `fps_limit` (the display is the limiter). |
| `vsync_mode` | string | derived from `vsync` | One of `AutoVsync`, `AutoNoVsync`, `Fifo`, `FifoRelaxed`, `Immediate`, `Mailbox`. Vulkan-only — see [03-engine-and-renderer.md](03-engine-and-renderer.md#present-modes-vulkan). Falls back to `Fifo` if the surface doesn't support the requested mode. |
| `fps_limit` | int | 60 | Ignored when `vsync` is `true`. |

## `renderer` — live

| Key | Type | Default | Notes |
|---|---|---|---|
| `backend` | string | `"vulkan"` | `"vulkan"`, `"opengl"`, or `"cpu"` (also accepts `"software"`). An unsupported value or a backend not compiled in falls back through Vulkan → OpenGL → Software with a warning — see [03-engine-and-renderer.md](03-engine-and-renderer.md#backend-resolution--fallback). |
| `validation_layers` | bool | `true` in debug builds, `false` in release | Vulkan only. Enables `VK_LAYER_KHRONOS_validation`. |

## `graphics` — live

| Key | Type | Default | Notes |
|---|---|---|---|
| `gpu_preference` | string | `"discrete"` | `"discrete"`, `"integrated"`, or `"any"`. Steers Vulkan physical-device selection: `"discrete"`/`"integrated"` add a scoring bonus to that device type, `"any"` drops the type bonus entirely and lets capability (API version, image limits, geometry-shader support) decide alone. Vulkan only. |
| `msaa_samples` | int | 1 | Requested MSAA sample count (`1`, `2`, `4`, `8`, ...). Clamped down to the largest count the selected device actually supports for both color and depth attachments. `1` disables MSAA. Only the Vulkan backend implements this — OpenGL and the CPU backend always render at 1x regardless of this value. Vulkan only. |
| `cpu_threads` | int | 0 | Worker threads the CPU (software) backend's row-band rasterizer splits a frame across. `0` auto-detects via `std::thread::hardware_concurrency()`; a positive value pins the count instead — useful to leave headroom for other processes, or to force single-threaded rendering for profiling. CPU backend only; ignored by Vulkan and OpenGL. |

## `audio` — live

Independent of `window.backend`: GLFW, X11 and Wayland are display protocols
with no audio API, so the two settings never constrain each other. See
[12-audio.md](12-audio.md).

| Key | Type | Default | Notes |
|---|---|---|---|
| `backend` | string | `"auto"` | `"auto"`, `"alsa"`, `"sdl3"`, or `"null"` (`"sdl"`, `"none"` and `"silent"` are accepted spellings). `"auto"` resolves through libwma: ALSA on desktop Linux, SDL3 on Windows/Android/WASM/Apple. A backend this build of libwma does not have, or an unrecognized value, falls back to `"auto"` with a warning. If the chosen backend cannot open a device, libwma degrades automatically (ALSA → SDL3 → Null), so audio never fails outright — a machine with no sound hardware gets a silent device. |
| `master_volume` | float | 1.0 | Overall output gain, clamped to `[0, 1]` and applied after every per-voice gain. |
| `sample_rate` | int | 48000 | Requested output rate. The device may grant something else; `AudioEngine::sampleRate()` reports what it actually got. Clips are resampled to it once at load. |
| `channels` | int | 2 | Requested output channels. Stereo panning needs 2; a mono device plays spatial voices unpanned. |
| `buffer_frames` | int | 1024 | Frames per device callback — the latency dial (~21 ms at 48 kHz). Lower means tighter timing and more risk of dropouts under load. Treated as a hint: SDL3 in particular picks its own period size. |
| `max_voices` | int | 32 | Voices that can sound simultaneously. Beyond it `play()` returns an invalid handle and drops the sound rather than cutting off one already audible. Fixed at startup — the mixer never allocates. |

## `paths` — live

| Key | Type | Default | Notes |
|---|---|---|---|
| `shaders` | string | `./resources/shaders/` | Read by `AuraSettings::getShadersPath()`; not currently consumed by the built-in pipelines (they embed their SPIR-V/GLSL — see [10-platform-builds.md](10-platform-builds.md)), but available for your own shader loading. |
| `textures` | string | `./resources/textures/` | Convention used by `ResourceManager::loadTexture` callers — see `apps/Sandbox/main.cpp`'s `settings->getTexturesPath() + "crate.png"`. |
| `models` | string | `./resources/models/` | Same convention for `ResourceManager::loadMesh`. |
| `audio` | string | `./resources/audio/` | Same convention for `ResourceManager::loadSound` — see `apps/Sandbox/main.cpp`'s `settings->getAudioPath() + "orb_hum.wav"`. Generate the Sandbox's own audio with `scripts/gen_sandbox_audio.py`. |
| `logs` | string | `./logs/` | Directory the log file is written into when `logging.write_to_file` is `true`. Created automatically if missing. |

## `memory.vma` — live (Vulkan only)

Configuration for the Vulkan Memory Allocator (VMA), read directly by
`VulkanMemoryManager::loadConfig`.

| Key | Type | Default | Notes |
|---|---|---|---|
| `buffer_device_address` | bool | `true` | Enables `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`. |
| `prefer_device_memory` | bool | `true` | |
| `persistently_map_upload_buffers` | bool | `true` | |
| `vulkan_api_version` | string | `"1.4"` | Parsed into the `VmaAllocatorCreateInfo::vulkanApiVersion` VMA expects. |
| `preferred_large_heap_block_size_mb` | int | 128 | |

## `debug` — live (`AURA_ENABLE_DEBUG_MODE` builds only)

Read by `aura3d::DebugModeConfig::fromSettings`, and ignored entirely in a
normal build — `Engine::debugMode()` returns `nullptr` there, so nothing looks
at these keys. Every one of them can be overridden by an environment variable,
which is what lets one built binary serve several CI jobs; see
[13-debug-benchmark-mode.md](13-debug-benchmark-mode.md) for the full
explanation of each and for how to read the report they produce.

| Key | Type | Default | Env override | Notes |
|---|---|---|---|---|
| `report_path` | string | `"aura3d-benchmark.json"` | `AURA_DEBUG_REPORT` | Where the JSON report is written. |
| `label` | string | `""` | `AURA_DEBUG_LABEL` | Free-form tag copied into the report (a commit SHA, a scene name). |
| `warmup_frames` | int | 60 | `AURA_DEBUG_WARMUP` | Frames discarded before sampling starts. |
| `sample_capacity` | int | 20000 | — | Ring capacity for raw samples, ~96 bytes each. |
| `target_frames` | int | 0 | `AURA_DEBUG_FRAMES` | Capture this many, then flush. 0 runs until the application stops. |
| `frame_budget_ms` | float | 16.667 | `AURA_DEBUG_BUDGET_MS` | What a frame must stay under to count as on budget. |
| `max_over_budget_ratio` | float | 0.05 | — | Fraction of over-budget frames above which the verdict fails. |
| `leak_slope_bytes_per_frame` | float | 1024.0 | — | Retained growth per frame above which a leak is called. |
| `auto_flush_interval_frames` | int | 0 | — | Write an interim report every N frames; 0 writes only at the end. |
| `exit_on_complete` | bool | `false` | `AURA_DEBUG_EXIT` | End the process once `target_frames` are captured. For headless runs. |

## `logging` — live

| Key | Type | Default | Notes |
|---|---|---|---|
| `level` | string | `"trace"` in debug builds, `"info"` in release | Case-insensitive: `off`, `fatal`, `error`, `warn`, `info`, `debug`, `verbose`, `trace`. |
| `write_to_file` | bool | `false` | When `true`, also writes to `<paths.logs>/<application name>.log` (in addition to the console). The directory is created automatically. |

## Reading settings from your own code

```cpp
const aura3d::AuraSettings* settings = engine.getSettings();

settings->getWindowWidth();      // int
settings->getWindowBackend();    // wma::WindowBackend (SDL3/GLFW/X11/WAYLAND)
settings->getRendererBackend();  // std::string
settings->getMsaaSamples();      // int
settings->getCpuThreads();       // int (0 = auto-detect)
settings->defaults();            // the AuraConfig an unset key falls back to
settings->getSettings();         // raw ink::EnhancedJson*, for bespoke schemas
                                  // (this is how memory.vma.* reads its own keys)
```

For anything not covered by a typed accessor, `getSettings()->getPath<T>("/your/key", defaultValue)`
reaches the raw document directly — the same mechanism every accessor above
is built on.

## Configuring in code

`aura3d::AuraConfig` mirrors the schema above as a plain struct, one nested
group per JSON object. Set only what you care about; the rest keeps the
built-in default listed in each table.

```cpp
aura3d::AuraConfig config;

config.window.title    = "Viewer";
config.window.width    = 1024;
config.window.height   = 768;
config.window.backend  = wma::WindowBackend::SDL3;
config.renderer.backend = "opengl";
config.graphics.msaaSamples = 4;
config.paths.textures  = "./art/";

Engine engine(config);   // ships as one binary; no settings.json to lose
```

Four fields are `std::optional`, because "unset" is a real state distinct from
any value they could hold:

| Field | Unset means |
|---|---|
| `window.vsyncMode` | derive from `window.vsync` |
| `renderer.validationLayers` | on in a debug build, off under `NDEBUG` |
| `audio.backend` | `wma::getDefaultAudioBackend()` — what `"auto"` means in JSON |
| `logging.level` | `TRACE` in a debug build, `INFO` under `NDEBUG` |

`window.title` is the one string with a sentinel: empty takes
`APPLICATION_NAME`.

## Reloading at runtime

```cpp
aura3d::AuraSettings::get()->reload("settings.json");
```

Replaces the in-memory document, leaving the `AuraConfig` layer standing — a
reload that fails falls back to your code defaults, not to the engine's.
Values already consumed by live subsystems (window size, the active renderer,
...) are **not** re-applied automatically — this is for picking up hand-edited
config before creating something new, not a live-reload system.

Next: **[03-engine-and-renderer.md](03-engine-and-renderer.md)**.
