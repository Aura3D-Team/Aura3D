# Aura3D

A modular C++23 2D/3D rendering engine with three interchangeable graphics
backends — **Vulkan**, **OpenGL**, and a **CPU software rasterizer** — behind
one interface. Switch backends by editing `settings.json`; your game code
never changes.

Aura3D builds as a static library you link into your own executable. It owns
windowing, input, the render loop, and GPU resource management; you write
game logic on top of `aura3d::IRenderer` and the small set of helpers layered
over it (`Camera`, `Material`, `ResourceManager`, `TextOverlay`, ...).

---

## Why three backends

- **Vulkan** — the primary desktop/Android path. MSAA, push-constant
  per-object transforms, a dedicated 2D overlay pipeline, GPU font rendering.
- **OpenGL** — desktop GL and WebGL2 (via Emscripten) for WASM builds.
- **CPU (software)** — a real triangle rasterizer (perspective-correct
  interpolation, depth testing, Gouraud shading) with no GPU dependency at
  all. Useful for headless testing, low-end targets, or simply understanding
  what the GPU is doing.

All three implement the same `aura3d::IRenderer` interface, so a scene
written once renders identically (mesh, material, camera, lighting, 2D
overlay, text) on whichever backend `settings.json` selects.

---

## Quick start

```cpp
#include "aura/Core/Engine.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/Renderer/Material.h"

using namespace aura3d;

int main()
{
    Engine engine("settings.json");
    IRenderer* r = engine.getRenderer();

    const MeshHandle cube = r->createMesh(MeshLoader::createCube());
    Material mat;
    mat.albedo = r->createCheckerboardTexture();
    const MaterialHandle matH = r->createMaterial(mat);

    r->setClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    r->setTransform({}); // model/view/proj — see docs/04-camera.md

    r->run([&]() {
        r->beginRenderPass();
        r->bindMaterial(matH);
        r->drawMesh(cube);
        r->endRenderPass();
    });
}
```

That's a complete, runnable program. For a camera, lighting, input, and a
full scene, start with **[docs/01-getting-started.md](docs/01-getting-started.md)**.

---

## Documentation

The [`docs/`](docs/) folder is a tutorial series that walks through every
feature Aura3D has, in the order you'd actually reach for them building a
game:

| # | Doc | Covers |
|---|-----|--------|
| 1 | [Getting Started](docs/01-getting-started.md) | Build, run, first window |
| 2 | [Project Configuration](docs/02-project-configuration.md) | Full `settings.json` reference |
| 3 | [Engine & Renderer](docs/03-engine-and-renderer.md) | `Engine`, `IRenderer`, backend switching, the run loop |
| 4 | [Camera](docs/04-camera.md) | Perspective/ortho projections, target vs. free-look |
| 5 | [Meshes, Materials & Textures](docs/05-meshes-materials-textures.md) | Loading OBJ/images, `ResourceManager`, `Material` |
| 6 | [Lighting](docs/06-lighting.md) | The directional light model |
| 7 | [2D Rendering & Text](docs/07-2d-rendering-and-text.md) | `drawBatch2D`, the overlay pipeline, `TextOverlay` |
| 8 | [Input](docs/08-input.md) | Keyboard/mouse contexts and bindings |
| 9 | [Building a Game](docs/09-building-a-game.md) | Capstone: a small playable scene from scratch |
| 10 | [Platform Builds](docs/10-platform-builds.md) | Linux, Windows, Android, WebAssembly |
| 11 | [Using a Release](docs/11-using-releases.md) | Consuming prebuilt release archives; building the dev container |
| 12 | [Audio](docs/12-audio.md) | `AudioEngine`: clips, one-shots, looping music, 3D positional sound |
| 13 | [Debug & Benchmark Mode](docs/13-debug-benchmark-mode.md) | CPU/GPU allocation tracking, per-phase frame timing, the JSON metrics report |
| 14 | [AuraUI — The Widget Toolkit](docs/14-auraui-toolkit.md) | Widget tree, flex/grid layout, shaped text, signals, animation, accessibility |

---

## Building

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

Or without presets:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### CMake options

