// UIPlayground — aura3d::ui driving a live scene: textured 3D objects spawned
// and removed at runtime, and a procedurally animated 2D sprite, all edited
// through UI panels while the scene keeps rendering underneath them.
//
// This is the companion app to docs/12-immediate-mode-ui.md and
// docs/13-auraui-internals.md: everything the docs describe in isolation runs
// together here. Three things it demonstrates that a single code snippet
// can't:
//
//   1. UI widgets reading and writing state that also drives real rendering
//      calls (r->drawMeshes(), r->setLight(), r->drawBatch2D()) in the same
//      frame, not a toy variable.
//   2. A texture that changes every frame rather than the FontAtlas/AuraUI
//      case of one rasterized once and reused -- see regenerateSprite()'s
//      comment for why that upload is written differently.
//   3. A widget-driven value (the sprite's hue) that never touches the atlas
//      or vertex data directly, only the vertex *colour* handed to
//      drawBatch2D -- the same "coverage texture, colour at draw time" split
//      FontAtlas/TextOverlay/AuraUI all use internally (see docs/13).
//
// Camera and input are copied from apps/Sandbox/main.cpp's desktop path
// unchanged; that file remains the reference for the full WASD/mouse-look +
// Android twin-stick-touch implementation. This app drops the touch branch to
// stay focused on the UI and texture material.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "aura/Core/AuraCore.h"
#include "aura/Core/Camera/Camera.h"
#include "aura/Core/Engine.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/Core/ResourceManager/ResourceManager.h"
#include "aura/Core/TextOverlay/TextOverlay.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Material.h"
#include "aura/Utils/ColorsDefinitions.h"

#ifndef AURA_HAS_UI
#error "UIPlayground demonstrates aura3d::ui; configure with -DAURA_ENABLE_UI=ON (the default)."
#endif
#include "aura/UI/AuraUI.h"

using namespace aura3d;

namespace {

/// One drawable instance: geometry, surface, and where it sits in the world.
struct SceneObject {
    MeshHandle mesh = INVALID_HANDLE;
    MaterialHandle material = INVALID_HANDLE;
    glm::vec3 position{0.0f};
    float spinSpeed = 0.0f; // radians/second about +Y
};

constexpr size_t kMaxObjects = 48;

//! Golden-angle spiral: each new object lands at a fixed angular step from the
//! last, with radius growing as sqrt(index). That spacing is what keeps
//! objects from ever landing on top of each other however many are spawned,
//! with no randomness needed -- the layout is identical on every run.
constexpr float kGoldenAngle = 2.399963229f; // radians, ~137.5 degrees

[[nodiscard]] glm::vec3 spawnPosition(size_t index) noexcept
{
    const float angle = static_cast<float>(index) * kGoldenAngle;
    const float radius = 0.85f * std::sqrt(static_cast<float>(index) + 1.0f);
    return {radius * std::cos(angle), 0.0f, radius * std::sin(angle)};
}

// The animated sprite: a texture regenerated every frame, drawn directly
// through drawBatch2D -- independent of both the 3D scene and the UI's own
// batch.

constexpr u32 kSpriteSize = 96;

/**
 * @brief Refills @p pixels with one frame of a plasma-style animation.
 *
 * Coverage only -- RGB stays white, alpha carries the shape -- exactly the
 * convention FontAtlas rasterizes glyphs with. That is what lets a single
 * texture be recoloured for free by the vertex colour drawBatch2D() blends it
 * against, rather than needing one texture per hue.
 *
 * @note This differs from how FontAtlas or AuraUI's own atlas is kept up to
 * date on purpose. Those upload a small dirty rectangle only when new content
 * was actually rasterized, because their content is static once drawn -- a
 * glyph, once rasterized, never changes. This texture is the other case: the
 * whole image changes every frame, so there is no sparse region to track and
 * the full @p size x @p size rectangle is regenerated and reuploaded each
 * time. Reach for FontAtlas's lazy, dirty-rectangle pattern when content is
 * static and drawn repeatedly; reach for this whole-texture pattern when it
 * is not static to begin with.
 */
void regenerateSprite(std::vector<u8>& pixels, u32 size, float time) noexcept
{
    for (u32 y = 0; y < size; ++y)
    {
        for (u32 x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Three overlapping sine waves at different speeds and axes read
            // as a slowly churning plasma rather than a simple ripple.
            const float wave = std::sin(u * 8.0f + time)
                             + std::sin(v * 7.0f - time * 1.3f)
                             + std::sin((u + v) * 6.0f + time * 0.7f);
            const float coverage = std::clamp(0.5f + 0.16f * wave, 0.0f, 1.0f);

            // A radial falloff from the centre turns the square texture into
            // a soft round blob instead of a filled tile.
            const float dx = u - 0.5f;
            const float dy = v - 0.5f;
            const float radial = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy) * 2.0f, 0.0f, 1.0f);

            const auto alpha = static_cast<u8>(std::clamp(coverage * radial, 0.0f, 1.0f) * 255.0f);

            u8* texel = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            texel[0] = 255;
            texel[1] = 255;
            texel[2] = 255;
            texel[3] = alpha;
        }
    }
}

