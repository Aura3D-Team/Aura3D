# Aura3D

High-performance configurable 2D/3D rendering engine. Pure code, no UI — the root of programming.

---

### What Is Aura3D

Aura3D is a **modular C++ rendering engine** that supports multiple graphics backends through a single unified interface. Switch between **Vulkan**, **OpenGL**, or **CPU software rendering** — and between **2D** and **3D** mode — by editing a single JSON config file. No code changes required.

The engine builds as a **static library** that you link into your game or application. It handles all rendering infrastructure; you write the game logic.

---

### Configuration

All runtime behavior is controlled by `settings.json`:

```json
{
    "renderer": {
        "backend": "vulkan",
        "mode": "2d"
    }
}
```

| Field               | Values                          | Description                           |
|---------------------|---------------------------------|---------------------------------------|
| `renderer.backend`  | `"vulkan"`, `"opengl"`, `"cpu"` | Which rendering backend to use        |
| `renderer.mode`     | `"2d"`, `"3d"`                  | Orthographic (2D) or perspective (3D) |
| `window.width`      | integer                         | Window width in pixels                |
| `window.height`     | integer                         | Window height in pixels               |
| `window.vsync`      | `true` / `false`                | Enable vertical sync                  |
| `window.fullscreen` | `true` / `false`                | Start in fullscreen mode              |
| `window.fps_limit`  | integer                         | Target FPS (ignored when vsync is on) |

---

### Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

#### CMake Options

| Option               | Default | Description                          |
|----------------------|---------|--------------------------------------|
| `AURA_ENABLE_VULKAN` | `ON`    | Compile with Vulkan backend support  |
| `AURA_ENABLE_OPENGL` | `ON`    | Compile with OpenGL backend support  |
| `AURA_ENABLE_CPU`    | `ON`    | Compile with CPU renderer support    |
| `AURA_BUILD_SANDBOX` | `ON`    | Build the Sandbox test application   |

Build with only the CPU renderer (no GPU dependencies):

```bash
cmake .. -DAURA_ENABLE_VULKAN=OFF -DAURA_ENABLE_OPENGL=OFF
```

---

### Using Aura3D as a Library

After installing Aura3D, external projects can consume it with `find_package`:

```bash
cmake --install build --prefix /usr/local
```

In your game's `CMakeLists.txt`:

```cmake
find_package(Aura3D REQUIRED)
add_executable(MyGame main.cpp)
target_link_libraries(MyGame PRIVATE Aura3D::Aura3D)
```

Minimal game code:

```cpp
#include "aura/Core/Engine.h"
#include "aura/Renderer/IRenderer.h"

int main() {
    Engine engine("settings.json");
    aura3d::IRenderer* renderer = engine.getRenderer();

    // renderer->is2D() / renderer->is3D() — query mode
    // renderer->getBackendType() — query active backend
    // renderer->getWindowManager()->process([&]() { ... }) — render loop

    return 0;
}
```

---

### Architecture

```
Aura3D/
├── apps/                     # Executables (games, tests)
│   └── Sandbox/              # Example application
│       ├── main.cpp
│       └── settings.json     # Runtime configuration
│
├── engine/                   # The Engine Library
│   ├── include/aura/         # PUBLIC HEADERS
│   │   ├── aura.h            # Main version/name defines
│   │   ├── Core/             # Engine, AuraCore, Settings, Font, Exception
│   │   ├── Renderer/
│   │   │   ├── IRenderer.h   # Abstract interface + RendererChoice/RendererMode enums
│   │   │   ├── Software/     # CPU renderer + framebuffer manager
│   │   │   ├── OpenGL/       # OpenGL renderer + GL shader/buffer managers
│   │   │   └── Vulkan/       # Vulkan renderer + full VkAura subsystem
│   │   └── Utils/            # Colors, math utilities, aligned allocator
│   │
│   └── src/                  # PRIVATE SOURCE (hidden from consumers)
│       ├── Core/             # Config loader, settings implementation
│       ├── Renderer/
│       │   ├── Software/     # CPU rasterizer implementation
│       │   ├── OpenGL/       # GL context + shader management
│       │   └── Vulkan/       # Instance, device, swapchain, pipeline, memory, etc.
│       └── Utils/            # Utility implementations
│
├── resources/                # Runtime assets
│   └── shaders/
│       ├── opengl/           # GLSL shaders
│       └── vulkan/           # GLSL shaders (compiled to SPIR-V by CMake)
│
├── vendor/                   # Third-party (glad, microui)
├── cmake/                    # CMake package config templates
├── config.json               # Engine default config template
└── CMakeLists.txt
```

**Key design principles:**
- **Public vs. Private**: Your game includes `engine/include` only. Internal headers (VkDeviceManager, etc.) stay hidden.
- **Backend isolation**: Each renderer lives in its own directory. Working on OpenGL never touches Vulkan files.
- **Conditional compilation**: Backends are guarded by `AURA_HAS_VULKAN`, `AURA_HAS_OPENGL`, `AURA_HAS_CPU` defines, set automatically by CMake options.

---

### Renderer Interface

All renderers implement `aura3d::IRenderer`:

```cpp
class IRenderer {
public:
    virtual void initialize() = 0;
    virtual void handleWindowChanges() = 0;
    virtual void cleanup() = 0;
    virtual wma::IWindowManager* getWindowManager() = 0;
    virtual RendererChoice getBackendType() const = 0;

    RendererMode getMode() const;
    bool is2D() const;
    bool is3D() const;
};
```

The `Engine` class reads `settings.json`, creates the correct renderer, and exposes it. Your application code queries `engine.getBackend()` and `engine.getMode()` to adapt behavior.

---

### Roadmap

- Unified math library (vectors, matrices, transforms)
- Entity/component system decoupled from the renderer
- Physics and scripting layers
- Debug visualization tools for CPU/GPU backend comparison
- Resource compiler for converting assets into engine-ready formats
- 3D shader variants alongside existing 2D shaders

---

### License

See [LICENSE](LICENSE).