| Option | Default | Description |
|---|---|---|
| `AURA_ENABLE_VULKAN` | `ON` | Compile the Vulkan backend |
| `AURA_ENABLE_OPENGL` | `ON` | Compile the OpenGL backend |
| `AURA_ENABLE_CPU` | `ON` | Compile the software (CPU) backend |
| `AURA_ENABLE_UI` | `ON` | Compile AuraUI (`aura3d::ui`), the widget toolkit |
| `AURA_BUILD_SANDBOX` | `ON` | Build the Sandbox demo app |
| `AURA_BUILD_ORGLOGO` | `ON` | Build the OrgLogo demo app |
| `AURA_BUILD_UIDEMO` | `ON` | Build the AuraUI widget-toolkit demo app |
| `AURA_BUILD_TESTS` | `OFF` | Build the automated test suite |
| `AURA_ENABLE_LTO` | `ON` | Interprocedural optimization |
| `AURA_NATIVE_OPTIMIZE` | `ON` | `-march=native` on desktop builds |
| `AURA_WASM_ASYNCIFY` | `OFF` | Emscripten Asyncify (only if the app ever blocks synchronously) |
| `AURA_PROFILE_FRAME` | `OFF` | Per-phase frame timing, logged periodically |
| `AURA_ENABLE_DEBUG_MODE` | `OFF` | CPU/GPU allocation tracking + JSON benchmark report ([docs](docs/15-debug-benchmark-mode.md)); implies `AURA_PROFILE_FRAME` |

Build with only the CPU renderer (no GPU dependencies at all):

```bash
cmake .. -DAURA_ENABLE_VULKAN=OFF -DAURA_ENABLE_OPENGL=OFF
```

Windows (`cmake --preset windows-release`, needs vcpkg — see
[docs/10-platform-builds.md#windows](docs/10-platform-builds.md#windows)),
Android, and WebAssembly have their own presets and prerequisites — see
[docs/10-platform-builds.md](docs/10-platform-builds.md).

### Using Aura3D as a library

Building from source:

```bash
cmake --install build --prefix /usr/local
```

```cmake
find_package(Aura3D REQUIRED)
add_executable(MyGame main.cpp)
target_link_libraries(MyGame PRIVATE Aura3D::Aura3D)
```

Or skip building Aura3D entirely and use a prebuilt release archive instead
— see [docs/11-using-releases.md](docs/11-using-releases.md) for Linux,
Windows, Android, and WASM specifics.

---

## Repository layout

```
Aura3D/
├── apps/
│   ├── Sandbox/       # Interactive 3D demo: WASD+mouse camera, lit scene, FPS overlay, F1-toggled AuraUI sidebar — spawn/remove textured 3D objects, tweak a live-animated sprite, edit the theme live
│   ├── OrgLogo/        # Minimal 2D demo: one textured quad, no camera movement
│   └── AuraUIDemo/     # AuraUI widget-toolkit showcase: a settings page of cards, a form Grid, live signals
├── docs/                # Tutorial series (see table above)
├── engine/
│   ├── include/aura/    # Public API — this is what your game includes
│   └── src/             # Implementation, including the private per-backend managers
├── resources/
│   └── shaders/{opengl,vulkan}/
├── vendor/               # stb (images/fonts), tinyobj, glad
├── android/              # Gradle project wrapping the CMake build
├── cmake/                # Platform.cmake, Dependencies.cmake, Install.cmake, Assets.cmake
├── scripts/               # build_android.sh, build_wasm.sh
├── CMakeLists.txt
└── CMakePresets.json
```

**Design principles:**
- **Public vs. private**: your game includes `engine/include/aura/` only.
  Per-backend internals (`VkDeviceManager`, `GlTextureManager`, ...) are not
  installed and stay out of your include path.
- **Backend isolation**: each renderer lives in its own directory tree
  (`engine/{include,src}/aura/Renderer/{Vulkan,OpenGL,Software}/`). Working on
  one backend never touches another's files.
- **Conditional compilation**: backends are gated by `AURA_HAS_VULKAN`,
  `AURA_HAS_OPENGL`, `AURA_HAS_CPU` defines that CMake sets from the
  `AURA_ENABLE_*` options above — an app can link Aura3D built with only the
  backends it actually needs. `AURA_HAS_UI` gates the UI module the same way.
- **Backend-agnostic subsystems**: anything built on the public `IRenderer`
  API alone — `TextOverlay`, `aura3d::ui` — lives outside the backend trees
  and works on every backend without a line of per-backend code.

---

## License

See [LICENSE](LICENSE).
