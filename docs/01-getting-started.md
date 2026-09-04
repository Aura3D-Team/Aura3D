# 1. Getting Started

This walks through building Aura3D, running the bundled demos, and writing
the smallest possible Aura3D program.

## Prerequisites

- CMake 4.3.3+, Ninja
- A C++23 compiler (GCC 15 / Clang 22+)
- Vulkan SDK (for the Vulkan backend), an OpenGL driver (for the OpenGL
  backend) — both are optional; see below
- `libink` and `libwma` built and installed (Aura3D depends on both; see
  their own READMEs)

If you're building inside the project's `vulkan-dev` container, all of this
is already installed — build and run it from
[Aura3D-Team/qt_dev](https://github.com/Aura3D-Team/qt_dev) (`image_tag="lts"`);
see [11-using-releases.md#development-environment](11-using-releases.md#development-environment)
for the exact commands. Otherwise see
[10-platform-builds.md](10-platform-builds.md) for exact per-platform
dependency lists.

Just want to use Aura3D as a dependency rather than build it? See
[11-using-releases.md](11-using-releases.md) instead — it covers consuming
the prebuilt release archives directly.

## Build

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

This produces, under `build/linux/release/`:
- `libaura3d_<platform>.a` — the engine
- `apps/Sandbox/Sandbox` — an interactive 3D demo (WASD + mouse-look camera, a
  lit scene with three objects, live FPS counter, spatial audio) with an
  F1-toggled AuraUI sidebar: spawn/remove textured 3D objects, tweak a
  procedurally animated 2D sprite, and edit the theme live. See
  [14-auraui-toolkit.md](14-auraui-toolkit.md)
- `apps/OrgLogo/OrgLogo` — a minimal 2D demo (one textured quad, no camera
  movement) — the simplest possible complete Aura3D program

Run any of them from its own directory, since each loads `settings.json` and
`resources/` relative to the working directory:

```bash
cd build/linux/release/apps/Sandbox && ./Sandbox
```

Press **Escape** to quit — every Aura3D app gets this for free (see
[03-engine-and-renderer.md](03-engine-and-renderer.md#the-run-loop)).

## Picking a backend without touching code

Open `apps/Sandbox/settings.json` and change:

```json
"renderer": { "backend": "vulkan" }
```

to `"opengl"` or `"cpu"`, then rerun — same scene, same code, different
graphics API. This is the core of how Aura3D is meant to be used: your game
logic is written once against `IRenderer` and never knows which backend is
actually active.

## The smallest Aura3D program

This is `apps/OrgLogo/main.cpp` reduced to its essentials — no camera
movement, one quad, one texture:

```cpp
#include "aura/Core/Engine.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Core/Camera/Camera.h"
#include "aura/Renderer/Material.h"

using namespace aura3d;

int main()
{
    Engine engine("settings.json");
    IRenderer* r = engine.getRenderer();

    // A flat-lit scene: ambient=1, intensity=0 means "show colors as-is".
    gfx::LightUBO light;
    light.intensity = 0.0f;
    light.ambient   = 1.0f;
    r->setLight(light);

    const TextureHandle tex = r->createCheckerboardTexture(256);
    Material mat;
    mat.albedo = tex;
    const MaterialHandle matH = r->createMaterial(mat);

    // A unit quad, doubled to fill the [-1,1] orthographic view box.
    gfx::Mesh3D quad;
    quad.vertices = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}, glm::vec4(1.0f), {0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}, glm::vec4(1.0f), {0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}, glm::vec4(1.0f), {0.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f}, glm::vec4(1.0f), {0.0f, 0.0f, 1.0f}},
    };
    quad.indices = {0, 1, 2, 2, 3, 0};
    const MeshHandle quadMesh = r->createMesh(quad);

    Camera cam = Camera::ortho({.left = -1, .right = 1, .bottom = -1, .top = 1});
    cam.setPosition({0.0f, 0.0f, 2.0f});
    cam.lookAt({0.0f, 0.0f, 0.0f});
    const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 1.0f));

    r->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    r->run([&]() {
        r->beginRenderPass();
        r->setTransform(cam.buildUBO(model));
        r->bindMaterial(matH);
        r->drawMesh(quadMesh);
        r->endRenderPass();
    });
}
```

Every concept here — `Engine`, `IRenderer`, `Camera`, `Material`,
`gfx::Mesh3D`, the render-pass/draw/present cycle — gets its own chapter next.
Continue to **[02-project-configuration.md](02-project-configuration.md)**.
