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
    "window":   { "width": 1280, "height": 720, "title": "Orb Collector", "vsync": true },
    "renderer": { "backend": "vulkan" },
    "graphics": { "msaa_samples": 4 },
    "logging":  { "level": "info" }
}
```
(Every field not listed keeps its documented default — see
[02-project-configuration.md](02-project-configuration.md).)

## 1. Boot the engine and a moving camera

This is [04-camera.md](04-camera.md)'s free-look example plus
[08-input.md](08-input.md)'s WASD helper, unchanged:

```cpp
#include "aura/Core/Engine.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Core/Camera/Camera.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/Core/TextOverlay/TextOverlay.h"
#include "aura/Utils/ColorsDefinitions.h"
#include <chrono>
#include <vector>

using namespace aura3d;

int main()
{
    Engine engine("settings.json");
    IRenderer* r = engine.getRenderer();

    auto* windowManager = r->getWindowManager();
    wma::KeyboardListener& keyboard = windowManager->getKeyboardListener();
    wma::MouseListener& mouse = windowManager->getMouseListener();

    const wma::WindowDetails* wd = windowManager->getWindowDetails();
    const float aspect = static_cast<float>(wd->width) / static_cast<float>(wd->height);

    Camera camera = Camera::perspective({.fovDeg = 70.0f, .aspect = aspect, .nearZ = 0.1f, .farZ = 100.0f});
    camera.setPosition({0.0f, 1.0f, 6.0f});
    float camYaw = -90.0f, camPitch = 0.0f;
    camera.setRotation(camYaw, camPitch);

    mouse.setCursorEnabled(false);
    mouse.setMoveAction(wma::MouseAction{[&](const wma::WMAMousePosition& pos) {
        camYaw   += static_cast<float>(pos.deltaX) * 0.1f;
        camPitch -= static_cast<float>(pos.deltaY) * 0.1f;
        camera.setRotation(camYaw, camPitch);
    }});

    bool moveForward = false, moveBack = false, moveLeft = false, moveRight = false;
    auto bindHeld = [&keyboard](wma::Key key, bool& flag) {
        keyboard.addKeyAction(key, wma::KeyAction{[&flag]() { flag = true; }, [&flag]() { flag = false; }});
    };
    bindHeld(wma::KEY_W, moveForward);
    bindHeld(wma::KEY_S, moveBack);
    bindHeld(wma::KEY_A, moveLeft);
    bindHeld(wma::KEY_D, moveRight);
```

## 2. The arena: floor + pillars, lit

From [05-meshes-materials-textures.md](05-meshes-materials-textures.md) and
[06-lighting.md](06-lighting.md) — built-in primitives, no asset files
needed, one directional light for the whole scene:

```cpp
    gfx::LightUBO light;
    light.direction = {-0.4f, -1.0f, -0.3f};
    light.intensity = 1.0f;
    light.ambient   = 0.25f;
    r->setLight(light);

    const MeshHandle planeMesh = r->createMesh(MeshLoader::createPlane());
    const MeshHandle cubeMesh  = r->createMesh(MeshLoader::createCube());
    const MeshHandle orbMesh   = r->createMesh(MeshLoader::createSphere(2));

    Material floorMat; floorMat.albedo = r->createSolidColorTexture(90, 90, 100);
    Material pillarMat; pillarMat.albedo = r->createCheckerboardTexture(32);
    Material orbMat; orbMat.albedo = r->createSolidColorTexture(255, 210, 60);

    const MaterialHandle floorMatH   = r->createMaterial(floorMat);
    const MaterialHandle pillarMatH  = r->createMaterial(pillarMat);
    const MaterialHandle orbMatH     = r->createMaterial(orbMat);

    struct Pillar { glm::vec3 position; glm::vec3 scale; };
    const std::vector<Pillar> pillars = {
        {{-4.0f, 1.0f, -4.0f}, {1.0f, 2.0f, 1.0f}},
        {{ 4.0f, 1.0f, -4.0f}, {1.0f, 2.0f, 1.0f}},
        {{-4.0f, 1.0f,  4.0f}, {1.0f, 2.0f, 1.0f}},
        {{ 4.0f, 1.0f,  4.0f}, {1.0f, 2.0f, 1.0f}},
    };

    const colors::RGBf clear = colors::MIDNIGHT_BLUE_F;
    r->setClearColor(clear.r, clear.g, clear.b, 1.0f);
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

From [07-2d-rendering-and-text.md](07-2d-rendering-and-text.md) — built
once, drawn every frame, no font file required:

```cpp
    TextOverlayDesc overlayDesc;
    overlayDesc.pixelHeight = 20.0f;
    TextOverlay overlay(r, overlayDesc);
```

## 5. The loop

Ties every earlier step together: move the camera from input, check orb
pickups, draw the arena and any remaining orbs, draw the UI text.

```cpp
    r->setTransform(camera.buildUBO());
    const auto startTime = std::chrono::steady_clock::now();
    constexpr float kMoveSpeed = 4.0f;

    r->run([&]() {
        const float dt = static_cast<float>(windowManager->getWindowFlags()->deltaTime) / 1000.0f;
        const float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - startTime).count();

        // --- movement ---
        const glm::vec3 forward = camera.forward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 moveDir{0.0f};
        if (moveForward) moveDir += forward;
        if (moveBack)    moveDir -= forward;
        if (moveRight)   moveDir += right;
        if (moveLeft)    moveDir -= right;
        moveDir.y = 0.0f; // stay on the ground plane
        if (glm::length(moveDir) > 0.0f)
            camera.setPosition(camera.position() + glm::normalize(moveDir) * kMoveSpeed * dt);

        // --- pickups ---
        for (Orb& orb : orbs) {
            if (orb.collected) continue;
            if (glm::distance(camera.position(), orb.position) < kCollectRadius) {
                orb.collected = true;
                ++score;
            }
        }

        // --- draw ---
        r->beginRenderPass();

        glm::mat4 floorModel = glm::scale(glm::mat4(1.0f), {10.0f, 1.0f, 10.0f});
        r->setTransform(camera.buildUBO(floorModel));
        r->bindMaterial(floorMatH);
        r->drawMesh(planeMesh);

        for (const Pillar& p : pillars) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), p.position);
            model = glm::scale(model, p.scale);
            r->setTransform(camera.buildUBO(model));
            r->bindMaterial(pillarMatH);
            r->drawMesh(cubeMesh);
        }

        for (const Orb& orb : orbs) {
            if (orb.collected) continue;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), orb.position);
            model = glm::rotate(model, elapsed * 2.0f, glm::vec3(0, 1, 0)); // slow spin
            model = glm::scale(model, glm::vec3(0.35f));
            r->setTransform(camera.buildUBO(model));
            r->bindMaterial(orbMatH);
            r->drawMesh(orbMesh);
        }

        // --- UI ---
        overlay.drawText("Orbs: " + std::to_string(score) + " / " + std::to_string(totalOrbs), 10.0f, 10.0f);
        overlay.drawText("WASD to move, mouse to look, Esc to quit", 10.0f, 34.0f, glm::vec4(0.8f, 0.8f, 0.8f, 1));
        if (score == totalOrbs)
            overlay.drawText("You win!", 10.0f, 58.0f, glm::vec4(0.4f, 1.0f, 0.4f, 1), 1.5f);
        overlay.drawFPS(10.0f, static_cast<float>(wd->height) - 24.0f);

        r->endRenderPass();
    });

    return 0;
}
```

## What to add next

Everything past this point is regular game-engine-agnostic C++ layered on
the same handful of calls above:

- **More object variety**: `MeshLoader::loadOBJ()` instead of the built-in
  primitives, textures via `engine.resources()->loadTexture(...)` instead of
  solid colors — see [05-meshes-materials-textures.md](05-meshes-materials-textures.md).
- **Menus / pause screen**: a second keyboard context
  (`keyboard.createContext()`) plus a `drawBatch2D` panel — see
  [08-input.md](08-input.md) and [07-2d-rendering-and-text.md](07-2d-rendering-and-text.md).
- **A different backend for comparison**: flip `renderer.backend` in
  `settings.json` to `"opengl"` or `"cpu"` — no code changes.
- **Shipping to Android/WASM**: [10-platform-builds.md](10-platform-builds.md).

The engine intentionally stops here — there's no built-in physics,
scripting, or entity/component system (see the Roadmap in the top-level
README). Collision, AI, save systems, and the rest are yours to build in
plain C++ on top of what's above, the way the pickup-radius check in step 3
was.
