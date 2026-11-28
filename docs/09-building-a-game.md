# 9. Building a Game

A capstone walkthrough: a small first-person "collect the orbs" game, built
up in stages from everything covered in chapters 1–8. End state: walk around
an arena with WASD + mouse-look, walk into glowing orbs to collect them, see
your score and remaining count on screen, get a win message when they're all
gone.

Nothing here is a new API — it's the same handful of calls from the previous
chapters, composed.

## 0. Project layout

```
MyGame/
├── CMakeLists.txt
├── settings.json
└── main.cpp
```

```cmake
# CMakeLists.txt
find_package(Aura3D REQUIRED)

add_executable(MyGame main.cpp)
target_link_libraries(MyGame PRIVATE Aura3D::Aura3D)

# Stages settings.json next to the binary after every build, mirroring
# apps/Sandbox/CMakeLists.txt in the engine repo.
add_custom_command(TARGET MyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/settings.json
        $<TARGET_FILE_DIR:MyGame>/settings.json)
```

```json
{
    "window":   { "width": 1280, "height": 720, "title": "Orb Collector" },
    "present":  { "mode": "fifo" },
    "renderer": { "api": "vulkan", "msaa": 4 },
    "logging":  { "level": "info" }
}
```
(Every field not listed keeps its documented default — see
[02-project-configuration.md](02-project-configuration.md).)

## 1. Boot the engine and a moving camera

This is [04-camera.md](04-camera.md)'s free-look example; the WASD keys are
polled in the loop, as in [08-input.md](08-input.md):

```cpp
#include "aura/Aura3D.h"
#include "aura/UI/UI.hpp"
#include <vector>

using namespace aura3d;

int main()
{
    Engine engine("settings.json");
    IRenderer &r = engine.renderer();

    wma::IWindowManager &window = *r.getWindowManager();
    wma::KeyboardListener &keyboard = window.getKeyboardListener();
    wma::MouseListener &mouse = window.getMouseListener();

    //! The aspect ratio follows the window from the first frame on.
    Camera camera = Camera::perspective({.fovDeg = 70.0f, .nearZ = 0.1f, .farZ = 100.0f});
    camera.setPosition({0.0f, 1.0f, 6.0f});
    float camYaw = -90.0f, camPitch = 0.0f;
    camera.setRotation(camYaw, camPitch);

    mouse.setCursorEnabled(false);
    mouse.setMoveAction(wma::MouseAction{[&](const wma::WMAMousePosition &pos) {
        camYaw   += static_cast<float>(pos.deltaX) * 0.1f;
        camPitch -= static_cast<float>(pos.deltaY) * 0.1f;
        camera.setRotation(camYaw, camPitch);
    }});
```

## 2. The arena: floor + pillars, lit

From [05-meshes-materials-textures.md](05-meshes-materials-textures.md) and
[06-lighting.md](06-lighting.md) — built-in primitives, no asset files
needed, one directional light and an ambient one for the whole scene:

```cpp
    r.setLights(std::array{gfx::Light{.direction = {-0.4f, -1.0f, -0.3f}},
                            gfx::Light{.type = gfx::LightType::Ambient, .intensity = 0.25f}});

    const MeshHandle planeMesh = r.createMesh(MeshLoader::createPlane());
    const MeshHandle cubeMesh  = r.createMesh(MeshLoader::createCube());
    const MeshHandle orbMesh   = r.createMesh(MeshLoader::createSphere(2));

    // A flat colour is a tint alone; colours are linear (see 07-2d-rendering-and-text.md).
    const MaterialHandle floorMat  = r.createMaterial({.tint = {0.1f, 0.1f, 0.13f, 1.0f}});
    const MaterialHandle pillarMat = r.createMaterial({.albedo = r.createCheckerboardTexture(32)});
    const MaterialHandle orbMat    = r.createMaterial({.tint = {1.0f, 0.64f, 0.05f, 1.0f}});

    struct Pillar { glm::vec3 position; glm::vec3 scale; };
    const std::vector<Pillar> pillars = {
        {{-4.0f, 1.0f, -4.0f}, {1.0f, 2.0f, 1.0f}},
        {{ 4.0f, 1.0f, -4.0f}, {1.0f, 2.0f, 1.0f}},
        {{-4.0f, 1.0f,  4.0f}, {1.0f, 2.0f, 1.0f}},
        {{ 4.0f, 1.0f,  4.0f}, {1.0f, 2.0f, 1.0f}},
    };

    r.setClearColor(0.01f, 0.01f, 0.16f, 1.0f);
```

## 3. Collectibles and score

No physics engine is involved — "collecting" an orb is a distance check
against the camera position, run once per frame. This is a completely
ordinary game-logic pattern; nothing here is Aura3D-specific:

