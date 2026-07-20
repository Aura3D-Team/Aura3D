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

    // camera
    const wma::WindowDetails* wd = r->getWindowManager()->getWindowDetails();
    const float aspect = static_cast<float>(wd->width) / static_cast<float>(wd->height);

    Camera camera = Camera::perspective({.fovDeg = 45.0f,
                                         .aspect = aspect,
                                         .nearZ  = 0.1f,
                                         .farZ   = 100.0f});
    camera.setPosition({0.0f, 1.8f, 4.5f});
    camera.lookAt({0.0f, 0.0f, 0.0f});

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

    r->run([&]() {
        const float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - startTime).count();

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
