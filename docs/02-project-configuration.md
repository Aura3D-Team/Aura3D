# 2. Project Configuration

Every Aura3D app is driven by one JSON file, loaded by `Engine`'s
constructor:

```cpp
Engine engine("settings.json");
```

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
        "msaa_samples": 4
    },
    "paths": {
        "shaders": "./resources/shaders/",
        "textures": "./resources/textures/",
        "models": "./resources/models/",
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

## `graphics` — live (Vulkan only)

| Key | Type | Default | Notes |
|---|---|---|---|
| `gpu_preference` | string | `"discrete"` | `"discrete"`, `"integrated"`, or `"any"`. Steers Vulkan physical-device selection: `"discrete"`/`"integrated"` add a scoring bonus to that device type, `"any"` drops the type bonus entirely and lets capability (API version, image limits, geometry-shader support) decide alone. |
| `msaa_samples` | int | 1 | Requested MSAA sample count (`1`, `2`, `4`, `8`, ...). Clamped down to the largest count the selected device actually supports for both color and depth attachments. `1` disables MSAA. Only the Vulkan backend implements this — OpenGL and the CPU backend always render at 1x regardless of this value. |

## `paths` — live

| Key | Type | Default | Notes |
|---|---|---|---|
| `shaders` | string | `./resources/shaders/` | Read by `AuraSettings::getShadersPath()`; not currently consumed by the built-in pipelines (they embed their SPIR-V/GLSL — see [10-platform-builds.md](10-platform-builds.md)), but available for your own shader loading. |
| `textures` | string | `./resources/textures/` | Convention used by `ResourceManager::loadTexture` callers — see `apps/Sandbox/main.cpp`'s `settings->getTexturesPath() + "crate.png"`. |
| `models` | string | `./resources/models/` | Same convention for `ResourceManager::loadMesh`. |
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

## `logging` — live

| Key | Type | Default | Notes |
|---|---|---|---|
| `level` | string | `"trace"` in debug builds, `"info"` in release | Case-insensitive: `off`, `fatal`, `error`, `warn`, `info`, `debug`, `verbose`, `trace`. |
| `write_to_file` | bool | `false` | When `true`, also writes to `<paths.logs>/<application name>.log` (in addition to the console). The directory is created automatically. |

## Reading settings from your own code

```cpp
const aura3d::AuraSettings* settings = engine.getSettings();

settings->getWindowWidth();      // int
settings->getRendererBackend();  // std::string
settings->getMsaaSamples();      // int
settings->getSettings();         // raw ink::EnhancedJson*, for bespoke schemas
                                  // (this is how memory.vma.* reads its own keys)
```

For anything not covered by a typed accessor, `getSettings()->getPath<T>("/your/key", defaultValue)`
reaches the raw document directly — the same mechanism every accessor above
is built on.

## Reloading at runtime

```cpp
aura3d::AuraSettings::get()->reload("settings.json");
```

Replaces the in-memory document. Values already consumed by live subsystems
(window size, the active renderer, ...) are **not** re-applied automatically
— this is for picking up hand-edited config before creating something new,
not a live-reload system.

Next: **[03-engine-and-renderer.md](03-engine-and-renderer.md)**.
