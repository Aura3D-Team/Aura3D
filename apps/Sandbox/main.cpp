#include <chrono>
#include <vector>

#include "aura/Core/Engine.h"
#include "aura/Core/AuraCore.h"
#include "aura/Core/Camera/Camera.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/Core/ResourceManager/ResourceManager.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Material.h"
#include "aura/Utils/ColorsDefinitions.h"

using namespace aura3d;

namespace {

/// One drawable instance: geometry, surface, and where it sits in the world.
struct SceneObject {
    MeshHandle mesh = INVALID_HANDLE;
    MaterialHandle material = INVALID_HANDLE;
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    float spinSpeed = 0.0f; // radians/second about +Y
};

} // namespace

int main()
{
    Engine engine("settings.json");

    IRenderer* r = engine.getRenderer();
    ResourceManager* resources = engine.resources();
    const AuraSettings* settings = engine.getSettings();

    auto* windowManager = r->getWindowManager();
    wma::KeyboardListener& keyboard = windowManager->getKeyboardListener();
    wma::MouseListener& mouse = windowManager->getMouseListener();
    wma::InputContextId gameplay = keyboard.createContext();
    keyboard.setActiveContext(gameplay);

    // camera
    const wma::WindowDetails* wd = r->getWindowManager()->getWindowDetails();
    const float aspect = static_cast<float>(wd->width) / static_cast<float>(wd->height);

    Camera camera = Camera::perspective({.fovDeg = 60.0f,
                                         .aspect = aspect,
                                         .nearZ  = 0.1f,
                                         .farZ   = 100.0f});

    camera.setPosition({0.0f, 0.8f, 4.5f});

    float camYaw = -90.0f;
    float camPitch = -8.0f;
    camera.setRotation(camYaw, camPitch);

    constexpr float kMouseSensitivity = 0.1f;
    mouse.setCursorEnabled(false);
    mouse.setMoveAction(wma::MouseAction{[&](const wma::WMAMousePosition& pos) {
        camYaw += static_cast<float>(pos.deltaX) * kMouseSensitivity;
        camPitch += static_cast<float>(pos.deltaY) * kMouseSensitivity;
        camera.setRotation(camYaw, camPitch);
    }});

    bool moveForward = false, moveBack = false, moveLeft = false, moveRight = false;
    bool moveUp = false, moveDown = false;

    auto bindHeld = [&keyboard](wma::Key key, bool& flag) {
        keyboard.addKeyAction(key, wma::KeyAction{
            [&flag]() { flag = true; },
            [&flag]() { flag = false; }
        });
    };
    bindHeld(wma::KEY_W, moveForward);
    bindHeld(wma::KEY_S, moveBack);
    bindHeld(wma::KEY_A, moveLeft);
    bindHeld(wma::KEY_D, moveRight);
    bindHeld(wma::KEY_SPACE, moveUp);
    bindHeld(wma::KEY_LEFT_SHIFT, moveDown);

    //- light
    gfx::LightUBO light;
    light.direction = {-0.4f, -1.0f, -0.5f}; // the way the light travels
    light.color = {1.0f, 0.98f, 0.92f, 1.0f};
    light.intensity = 1.0f;
    light.ambient = 0.18f;
    r->setLight(light);

    // meshes
    // Embedded primitives need no files on disk.
    const MeshHandle cubeMesh   = r->createMesh(MeshLoader::createCube());
    const MeshHandle sphereMesh = r->createMesh(MeshLoader::createSphere(3));
    const MeshHandle planeMesh  = r->createMesh(MeshLoader::createPlane());

    // Cached and de-duplicated by path. A missing file is not fatal: the loader
    // substitutes the magenta/black checkerboard and logs a warning, so run this
    // without any assets present and the scene still renders.
    const TextureHandle crateTex = resources->loadTexture(settings->getTexturesPath() + "crate.png");

    const TextureHandle checkerTex = r->createCheckerboardTexture(64);
    const TextureHandle whiteTex   = r->createSolidColorTexture(255, 255, 255);

    // materials
    Material crateMaterial;
    crateMaterial.albedo = crateTex;
    crateMaterial.roughness = 0.8f;

    Material checkerMaterial;
    checkerMaterial.albedo = checkerTex;
    checkerMaterial.roughness = 0.4f;

    Material floorMaterial;
    floorMaterial.albedo = whiteTex;
    floorMaterial.tint = {0.6f, 0.65f, 0.7f, 1.0f};

    const std::vector<SceneObject> scene = {
        // Floor.
        {planeMesh, r->createMaterial(floorMaterial), {0.0f, -0.75f, 0.0f}, {8.0f, 1.0f, 8.0f}, 0.0f},
        // Two spinning cubes flanking a sphere.
        {cubeMesh,   r->createMaterial(crateMaterial),   {-1.5f, 0.0f, 0.0f}, glm::vec3(1.0f), 0.9f},
        {sphereMesh, r->createMaterial(checkerMaterial), { 0.0f, 0.0f, 0.0f}, glm::vec3(1.2f), 0.0f},
        {cubeMesh,   r->createMaterial(crateMaterial),   { 1.5f, 0.0f, 0.0f}, glm::vec3(1.0f), -0.9f},
    };

    const colors::RGBf clear = colors::MIDNIGHT_BLUE_F;
    r->setClearColor(clear.r, clear.g, clear.b, 1.0f);

    // Seed view/projection before the first beginRenderPass, which is what
    // uploads them for the frame.
    r->setTransform(camera.buildUBO());

    const auto startTime = std::chrono::steady_clock::now();

    constexpr float kMoveSpeed = 3.0f; // world units per second

    r->run([&]() {
        const float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - startTime).count();

        const float dt = static_cast<float>(windowManager->getWindowFlags()->deltaTime) / 1000.0f;

        const glm::vec3 forward = camera.forward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

        glm::vec3 moveDir{0.0f};
        if (moveForward)
            moveDir += forward;
        if (moveBack)
            moveDir -= forward;
        if (moveRight)
            moveDir += right;
        if (moveLeft)
            moveDir -= right;
        if (moveUp)
            moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
        if (moveDown)
            moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

        if (glm::length(moveDir) > 0.0f) {
            camera.setPosition(camera.position() + glm::normalize(moveDir) * kMoveSpeed * dt);
        }

        r->beginRenderPass();

        for (const SceneObject& object : scene) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), object.position);
            if (object.spinSpeed != 0.0f) {
                model = glm::rotate(model, elapsed * object.spinSpeed, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            model = glm::scale(model, object.scale);

            // One transform per object: the per-draw path the renderer feeds to
            // Vulkan push constants.
            r->setTransform(camera.buildUBO(model));
            r->bindMaterial(object.material);
            r->drawMesh(object.mesh);
        }

        r->endRenderPass();
    });

    return 0;
}
