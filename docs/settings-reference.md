<!-- Generated from AURA_SETTINGS by the update-settings-docs target. Do not edit. -->

## `file`

| Key | Type | Default | Notes |
|---|---|---|---|
| `schema_version` | int | 1 | Layout version of the file; 1 is the only one. |

## `window`

| Key | Type | Default | Notes |
|---|---|---|---|
| `backend` | choice | "sdl3" | Library the window is created through. SDL3 is the one every renderer supports on every platform. One of `sdl3`, `glfw`, `x11`, `wayland`. |
| `width` | int | 1280 | Initial width in pixels. 1 to 16384. |
| `height` | int | 720 | Initial height in pixels. 1 to 16384. |
| `title` | text | "" | Window title. Empty takes the application name. |
| `resizable` | bool | true | Whether the user can resize the window. |
| `fullscreen` | bool | false | Start fullscreen. |
| `decorations` | choice | "server" | Who draws the title bar. `client` makes UIView draw it; a Wayland compositor may override either way. One of `server`, `client`. |

## `present`

| Key | Type | Default | Notes |
|---|---|---|---|
| `mode` | choice | "fifo" | How frames reach the display. `fifo` waits for it and never tears; `fifo_relaxed` may tear when late; `mailbox` does not wait and does not tear; `immediate` does not wait; `uncapped` is the best of those the surface has. OpenGL and Metal only take wait or no wait from it. One of `fifo`, `fifo_relaxed`, `mailbox`, `immediate`, `uncapped`. |
| `fps_limit` | int | 0 | Frame cap for the modes that do not wait for the display. 0 is uncapped. 0 to 1000. |

## `renderer`

| Key | Type | Default | Notes |
|---|---|---|---|
| `api` | choice | "auto" | Graphics API. `auto` is the platform's best; one that is unavailable falls back with a warning. One of `auto`, `vulkan`, `opengl`, `metal`, `software`. |
| `msaa` | int | 1 | MSAA sample count, clamped to what the device supports; 1 is off. Vulkan only. 0 to 64. |
| `frames_in_flight` | int | 2 | Frames the CPU may record ahead of the GPU. Vulkan only. 1 to 8. |
| `gpu_timing` | bool | on in AURA_PROFILE_FRAME builds | Measure each frame's GPU time. Ignored without AURA_PROFILE_FRAME. |
| `cpu_threads` | int | 0 | Worker threads of the job system and the software rasteriser. 0 detects the core count. 0 to 256. |
| `vulkan.validation` | bool | on in debug builds | Enable the Khronos validation layer. |
| `vulkan.gpu` | choice | "discrete" | Which kind of device to prefer. `any` lets capability decide alone. One of `discrete`, `integrated`, `any`. |
| `vulkan.api_version` | text | "1.4" | Vulkan API version the allocator is created for, capped at what the headers support. |
| `vulkan.transform_slots` | int | 1024 | Camera slots per frame in flight; a frame's cameras past this overwrite the last slot. 1 to 65536. |
| `vulkan.min_draws_to_thread` | int | 512 | Draws a batch needs before its recording is spread over the workers; fewer record inline. 0 to 1e+06. |
| `vulkan.min_draws_per_chunk` | int | 128 | Fewest draws one worker records, which caps how many chunks a batch is cut into. 1 to 1e+06. |
| `vulkan.descriptor_headroom` | int | 16 | Update-after-bind descriptors kept back for the engine's other bindings when sizing the texture table. 0 to 4096. |
| `vulkan.vma.buffer_device_address` | bool | true | Allow buffer device addresses, where the device has them. |
| `vulkan.vma.prefer_device_memory` | bool | true | Prefer device-local memory for resources. |
| `vulkan.vma.map_upload_buffers` | bool | true | Keep upload buffers mapped for their whole life. |
| `vulkan.vma.heap_block_mb` | int | 128 | Preferred size of one large heap block, in MiB. 1 to 4096. |

## `audio`

| Key | Type | Default | Notes |
|---|---|---|---|
| `backend` | choice | "auto" | API sound is played through, independent of `window.backend`. `auto` is ALSA on desktop Linux, SDL3 elsewhere; one that cannot open a device degrades to `null`. One of `auto`, `alsa`, `sdl3`, `null`. |
| `volume` | float | 1 | Master output gain. 0 to 1. |
| `sample_rate` | int | 48000 | Requested output rate; the device may grant another. 8000 to 192000. |
| `channels` | int | 2 | Requested output channels. 1 to 8. |
| `buffer_frames` | int | 1024 | Frames per device callback, the latency dial. SDL3 may choose its own. 32 to 16384. |
| `max_voices` | int | 32 | Voices that can sound at once; further play() calls are dropped. 0 plays nothing. 0 to 1024. |

## `paths`

| Key | Type | Default | Notes |
|---|---|---|---|
| `shaders` | text | "./resources/shaders/" | Shader directory for the application's own loading. |
| `textures` | text | "./resources/textures/" | Texture directory. |
| `models` | text | "./resources/models/" | Model directory. |
| `audio` | text | "./resources/audio/" | Sound directory. |
| `logs` | text | "./logs/" | Where the log file goes when `logging.file` is on. |

## `logging`

| Key | Type | Default | Notes |
|---|---|---|---|
| `level` | choice | trace in debug builds, info otherwise | Lowest level logged. One of `off`, `fatal`, `error`, `warn`, `info`, `debug`, `verbose`, `trace`. |
| `file` | bool | false | Also write the log to `<paths.logs>/<application>.log`. |

## `debug`

| Key | Type | Default | Notes |
|---|---|---|---|
| `report_path` | text | "aura3d-benchmark.json" | Where the benchmark report is written. AURA_ENABLE_DEBUG_MODE builds only. |
| `label` | text | "" | Free-form tag copied into the report, such as a commit or a scene. |
| `warmup_frames` | int | 60 | Frames discarded before sampling begins. 0 to 1e+06. |
| `sample_capacity` | int | 20000 | Ring capacity for raw samples, about 96 bytes each. 1 to 1e+07. |
| `target_frames` | int | 0 | Frames to capture before the report is written; 0 runs until the application stops. 0 to 1e+09. |
| `frame_budget_ms` | float | 16.6667 | Frame time a frame must stay under to be on budget. 0.001 to 10000. |
| `max_over_budget_ratio` | float | 0.05 | Fraction of over-budget frames above which the verdict fails. 0 to 1. |
| `leak_slope_bytes_per_frame` | float | 1024 | Retained growth per frame above which a leak is called. 0 to 1e+12. |
| `auto_flush_interval_frames` | int | 0 | Write an interim report every N frames; 0 writes only at the end. 0 to 1e+09. |
| `exit_on_complete` | bool | false | End the process once `debug.target_frames` are captured. |