/// Standard HSV(hue, 1, 1) -> RGB, used to turn one "Hue" slider into a vivid
/// colour without needing three separate R/G/B sliders in the panel.
[[nodiscard]] glm::vec3 hueToRgb(float hue01) noexcept
{
    const float h = std::clamp(hue01, 0.0f, 1.0f) * 6.0f;
    const float x = 1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f);

    if (h < 1.0f) return {1.0f, x, 0.0f};
    if (h < 2.0f) return {x, 1.0f, 0.0f};
    if (h < 3.0f) return {0.0f, 1.0f, x};
    if (h < 4.0f) return {0.0f, x, 1.0f};
    if (h < 5.0f) return {x, 0.0f, 1.0f};
    return {1.0f, 0.0f, x};
}

/// Appends one axis-aligned textured quad to a 2D batch, matching the vertex
/// order AuraUI and TextOverlay both use (top-left, top-right, bottom-right,
/// bottom-left) so the two triangles share the quad's diagonal.
void appendQuad(std::vector<gfx::Vertex2D>& vertices, std::vector<u32>& indices,
                const glm::vec2& min, const glm::vec2& max, const glm::vec4& color)
{
    const auto base = static_cast<u32>(vertices.size());

    vertices.push_back({{min.x, min.y}, {0.0f, 0.0f}, color});
    vertices.push_back({{max.x, min.y}, {1.0f, 0.0f}, color});
    vertices.push_back({{max.x, max.y}, {1.0f, 1.0f}, color});
    vertices.push_back({{min.x, max.y}, {0.0f, 1.0f}, color});

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
    indices.push_back(base + 0);
}

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

    const wma::WindowDetails* wd = windowManager->getWindowDetails();
    int lastWindowWidth = wd->width;
    int lastWindowHeight = wd->height;

    Camera camera = Camera::perspective({.fovDeg = 55.0f,
                                         .aspect = static_cast<float>(lastWindowWidth) / static_cast<float>(lastWindowHeight),
                                         .nearZ  = 0.1f,
                                         .farZ   = 100.0f});

    // Set back and above the spiral's centre, looking down and in -- the
    // spiral grows outward from the origin in the XZ plane, and this framing
    // keeps every object visible even at kMaxObjects (checked by projecting
    // the full spiral through this exact position/fov offline; the outermost
    // object stays comfortably inside the frustum rather than at its edge).
    camera.setPosition({0.0f, 6.0f, 10.5f});
    float camYaw = -90.0f;
    float camPitch = -30.0f;
    camera.setRotation(camYaw, camPitch);

    bool moveForward = false, moveBack = false, moveLeft = false, moveRight = false;
    bool moveUp = false, moveDown = false;
    constexpr float kMouseSensitivity = 0.1f;

    /*
     * Mouse-look and a pointer-driven UI want opposite things from the
     * cursor: the first needs it captured and invisible, the second needs it
     * free and on screen. Tab swaps between the two, exactly as
     * docs/12-immediate-mode-ui.md describes -- this is that pattern's
     * reference implementation.
     */
    bool uiVisible = true;
    mouse.setCursorEnabled(uiVisible);
    mouse.setMoveAction(wma::MouseAction{[&](const wma::WMAMousePosition& pos) {
        if (uiVisible)
            return;

        camYaw += static_cast<float>(pos.deltaX) * kMouseSensitivity;
        camPitch += static_cast<float>(pos.deltaY) * kMouseSensitivity;
        camera.setRotation(camYaw, camPitch);
    }});

    keyboard.addKeyAction(wma::KEY_TAB, wma::KeyAction{[&]() {
        uiVisible = !uiVisible;
        mouse.setCursorEnabled(uiVisible);
    }});

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

    gfx::LightUBO light;
    light.direction = {-0.4f, -1.0f, -0.5f};
    light.color = {1.0f, 0.98f, 0.92f, 1.0f};
    light.intensity = 1.0f;
    light.ambient = 0.2f;
    r->setLight(light);

    // Geometry and materials for spawned objects. Both textures are loaded
    // once; every crate object shares one material, every orb another.
    const MeshHandle cubeMesh = r->createMesh(MeshLoader::createCube());
    const MeshHandle sphereMesh = r->createMesh(MeshLoader::createSphere(3));

    const TextureHandle crateTex = resources->loadTexture(settings->getTexturesPath() + "crate.png");
    const TextureHandle orbTex = resources->loadTexture(settings->getTexturesPath() + "orb.png");

    Material crateMaterial;
    crateMaterial.albedo = crateTex;
    crateMaterial.roughness = 0.8f;

    Material orbMaterial;
    orbMaterial.albedo = orbTex;
    orbMaterial.roughness = 0.4f;

    const MaterialHandle crateMat = r->createMaterial(crateMaterial);
    const MaterialHandle orbMat = r->createMaterial(orbMaterial);

    std::vector<SceneObject> objects = {
        {cubeMesh, crateMat, spawnPosition(0), 0.6f},
        {sphereMesh, orbMat, spawnPosition(1), -0.5f},
        {cubeMesh, crateMat, spawnPosition(2), 0.6f},
    };
    // Rebuilt each frame from `objects` and handed to drawMeshes() in one
    // call, exactly as apps/Sandbox does.
    std::vector<IRenderer::DrawItem> drawItems;

    bool spinEnabled = true;
    float spinSpeedScale = 1.0f;
    float spinTime = 0.0f; // Its own clock: pausing holds objects in place.

    const colors::RGBf clear = colors::MIDNIGHT_BLUE_F;
    r->setClearColor(clear.r, clear.g, clear.b, 1.0f);

    // Built once: allocates the glyph atlas texture, like TextOverlay's own.
    TextOverlayDesc overlayDesc;
    overlayDesc.pixelHeight = 18.0f;
    TextOverlay overlay(r, overlayDesc);

    // The UI. It draws through the same 2D pipeline as the sprite below, so
    // this same code renders unchanged on every backend -- see docs/12 and
    // docs/13 for how it stays a single draw call regardless of panel count.
    ui::ContextDesc uiDesc;
    uiDesc.pixelHeight = 15.0f;
    ui::Context gui(r, uiDesc);
    gui.attachInput(*windowManager);

    // The animated sprite's texture: allocated once, rewritten in full every
    // frame it animates. See regenerateSprite()'s comment for why that upload
    // shape is deliberate here rather than a bug.
    const TextureHandle spriteTexture = r->createDynamicTexture(kSpriteSize, kSpriteSize);
    std::vector<u8> spritePixels(static_cast<size_t>(kSpriteSize) * kSpriteSize * 4);

    bool spriteAnimate = true;
    float spriteSpeed = 1.0f;
    float spriteHue = 0.55f;
    float spriteScale = 160.0f;
    float spriteAlpha = 0.9f;
    float spriteTime = 0.0f;

    std::vector<gfx::Vertex2D> spriteVertices;
    std::vector<u32> spriteIndices;

    r->setTransform(camera.buildUBO());

    r->run([&]() {
        if (wd->width != lastWindowWidth || wd->height != lastWindowHeight)
        {
            lastWindowWidth = wd->width;
            lastWindowHeight = wd->height;
            camera.setPerspective({.fovDeg = 55.0f,
                                   .aspect = static_cast<float>(lastWindowWidth) / static_cast<float>(lastWindowHeight),
                                   .nearZ  = 0.1f,
                                   .farZ   = 100.0f});
        }

        const float dt = static_cast<float>(windowManager->getWindowFlags()->deltaTime) / 1000.0f;

        if (spinEnabled)
            spinTime += dt * spinSpeedScale;
        if (spriteAnimate)
            spriteTime += dt * spriteSpeed;

        const glm::vec3 forward = camera.forward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

        glm::vec3 moveDir{0.0f};
        if (moveForward) moveDir += forward;
        if (moveBack)    moveDir -= forward;
        if (moveRight)   moveDir += right;
        if (moveLeft)    moveDir -= right;
        if (moveUp)      moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
        if (moveDown)    moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

        constexpr float kMoveSpeed = 4.0f;
        if (glm::length(moveDir) > 0.0f)
            camera.setPosition(camera.position() + glm::normalize(moveDir) * kMoveSpeed * dt);

        r->beginRenderPass();

        // -- 3D: the spawned/removed objects --------------------------------
        drawItems.resize(objects.size());
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const SceneObject& object = objects[i];

            glm::mat4 model = glm::translate(glm::mat4(1.0f), object.position);
            if (object.spinSpeed != 0.0f)
                model = glm::rotate(model, spinTime * object.spinSpeed, glm::vec3(0.0f, 1.0f, 0.0f));

            drawItems[i] = {object.mesh, object.material, model};
        }
        r->setTransform(camera.buildUBO());
        r->drawMeshes(drawItems);

        overlay.drawFPS(10.0f, 10.0f);

        // -- 2D: the animated sprite, drawn independently of the UI ---------
        if (spriteAnimate)
        {
            regenerateSprite(spritePixels, kSpriteSize, spriteTime);
            r->updateTextureRegion(spriteTexture, 0, 0, kSpriteSize, kSpriteSize, spritePixels.data());
        }

        // The margin grows with the scale slider, so the corner nearest the
        // edge stays a fixed 16px inset at every size instead of the sprite
        // clipping off-screen once Scale is pushed past a fixed margin.
        const glm::vec2 half{spriteScale * 0.5f, spriteScale * 0.5f};
        const float margin = spriteScale * 0.5f + 16.0f;
        const glm::vec2 anchor{static_cast<float>(lastWindowWidth) - margin,
                               static_cast<float>(lastWindowHeight) - margin};
        const glm::vec4 spriteColor{hueToRgb(spriteHue), spriteAlpha};

        spriteVertices.clear();
        spriteIndices.clear();
        appendQuad(spriteVertices, spriteIndices, anchor - half, anchor + half, spriteColor);
        r->drawBatch2D(spriteVertices, spriteIndices, spriteTexture);

        // -- UI: reads and writes every variable above -----------------------
        if (uiVisible)
        {
            gui.newFrame();

            if (gui.beginPanel("Objects", {16.0f, 40.0f}, 240.0f))
            {
                char count[32];
                std::snprintf(count, sizeof(count), "%zu / %zu objects", objects.size(), kMaxObjects);
                gui.label(count);
                gui.separator();

                if (gui.button("+ Crate") && objects.size() < kMaxObjects)
                    objects.push_back({cubeMesh, crateMat, spawnPosition(objects.size()), 0.6f});

                if (gui.button("+ Orb") && objects.size() < kMaxObjects)
                    objects.push_back({sphereMesh, orbMat, spawnPosition(objects.size()), -0.5f});

                if (gui.button("Remove last") && !objects.empty())
                    objects.pop_back();

                gui.separator();
                gui.checkbox("Spin", spinEnabled);
                gui.sliderFloat("Spin speed", spinSpeedScale, 0.0f, 3.0f);

                gui.endPanel();
            }

            if (gui.beginPanel("Sprite", {272.0f, 40.0f}, 240.0f))
            {
                gui.label("Regenerated live, 96x96");
                gui.separator();

                gui.checkbox("Animate", spriteAnimate);
                gui.sliderFloat("Speed", spriteSpeed, 0.0f, 4.0f);
                gui.sliderFloat("Hue", spriteHue, 0.0f, 1.0f);
                gui.sliderFloat("Scale", spriteScale, 32.0f, 320.0f);
                gui.sliderFloat("Alpha", spriteAlpha, 0.0f, 1.0f);

                gui.endPanel();
            }

            gui.render(); // Both panels above: one draw call regardless.
        }

        r->endRenderPass();
    });

    return 0;
}