```cpp
    struct Orb { glm::vec3 position; bool collected = false; };
    std::vector<Orb> orbs = {
        {{2.0f, 0.6f, 0.0f}}, {{-2.0f, 0.6f, 2.0f}}, {{0.0f, 0.6f, -3.0f}},
        {{3.0f, 0.6f, 3.0f}}, {{-3.0f, 0.6f, -2.0f}},
    };
    int score = 0;
    const int totalOrbs = static_cast<int>(orbs.size());
    constexpr float kCollectRadius = 0.8f;
```

## 4. Score and instructions on screen

From [07-2d-rendering-and-text.md](07-2d-rendering-and-text.md): labels
built once and updated every frame, no font file required:

```cpp
    ui::UIView ui(r);
    auto &hud = ui.root().setContent<ui::Column>();
    hud.layout().padding = ui::Thickness::all(10.0f);
    auto &scoreLabel = hud.add<ui::Label>();
    hud.add<ui::Label>("WASD to move, mouse to look, Esc to quit");
    auto &winLabel = hud.add<ui::Label>("You win!");
    winLabel.setFontSize(30.0f);
    winLabel.setVisibility(ui::Visibility::Collapsed);
    auto &fpsLabel = hud.add<ui::Label>();
```

## 5. The loop

Ties every earlier step together: move the camera from input, check orb
pickups, draw the arena and any remaining orbs, draw the UI text.

```cpp
    constexpr float kMoveSpeed = 4.0f;

    r.run([&](const Frame &frame) {
        if (frame.resized)
            camera.setAspect(frame.aspect());

        // --- movement ---
        const glm::vec3 forward = camera.forward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 moveDir{0.0f};
        if (keyboard.isKeyDown(wma::KEY_W)) moveDir += forward;
        if (keyboard.isKeyDown(wma::KEY_S)) moveDir -= forward;
        if (keyboard.isKeyDown(wma::KEY_D)) moveDir += right;
        if (keyboard.isKeyDown(wma::KEY_A)) moveDir -= right;
        moveDir.y = 0.0f; // stay on the ground plane
        if (glm::length(moveDir) > 0.0f)
            camera.setPosition(camera.position() + glm::normalize(moveDir) * kMoveSpeed * frame.dt);

        // --- pickups ---
        for (Orb &orb : orbs) {
            if (orb.collected) continue;
            if (glm::distance(camera.position(), orb.position) < kCollectRadius) {
                orb.collected = true;
                ++score;
            }
        }

        // --- UI state ---
        scoreLabel.text = "Orbs: " + std::to_string(score) + " / " + std::to_string(totalOrbs);
        if (score == totalOrbs)
            winLabel.setVisibility(ui::Visibility::Visible);
        fpsLabel.text = "FPS: " + std::to_string(static_cast<int>(window.getWindowFlags()->fps));

        // --- draw ---
        if (!r.beginRenderPass())
            return;
        r.setCamera(camera);

        r.drawMesh(planeMesh, floorMat, glm::scale(glm::mat4(1.0f), {10.0f, 1.0f, 10.0f}));

        for (const Pillar &p : pillars)
            r.drawMesh(cubeMesh, pillarMat, glm::scale(glm::translate(glm::mat4(1.0f), p.position), p.scale));

        for (const Orb &orb : orbs) {
            if (orb.collected) continue;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), orb.position);
            model = glm::rotate(model, static_cast<float>(frame.time) * 2.0f, glm::vec3(0, 1, 0)); // slow spin
            model = glm::scale(model, glm::vec3(0.35f));
            r.drawMesh(orbMesh, orbMat, model);
        }

        ui.render(frame.dt);
        r.endRenderPass();
    });

    return 0;
}
```

## What to add next

Everything past this point is regular game-engine-agnostic C++ layered on
the same handful of calls above:

- **More object variety**: `MeshLoader::loadOBJ()` instead of the built-in
  primitives, textures via `r.createTextureFromFile(...)` instead of
  solid colors — see [05-meshes-materials-textures.md](05-meshes-materials-textures.md).
- **Menus / pause screen**: a second keyboard context
  (`keyboard.createContext()`) plus a `drawBatch` panel — see
  [08-input.md](08-input.md) and [07-2d-rendering-and-text.md](07-2d-rendering-and-text.md).
- **A different backend for comparison**: flip `renderer.api` in
  `settings.json` to `"opengl"` or `"software"` — no code changes.
- **Shipping to Android/WASM**: [10-platform-builds.md](10-platform-builds.md).

The engine intentionally stops here — there's no built-in physics,
scripting, or entity/component system (see the Roadmap in the top-level
README). Collision, AI, save systems, and the rest are yours to build in
plain C++ on top of what's above, the way the pickup-radius check in step 3
was.
