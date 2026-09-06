#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "aura/Core/Engine.h"
#include "aura/Core/AudioEngine/AudioEngine.h"
#include "aura/Core/AuraCore.h"
#include "aura/Core/Camera/Camera.h"
#include "aura/Core/DebugMode/DebugMode.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/Core/ResourceManager/ResourceManager.h"
#include "aura/Core/TextOverlay/TextOverlay.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Material.h"
#include "aura/Utils/ColorsDefinitions.h"

#ifdef AURA_HAS_UI
#include "aura/UI/UI.hpp"
#endif

using namespace aura3d;

namespace {

/// One drawable instance: geometry, surface, and where it sits in the world.
struct SceneObject {
    MeshHandle mesh;
    MaterialHandle material;
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    float spinSpeed = 0.0f; // radians/second about +Y
};

#ifdef AURA_HAS_UI

/*
 * Everything below this point, down to appendQuad(), used to be the separate
 * apps/UIPlayground demo: objects spawned and removed at runtime through UI
 * panels, and a procedurally animated 2D sprite. It is folded in here rather
 * than kept apart because both were the same kind of thing -- IRenderer plus
 * aura3d::ui driving a live scene -- and a second near-identical app cost more
 * to keep in sync than a few extra panels cost to read. See
 * docs/16-auraui-toolkit.md for what the widgets themselves do; this is still
 * their reference usage, alongside apps/AuraUIDemo.
 *
 * All of it is additive: the WASD/mouse-look/touch camera, the audio demo and
 * the floor-plus-three-objects scene above are untouched. What follows spawns
 * *more* objects alongside them (offset behind the original scene so the two
 * do not overlap on spawn) and lays out a docked sidebar of cards beside it.
 */

constexpr size_t kMaxSpawnedObjects = 48;

//! Golden-angle spiral: each new object lands at a fixed angular step from the
//! last, with radius growing as sqrt(index). That spacing is what keeps
//! objects from ever landing on top of each other however many are spawned,
//! with no randomness needed -- the layout is identical on every run.
constexpr float kGoldenAngle = 2.399963229f; // radians, ~137.5 degrees

//! Shifted well behind the original scene (see the stress-object grid further
//! down, which offsets the same way) so the first few spawns do not land on
//! top of the floor's existing cubes and orb.
constexpr glm::vec3 kSpawnOrigin{0.0f, 0.0f, -6.0f};

[[nodiscard]] glm::vec3 spawnPosition(size_t index) noexcept
{
    const float angle = static_cast<float>(index) * kGoldenAngle;
    const float radius = 0.85f * std::sqrt(static_cast<float>(index) + 1.0f);
    return kSpawnOrigin + glm::vec3{radius * std::cos(angle), 0.0f, radius * std::sin(angle)};
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

/*
 * The Sandbox's look
 *
 * Split in two on purpose. A palette is the colours; a theme is the palette
 * plus every decision that is *not* a colour -- shape, spacing, weight. Keeping
 * them separable is what lets the Theme card's radio group swap one without
 * throwing the other away, and it is the same split Theme::applyPalette draws.
 */

/// A deep slate ground under one cyan accent. Sits cooler and darker than the
/// engine default so the sidebar reads as glass laid over the midnight-blue
/// scene rather than as another grey box on top of it.
[[nodiscard]] ui::Palette auraPalette()
{
    ui::Palette palette;
    palette.window         = {0.063f, 0.075f, 0.106f, 0.965f};
    palette.titleBar       = {0.110f, 0.133f, 0.184f, 1.000f};
    palette.surface        = {0.149f, 0.173f, 0.231f, 1.000f};
    palette.surfaceHovered = {0.204f, 0.239f, 0.310f, 1.000f};
    palette.surfaceActive  = {0.157f, 0.451f, 0.529f, 1.000f};
    palette.accent         = {0.310f, 0.765f, 0.831f, 1.000f};
    palette.text           = {0.878f, 0.906f, 0.949f, 1.000f};
    palette.border         = {1.000f, 1.000f, 1.000f, 0.070f};
    palette.overlay        = {0.047f, 0.055f, 0.078f, 0.980f};
    return palette;
}

//! Every card shares this shape: controls softly rounded, sliders and the
//! scrollbar thumb pills. One rounding value restyled together is what a
//! "Rounding" slider in the Theme card can mean anything by -- a pile of
//! unrelated radii would not be a shape language.
constexpr std::array<ui::Part, 3> kRoundedParts = {ui::Part::Button, ui::Part::Checkbox,
                                                   ui::Part::TextField};

/// @p palette plus this app's shape language and spacing.
[[nodiscard]] ui::Theme shapedTheme(const ui::Palette& palette)
{
    ui::Theme theme(palette);

    //! Room to breathe. The engine's defaults are sized for a debug overlay
    //! squeezed into a corner; a tool panel that is the point of the screen can
    //! afford a couple more pixels a row, and reads as designed rather than
    //! dense.
    theme.metrics.rowHeight   = 24.0f;
    theme.metrics.itemSpacing = 6.0f;
    theme.metrics.padding     = 10.0f;
    theme.metrics.fontSize    = 14.0f;

    for (const ui::Part part : kRoundedParts)
        theme[part].rounding = 5.0f;

    //! A slider draws its accent fill under its own value; halfway to the
    //! surface rather than the full accent keeps a caption over it readable
    //! in any palette, since the surface is whatever the text is *not*.
    theme[ui::Part::Slider].accent = glm::mix(palette.accent, palette.surface, 0.5f);

    //! A hairline is what makes a button read as a raised thing rather than a
    //! flat patch, when button and card are two greys from the same family.
    theme[ui::Part::Button].border = ui::ColorSet::flat(palette.border);
    theme[ui::Part::Button].borderWidth = 1.0f;
    theme[ui::Part::Button].padding = 10.0f;

    return theme;
}

/*
 * Semantic one-off styles, as ui::Style patches. Each names only what it
 * changes, so every one of them still follows the theme for everything else --
 * which is why they keep tracking the Theme card's Rounding slider while it is
 * being dragged, instead of freezing whatever the shape was when they were
 * written.
 */

/// A quieter label, for the small-caps headings that group a card's rows.
/// Labels draw no surface, so colour is the only thing separating a heading
/// from body text.
///
/// The palette's own text at reduced alpha rather than a fixed grey: a heading
/// has to sit one step below body text in *every* palette, and a colour picked
/// against the dark one washes out on the light one.
[[nodiscard]] ui::Style heading(const ui::Theme& theme)
{
    glm::vec4 muted = theme[ui::Part::Label].text;
    muted.a *= 0.60f;

    return ui::Style{}.textColor(muted);
}

/*
 * The two semantic buttons name their *own* text colour as well as their fill.
 * A style whose fill is dark in every palette cannot inherit the palette's text
 * -- that is near-black in a light theme, and the label disappears into the
 * button. Owning both halves is what makes these safe to drop anywhere.
 */

/// The affirmative action in a group: accent-tinted, so exactly one button in
/// a card pulls the eye.
[[nodiscard]] ui::Style primary()
{
    return ui::Style{}
        .fill(ui::ColorSet{{0.129f, 0.365f, 0.427f, 1.0f},
                           {0.169f, 0.475f, 0.549f, 1.0f},
                           {0.216f, 0.588f, 0.667f, 1.0f}})
        .textColor({0.945f, 0.976f, 0.984f, 1.0f});
}

/// Destructive, and the only red on screen.
[[nodiscard]] ui::Style danger()
{
    return ui::Style{}
        .fill(ui::ColorSet{{0.396f, 0.153f, 0.180f, 1.0f},
                           {0.522f, 0.196f, 0.227f, 1.0f},
                           {0.647f, 0.243f, 0.278f, 1.0f}})
        .textColor({0.988f, 0.925f, 0.925f, 1.0f});
}

/// A titled, rounded card -- the sidebar's unit of grouping. Every card in
/// this file starts from one of these, styled once from the palette rather
/// than restated per card.
ui::Column& card(ui::Widget& parent, std::string_view title, const ui::Theme& theme)
{
    auto& panel = parent.add<ui::Column>();
    panel.style().fill(theme.palette().window).rounded(8.0f);
    panel.layout().padding = ui::Thickness::all(14.0f);
    panel.setSpacing(10.0f);

    auto& heading_ = panel.add<ui::Label>(std::string{title});
    heading_.setFontSize(12.0f);
    heading_.style() = heading(theme);

    panel.add<ui::Separator>();

    return panel;
}

/// Appends one axis-aligned textured quad to a 2D batch, matching the vertex
/// order the engine's 2D pipeline uses everywhere (top-left, top-right,
/// bottom-right, bottom-left) so the two triangles share the quad's diagonal.
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

#endif // AURA_HAS_UI

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

    // camera
    const wma::WindowDetails* wd = r->getWindowManager()->getWindowDetails();
    int lastWindowWidth = wd->width;
    int lastWindowHeight = wd->height;

    Camera camera = Camera::perspective({.fovDeg = 60.0f,
                                         .aspect = static_cast<float>(lastWindowWidth) / static_cast<float>(lastWindowHeight),
                                         .nearZ  = 0.1f,
                                         .farZ   = 100.0f});

    camera.setPosition({0.0f, 0.8f, 4.5f});

    float camYaw = -90.0f;
    float camPitch = -8.0f;
    camera.setRotation(camYaw, camPitch);

    bool moveForward = false, moveBack = false, moveLeft = false, moveRight = false;
    bool moveUp = false, moveDown = false;

    constexpr float kMouseSensitivity = 0.1f;

    /*
     * Gates gameplay input while the tool sidebar is up: a pointer-driven UI
     * and mouse-look want opposite things from the cursor, and typing into a
     * field must not also walk the camera. Declared unconditionally (rather
     * than only under AURA_HAS_UI) so the movement and look code below needs
     * no separate branch for a build with the UI compiled out -- there it
     * simply never becomes true.
     */
    bool sidebarOpen = false;

#ifdef __ANDROID__
    // Twin-stick touch controls: left thumb walks, right thumb looks, both at
    // the same time. This needs wma's TouchListener rather than its
    // MouseListener -- SDL can synthesize mouse events from touch, but a mouse
    // has one cursor, so every finger collapses into a single stream and only
    // one stick could ever be active at a time.
    wma::TouchListener& touch = windowManager->getTouchListener();

    // Each stick tracks the specific finger that claimed it, by fingerId, so
    // lifting one thumb never disturbs the other -- and a third finger is
    // ignored rather than hijacking a stick already in use.
    constexpr wma::TouchFingerId kNoFinger = -1;
    wma::TouchFingerId lookFinger = kNoFinger;
    wma::TouchFingerId moveFinger = kNoFinger;
    double moveOriginX = 0.0;
    double moveOriginY = 0.0;

    constexpr float kTouchLookSensitivity = 0.25f;
    constexpr double kMoveDeadZonePx = 24.0;

    auto clearMovement = [&]() {
        moveForward = moveBack = moveLeft = moveRight = false;
    };

    // Unconditional, and never gated by sidebarOpen: the sidebar is always up
    // on Android (there is no separate menu gesture), and the two share the
    // screen rather than taking turns with it.
    touch.setDownAction(wma::TouchInputCallback::from(
        [&](const wma::WMATouchPoint& p) {
            // wd->width is live, so the split follows orientation changes.
            const bool isLookZone = p.x > (static_cast<double>(wd->width) * 0.5);
            if (isLookZone)
            {
                if (lookFinger == kNoFinger)
                    lookFinger = p.fingerId;
            }
            if (moveFinger == kNoFinger)
            {
                moveFinger = p.fingerId;
                // Where the thumb lands becomes the stick's centre.
                moveOriginX = p.x;
                moveOriginY = p.y;
            }
        }));

    touch.setMoveAction(wma::TouchInputCallback::from(
        [&](const wma::WMATouchPoint& p) {
            if (p.fingerId == lookFinger)
            {
                camYaw += static_cast<float>(p.deltaX) * kTouchLookSensitivity;
                camPitch += static_cast<float>(p.deltaY) * kTouchLookSensitivity;
                camera.setRotation(camYaw, camPitch);
            }
            if (p.fingerId == moveFinger)
            {
                // Virtual thumbstick: displacement from the origin picks a
                // direction, with a dead zone so a resting thumb doesn't creep.
                // Y is up (see wma's SDLTouchListener), so dragging up walks
                // forward.
                const double dx = p.x - moveOriginX;
                const double dy = moveOriginY - p.y;
                moveRight = dx > kMoveDeadZonePx;
                moveLeft = dx < -kMoveDeadZonePx;
                moveForward = dy > kMoveDeadZonePx;
                moveBack = dy < -kMoveDeadZonePx;
            }
        }));

    touch.setUpAction(wma::TouchInputCallback::from(
        [&](const wma::WMATouchPoint& p) {
            if (p.fingerId == lookFinger)
            {
                lookFinger = kNoFinger;
            }
            if (p.fingerId == moveFinger)
            {
                moveFinger = kNoFinger;
                clearMovement();
            }
        }));
#else
    // Relative mouse mode: hidden and captured for camera look, released
    // whenever the sidebar is open. Cursor position is polled by the UI
    // (see UIView::attachInput()'s doc comment), not bound, so it stays
    // correct while this callback also owns mouse motion.
    mouse.setCursorEnabled(false);

    mouse.setMoveAction(wma::MouseAction{wma::MouseAction::PositionCallback(
        [&](const wma::WMAMousePosition& pos) {
            if (sidebarOpen)
                return;

            camYaw += static_cast<float>(pos.deltaX) * kMouseSensitivity;
            camPitch += static_cast<float>(pos.deltaY) * kMouseSensitivity;
            camera.setRotation(camYaw, camPitch);
        })});
#endif

    /*
     * Movement bindings. Gated inline on sidebarOpen rather than through a
     * suspended input context: a press while the sidebar is up is simply
     * dropped, and a key already held when the sidebar opens is not
     * re-applied, so it cannot get stuck down for the rest of that visit.
     */
    const auto bindHeld = [&](wma::Key key, bool& flag) {
        keyboard.addKeyAction(key, wma::KeyAction{
            [&flag, &sidebarOpen]() { if (!sidebarOpen) flag = true; },
            [&flag]() { flag = false; }
        });
    };
    const auto bindPress = [&](wma::Key key, std::function<void()> action) {
        keyboard.addKeyAction(key, wma::KeyAction{
            [&sidebarOpen, action = std::move(action)]() { if (!sidebarOpen) action(); }
        });
    };

    bindHeld(wma::KEY_W, moveForward);
    bindHeld(wma::KEY_S, moveBack);
    bindHeld(wma::KEY_A, moveLeft);
    bindHeld(wma::KEY_D, moveRight);
    bindHeld(wma::KEY_SPACE, moveUp);
    bindHeld(wma::KEY_LEFT_SHIFT, moveDown);

    // light
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
    // Both are generated by scripts/gen_sandbox_textures.py.
    const TextureHandle crateTex = resources->loadTexture(settings->getTexturesPath() + "crate.png");
    const TextureHandle orbTex   = resources->loadTexture(settings->getTexturesPath() + "orb.png");

    const TextureHandle whiteTex   = r->createSolidColorTexture(255, 255, 255);

#ifndef NDEBUG
    // Bindless-texture stress check: the old per-texture-per-swapchain-image-
    // per-pipeline descriptor scheme exhausted its pool around the 42nd
    // texture (VK_ERROR_OUT_OF_POOL_MEMORY thrown from VkDescriptorManager).
    // Creating well past that ceiling here means a regression on this front
    // surfaces immediately as a crash on startup in every debug build,
    // rather than only under a manually-triggered repro.
    for (int i = 0; i < 200; ++i)
    {
        const u8 shade = static_cast<u8>((i * 37) % 256);
        r->createSolidColorTexture(shade, 255 - shade, 128, 255);
    }
#endif

    // materials
    Material crateMaterial;
    crateMaterial.albedo = crateTex;
    crateMaterial.roughness = 0.8f;

    Material orbMaterial;
    orbMaterial.albedo = orbTex;
    orbMaterial.roughness = 0.6f;

    Material floorMaterial;
    floorMaterial.albedo = whiteTex;
    floorMaterial.tint = {0.6f, 0.65f, 0.7f, 1.0f};

    const MaterialHandle floorMat  = r->createMaterial(floorMaterial);
    const MaterialHandle crateMat  = r->createMaterial(crateMaterial);
    const MaterialHandle orbMat    = r->createMaterial(orbMaterial);

    std::vector<SceneObject> scene = {
        // Floor.
        {planeMesh, floorMat, {0.0f, -0.75f, 0.0f}, {8.0f, 1.0f, 8.0f}, 0.0f},
        // Two spinning cubes flanking a sphere.
        {cubeMesh,   crateMat, {-1.5f, 0.0f, 0.0f}, glm::vec3(1.0f), 0.9f},
        {sphereMesh, orbMat,   { 0.0f, 0.0f, 0.0f}, glm::vec3(1.2f), 0.25f},
        {cubeMesh,   crateMat, { 1.5f, 0.0f, 0.0f}, glm::vec3(1.0f), -0.9f},
    };

    // audio
    //
    // Same contract as the textures above: a missing file falls back (to silence
    // rather than a checkerboard) and logs a warning, so this runs with no audio
    // assets present -- and on a machine with no sound hardware at all, where
    // libwma hands back its null device. Generated by
    // scripts/gen_sandbox_audio.py.
    AudioEngine* audio = engine.audio();
    const std::string audioPath = settings->getAudioPath();

    const AudioClipHandle humClip   = resources->loadSound(audioPath + "orb_hum.wav");
    const AudioClipHandle blipClip  = resources->loadSound(audioPath + "blip.wav");
    // Streaming marks this as long-form: see AudioClipMode's note on what that
    // does and does not currently change.
    const AudioClipHandle musicClip = resources->loadSound(audioPath + "music.wav",
                                                          AudioClipMode::Streaming);

    // Unspatialized and looping: music has no position in the scene, so it stays
    // at an even level in both ears no matter where the camera goes.
    AudioSourceDesc musicDesc;
    musicDesc.clip = musicClip;
    musicDesc.loop = true;
    musicDesc.gain = 0.35f;
    const AudioSourceHandle musicVoice = audio->play(musicDesc);

    // The orb, in contrast, *is* a point in the world. This is the voice that
    // demonstrates 3D audio: walk around the sphere and it pans across the
    // stereo field, walk away and it fades out.
    const glm::vec3 orbPosition = scene[2].position;

    AudioSourceDesc orbDesc;
    orbDesc.clip        = humClip;
    orbDesc.loop        = true;
    orbDesc.gain        = 0.9f;
    orbDesc.spatial     = true;
    orbDesc.position    = orbPosition;
    orbDesc.minDistance = 1.5f;   // full volume inside this radius
    orbDesc.maxDistance = 14.0f;  // inaudible past it
    const AudioSourceHandle orbVoice = audio->play(orbDesc);

    // E fires a one-shot. Bound on press only, so holding the key does not
    // retrigger it every frame -- and through bindPress(), so typing an "e"
    // into a text field is a letter rather than also a blip.
    bindPress(wma::KEY_E, [&]() { (void)audio->play(blipClip, 0.8f); });

    // M mutes and unmutes, which is also the quickest way to confirm the master
    // gain is reaching the mixer.
    bindPress(wma::KEY_M, [&]() {
        audio->setMasterVolume(audio->masterVolume() > 0.0f ? 0.0f : 1.0f);
    });

    INK_INFO << "Audio: " << (audio->isDeviceRunning() ? "running" : "silent (no device)")
             << " -- E: one-shot blip, M: mute toggle. "
             << "The orb hums in 3D; walk around it to hear the pan.";

    if (!isValidHandle(musicVoice) || !isValidHandle(orbVoice))
        INK_WARN << "Audio: a demo voice failed to start";

    /*
     * AURA_SANDBOX_STRESS_OBJECTS=N adds a grid of N spinning objects around
     * the scene. Four objects is far too few to say anything about the
     * renderer's threaded submission path -- the fan-out does not even engage
     * below its per-worker minimum -- so this exists to give that path a real
     * batch to chew on and to make the recording-time measurement meaningful.
     */
    if (const char* stressEnv = std::getenv("AURA_SANDBOX_STRESS_OBJECTS"))
    {
        const int stressCount = std::atoi(stressEnv);
        const int perRow = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(stressCount))));

        for (int i = 0; i < stressCount; ++i)
        {
            const int row = i / perRow;
            const int col = i % perRow;
            const float x = static_cast<float>(col - perRow / 2) * 1.6f;
            const float z = static_cast<float>(row - perRow / 2) * 1.6f - 6.0f;

            scene.push_back({(i % 2) ? cubeMesh : sphereMesh,
                             (i % 2) ? crateMat : orbMat,
                             {x, 0.0f, z},
                             glm::vec3(0.45f),
                             (i % 3 == 0) ? 0.6f : -0.4f});
        }

        INK_INFO << "Sandbox stress mode: " << scene.size() << " objects";
    }

    //! Rebuilt each frame from `scene` and handed to drawMeshes() in one call,
    //! which is what lets the Vulkan backend record it across worker threads.
    std::vector<IRenderer::DrawItem> drawItems(scene.size());

    const colors::RGBf clear = colors::MIDNIGHT_BLUE_F;
    r->setClearColor(clear.r, clear.g, clear.b, 1.0f);

    // Built once: allocates the glyph atlas texture. Characters are rasterized
    // into it the first time they are drawn, and each string then costs a
    // single batched draw call on the renderer's unlit 2D pipeline.
    TextOverlayDesc overlayDesc;
    overlayDesc.pixelHeight = 18.0f;
    TextOverlay overlay(r, overlayDesc);

#ifdef AURA_HAS_UI
    /*
     * AuraUI's retained widget toolkit. It draws through the same
     * backend-agnostic DrawList -> IRenderer::drawBatch2D() pipeline the text
     * overlay uses, so this same tree runs unchanged on Vulkan, OpenGL, Metal
     * and the software rasterizer, and the whole sidebar costs a single draw
     * call. See docs/16-auraui-toolkit.md.
     */
    constexpr float kSidebarWidth = 300.0f;

    ui::UIViewDesc uiDesc;
    ui::UIView ui(*r, uiDesc);
    ui.attachInput(*windowManager);
    ui.theme() = shapedTheme(auraPalette());

    /*
     * The sidebar is docked, not floating: a Row spanning the whole surface,
     * hit-test invisible itself so a click in the empty space beside the
     * sidebar falls through to the 3D scene, holding one fixed-width child
     * that is the actual scrolling column of cards.
     */
    auto& desktop = ui.root().setContent<ui::Row>();
    desktop.setHitTestVisible(false);

    auto& sidebar = desktop.add<ui::ScrollView>();
    sidebar.layout().width = ui::Length::px(kSidebarWidth);
    sidebar.setVisibility(ui::Visibility::Collapsed);

    auto& sidebarColumn = sidebar.setContent<ui::Column>();
    sidebarColumn.layout().padding = ui::Thickness::all(16.0f);
    sidebarColumn.setSpacing(14.0f);

    const ui::Theme& theme = ui.theme();

    /*
     * F1 toggles the sidebar and swaps the cursor between captured (camera
     * look) and free (clicking the sidebar); Escape only ever closes it, and
     * on Android it is permanently open since there is no separate menu
     * gesture -- the sidebar and the twin-stick controls share the screen.
     */
    const auto setSidebarOpen = [&](bool open) {
        sidebarOpen = open;
        mouse.setCursorEnabled(open); // ignored on Android; harmless there
        sidebar.setVisibility(open ? ui::Visibility::Visible : ui::Visibility::Collapsed);

        if (!open)
            moveForward = moveBack = moveLeft = moveRight = moveUp = moveDown = false;
    };

    keyboard.addKeyAction(wma::KEY_F1, wma::KeyAction{[&]() { setSidebarOpen(!sidebarOpen); }});
    keyboard.addKeyAction(wma::KEY_ESCAPE, wma::KeyAction{[&]() {
        if (sidebarOpen)
            setSidebarOpen(false);
    }});

    INK_INFO << "UI: F1 opens the tool sidebar, Escape closes it";

#ifdef __ANDROID__
    setSidebarOpen(true);
#endif

#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * A benchmark run never presses F1, so every capture taken so far measured
     * the sidebar-closed path twice and concluded the UI was free. Only the
     * debug build reads this -- a shipping Sandbox has no env backdoor.
     */
    if (const char* panels = std::getenv("AURA_SANDBOX_PANELS");
        panels != nullptr && *panels != '0')
    {
        setSidebarOpen(true);
        INK_INFO << "Sandbox: AURA_SANDBOX_PANELS set; tool sidebar open from frame one";
    }
#endif

    // -- Scene card: backend, FPS, motion, light, camera reset -------------
    auto& sceneCard = card(sidebarColumn, "Scene", theme);

    sceneCard.add<ui::Label>(std::string{RendererChoiceToString(r->getBackendType())});
    auto& fpsLabel = sceneCard.add<ui::Label>("0 FPS");

    sceneCard.add<ui::Separator>();

    auto& sceneSpin = sceneCard.add<ui::CheckBox>("Spin objects", true);

    sceneCard.add<ui::Separator>();

    auto& lightSlider = sceneCard.add<ui::Slider>(0.0f, 3.0f);
    lightSlider.value = light.intensity;
    lightSlider.value.changed().connect([&](f32 value) {
        light.intensity = value;
        r->setLight(light);
    });

    auto& ambientSlider = sceneCard.add<ui::Slider>(0.0f, 1.0f);
    ambientSlider.value = light.ambient;
    ambientSlider.value.changed().connect([&](f32 value) {
        light.ambient = value;
        r->setLight(light);
    });

    sceneCard.add<ui::Separator>();

    auto& resetCameraButton = sceneCard.add<ui::Button>("Reset camera");
    resetCameraButton.style() = primary();
    resetCameraButton.clicked.connect([&]() {
        camYaw = -90.0f;
        camPitch = -8.0f;
        camera.setPosition({0.0f, 0.8f, 4.5f});
        camera.setRotation(camYaw, camPitch);
    });

    // -- Objects card: spawn/remove, spin --------------------------------
    std::vector<SceneObject> spawned;
    std::vector<IRenderer::DrawItem> spawnedDrawItems;

    auto& objectsCard = card(sidebarColumn, "Objects", theme);

    auto& populationLabel = objectsCard.add<ui::Label>("0 / " + std::to_string(kMaxSpawnedObjects) +
                                                        " spawned");

    objectsCard.add<ui::Separator>();

    auto& spawnRow = objectsCard.add<ui::Row>();
    spawnRow.setSpacing(8.0f);

    auto& spawnCrateButton = spawnRow.add<ui::Button>("+ Crate");
    spawnCrateButton.style() = primary();
    spawnCrateButton.layout().width = ui::Length::fill();

    auto& spawnOrbButton = spawnRow.add<ui::Button>("+ Orb");
    spawnOrbButton.style() = primary();
    spawnOrbButton.layout().width = ui::Length::fill();

    //! Disabled rather than hidden while there is nothing to remove: the card
    //! keeps its height, so the rows below it don't jump every time the list
    //! empties.
    auto& removeLastButton = objectsCard.add<ui::Button>("Remove last");
    removeLastButton.setEnabled(false);

    objectsCard.add<ui::Separator>();

    auto& spawnSpinCheck = objectsCard.add<ui::CheckBox>("Spin", true);
    auto& spawnSpinSpeedSlider = objectsCard.add<ui::Slider>(0.0f, 3.0f);
    spawnSpinSpeedSlider.value = 1.0f;

    // -- Inspector card: name, shading/detail demo state, spawned list ----
    auto& inspectorCard = card(sidebarColumn, "Inspector", theme);

    auto& nameField = inspectorCard.add<ui::TextField>("sandbox");
    nameField.setPlaceholder("Scene name");

    inspectorCard.add<ui::Separator>();

    auto& shadingHeading = inspectorCard.add<ui::Label>("SHADING");
    shadingHeading.style() = heading(theme);
    auto& shadingRow = inspectorCard.add<ui::Row>();
    shadingRow.setSpacing(10.0f);

    ui::RadioGroup shadingGroup;
    for (const char* name : {"Lit", "Unlit", "Wireframe"})
        shadingGroup.add(shadingRow.add<ui::RadioButton>(name));
    shadingGroup.select(0);

    inspectorCard.add<ui::Separator>();

    auto& detailHeading = inspectorCard.add<ui::Label>("DETAIL");
    detailHeading.style() = heading(theme);
    auto& detailRow = inspectorCard.add<ui::Row>();
    detailRow.setSpacing(10.0f);

    ui::RadioGroup detailGroup;
    for (const char* name : {"Low", "Medium", "High"})
        detailGroup.add(detailRow.add<ui::RadioButton>(name));
    detailGroup.select(1);

    inspectorCard.add<ui::Separator>();

    //! Fixed height, so a long list scrolls rather than growing the sidebar
    //! past the bottom of the window. Rows are only ever removed from the
    //! end or all at once (matching the Objects card's own buttons), which is
    //! what lets each row capture its own index at creation and stay valid.
    auto& spawnedScroll = inspectorCard.add<ui::ScrollView>();
    spawnedScroll.layout().height = ui::Length::px(120.0f);
    auto& spawnedList = spawnedScroll.setContent<ui::Column>();

    int selectedSpawned = -1;

    const auto refreshSelection = [&]() {
        for (usize i = 0; i < spawnedList.childCount(); ++i)
        {
            auto& row = static_cast<ui::Button&>(spawnedList.childAt(i));
            row.style().fill(static_cast<int>(i) == selectedSpawned ? theme.palette().surfaceActive
                                                                    : theme.palette().surface);
        }
    };

    inspectorCard.add<ui::Separator>();

    auto& applyButton = inspectorCard.add<ui::Button>("Apply");
    applyButton.style() = primary();

    auto& statusLabel = inspectorCard.add<ui::Label>("");
    statusLabel.style().textColor(theme.palette().accent);

    applyButton.clicked.connect([&]() {
        statusLabel.text = "Name set to '" + nameField.text.get() + "'.";
    });

    auto& resetButton = inspectorCard.add<ui::Button>("Reset");
    resetButton.clicked.connect([&]() {
        nameField.text = "sandbox";
        statusLabel.text = "Reset.";
    });

    // -- Theme card: palette, rounding, disabled-state demo --------------
    auto& themeCard = card(sidebarColumn, "Theme", theme);

    auto& paletteRow = themeCard.add<ui::Row>();
    paletteRow.setSpacing(10.0f);

    ui::RadioGroup paletteGroup;
    for (const char* name : {"Aura", "Dark", "Light"})
        paletteGroup.add(paletteRow.add<ui::RadioButton>(name));

    auto& roundingSlider = themeCard.add<ui::Slider>(0.0f, 12.0f);
    roundingSlider.value = 5.0f;
    roundingSlider.value.changed().connect([&](f32 value) {
        for (const ui::Part part : kRoundedParts)
            ui.theme()[part].rounding = value;
    });

    paletteGroup.selectionChanged.connect([&](int index) {
        //! Rebuilt through shapedTheme() rather than applyPalette(), so
        //! swapping the colours keeps this app's spacing and shape instead of
        //! falling back to the engine's. Colours and shape are separate
        //! decisions and the radio group only makes one of them.
        const ui::Palette palette = index == 0   ? auraPalette()
                                    : index == 1 ? ui::Palette::dark()
                                                 : ui::Palette::light();

        ui.theme() = shapedTheme(palette);
        roundingSlider.value = ui.theme()[ui::Part::Button].rounding;
    });
    paletteGroup.select(0);

    themeCard.add<ui::Separator>();

    //! One instance, styled for itself. The patch names only the colours;
    //! rounding, padding and height still come from Part::Button, so the
    //! Rounding slider above still reaches it.
    auto& removeAllButton = themeCard.add<ui::Button>("Remove every object");
    removeAllButton.style() = danger();
    removeAllButton.setEnabled(false);

    themeCard.add<ui::Separator>();

    auto& advancedUnlock = themeCard.add<ui::CheckBox>("Unlock advanced");

    //! Greyed out rather than hidden: the card keeps its shape as the option
    //! becomes available.
    auto& advancedSpeedSlider = themeCard.add<ui::Slider>(0.0f, 3.0f);
    advancedSpeedSlider.value = 1.0f;
    advancedSpeedSlider.setEnabled(false);

    auto& advancedButton = themeCard.add<ui::Button>("Recalculate");
    advancedButton.setEnabled(false);

    advancedUnlock.checked.changed().connect([&](bool unlocked) {
        advancedSpeedSlider.setEnabled(unlocked);
        advancedButton.setEnabled(unlocked);
    });

    advancedSpeedSlider.value.changed().connect(
        [&](f32 value) { spawnSpinSpeedSlider.value = value; });

    // -- Wiring that needs every card above to already exist --------------
    const auto updatePopulation = [&]() {
        populationLabel.text =
            std::to_string(spawned.size()) + " / " + std::to_string(kMaxSpawnedObjects) +
            " spawned";
        removeLastButton.setEnabled(!spawned.empty());
        removeAllButton.setEnabled(!spawned.empty());
    };

    const auto spawnObject = [&](MeshHandle mesh, MaterialHandle material, float spinSpeed) {
        if (spawned.size() >= kMaxSpawnedObjects)
            return;

        const usize index = spawned.size();
        spawned.push_back({mesh, material, spawnPosition(index), glm::vec3(1.0f), spinSpeed});

        auto& row = spawnedList.add<ui::Button>("Object " + std::to_string(index));
        row.style().fill(theme.palette().surface);
        row.clicked.connect([&, index]() {
            selectedSpawned = static_cast<int>(index);
            refreshSelection();
        });

        updatePopulation();
    };

    spawnCrateButton.clicked.connect([&]() { spawnObject(cubeMesh, crateMat, 0.6f); });
    spawnOrbButton.clicked.connect([&]() { spawnObject(sphereMesh, orbMat, -0.5f); });

    removeLastButton.clicked.connect([&]() {
        if (spawned.empty())
            return;

        spawned.pop_back();
        spawnedList.remove(spawnedList.childAt(spawnedList.childCount() - 1));

        if (selectedSpawned == static_cast<int>(spawned.size()))
            selectedSpawned = -1;

        updatePopulation();
    });

    removeAllButton.clicked.connect([&]() {
        spawned.clear();
        spawnedList.clearChildren();
        selectedSpawned = -1;
        updatePopulation();
    });

    // -- The animated sprite: allocated once, rewritten in full every frame
    // -- it animates. See regenerateSprite()'s comment for why that upload
    // -- shape is deliberate here rather than a bug.
    const TextureHandle spriteTexture = r->createDynamicTexture(kSpriteSize, kSpriteSize);
    std::vector<u8> spritePixels(static_cast<size_t>(kSpriteSize) * kSpriteSize * 4);

    //! Time banked towards the next sprite refresh; see where it is spent.
    float spriteRefreshAccum = 0.0f;
    float spriteTime = 0.0f;

    std::vector<gfx::Vertex2D> spriteVertices;
    std::vector<u32> spriteIndices;

    auto& spriteCard = card(sidebarColumn, "Sprite", theme);
    spriteCard.add<ui::Label>("96x96 procedural");
    spriteCard.add<ui::Separator>();

    auto& spriteAnimateCheck = spriteCard.add<ui::CheckBox>("Animate", true);
    auto& spriteSpeedSlider = spriteCard.add<ui::Slider>(0.0f, 4.0f);
    spriteSpeedSlider.value = 1.0f;

    spriteCard.add<ui::Separator>();

    auto& spriteHueSlider = spriteCard.add<ui::Slider>(0.0f, 1.0f);
    spriteHueSlider.value = 0.55f;
    auto& spriteScaleSlider = spriteCard.add<ui::Slider>(32.0f, 320.0f);
    spriteScaleSlider.value = 160.0f;
    auto& spriteAlphaSlider = spriteCard.add<ui::Slider>(0.0f, 1.0f);
    spriteAlphaSlider.value = 0.9f;
#endif

    //! Rotation runs off its own clock rather than off the wall clock, so
    //! pausing the spin holds the objects where they are instead of letting
    //! them jump forward when it resumes.
    bool spinning = true;
    float spinTime = 0.0f;

#ifdef AURA_HAS_UI
    float spawnSpinTime = 0.0f; // Its own clock, independent of the scene's.
#endif

    // Seed view/projection before the first beginRenderPass, which is what
    // uploads them for the frame.
    r->setTransform(camera.buildUBO());

#ifdef AURA_PROFILE_DRAW_RECORDING
    //! Rolling accumulator for the drawMeshes() timing reported below.
    long long recordNanos = 0;
    int recordSamples = 0;
#endif

    constexpr float kMoveSpeed = 3.0f; // world units per second

    r->run([&]() {
        // Window resize
        if (wd->width != lastWindowWidth || wd->height != lastWindowHeight)
        {
            lastWindowWidth = wd->width;
            lastWindowHeight = wd->height;
            camera.setPerspective({.fovDeg = 60.0f,
                                   .aspect = static_cast<float>(lastWindowWidth) / static_cast<float>(lastWindowHeight),
                                   .nearZ  = 0.1f,
                                   .farZ   = 100.0f});
        }

        const float dt = static_cast<float>(windowManager->getWindowFlags()->deltaTime) / 1000.0f;

        /*
         * Null in a normal build (see Engine::debugMode()), so this costs one
         * predictable branch and needs no #ifdef around it. In an
         * AURA_ENABLE_DEBUG_MODE build it is what advances the benchmark's
         * clock, writes any interim report, and -- with debug.exit_on_complete
         * set -- ends a headless run once its frame target is reached.
         *
         * The frame samples themselves arrive through FrameProfiler, not here.
         */
        if (DebugMode* debug = engine.debugMode())
            debug->update(dt);

#ifdef AURA_HAS_UI
        spinning = sceneSpin.checked.get();
#endif

        if (spinning)
            spinTime += dt;

#ifdef AURA_HAS_UI
        if (spawnSpinCheck.checked.get())
            spawnSpinTime += dt * spawnSpinSpeedSlider.value.get();

        const bool spriteAnimate = spriteAnimateCheck.checked.get();
        if (spriteAnimate)
            spriteTime += dt * spriteSpeedSlider.value.get();
#endif

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

        if (glm::length(moveDir) > 0.0f)
            camera.setPosition(camera.position() + glm::normalize(moveDir) * kMoveSpeed * dt);

        /*
         * Audio, once per frame and after the camera has moved. The listener
         * follows the camera, which is what makes the orb's hum pan and fade as
         * you walk around it -- both the position and the orientation matter,
         * since panning is computed against where the listener is facing.
         *
         * update() is the bookkeeping half: the mixing itself runs on the audio
         * thread, driven by the device rather than by this loop.
         */
        audio->setListener({.position = camera.position(),
                            .forward  = camera.forward(),
                            .up       = glm::vec3(0.0f, 1.0f, 0.0f)});
        audio->update(dt);

#ifdef AURA_HAS_UI
        /*
         * The sprite is refreshed here -- before the render pass, and on its own
         * clock rather than the frame's.
         *
         * Both halves matter. Regenerating 96x96 pixels costs ~95us of sin() on
         * this machine, which on a scene that otherwise runs a 50us frame is
         * twice the entire frame; and updateTextureRegion() is a queue
         * submission, which belongs outside the render pass rather than in the
         * middle of one. Sixty updates a second is already more than the eye
         * resolves in a plasma animation, and it decouples the cost of the demo
         * from how fast the renderer happens to be going.
         */
        if (spriteAnimate)
        {
            constexpr float kSpriteInterval = 1.0f / 60.0f;

            spriteRefreshAccum += dt;
            if (spriteRefreshAccum >= kSpriteInterval)
            {
                //! Subtracted rather than zeroed, so the update rate does not
                //! drift with the frame rate.
                spriteRefreshAccum = std::fmod(spriteRefreshAccum, kSpriteInterval);

                regenerateSprite(spritePixels, kSpriteSize, spriteTime);
                r->updateTextureRegion(spriteTexture, 0, 0, kSpriteSize, kSpriteSize,
                                       spritePixels.data());
            }
        }
#endif

        r->beginRenderPass();

        for (size_t i = 0; i < scene.size(); ++i)
        {
            const SceneObject& object = scene[i];

            glm::mat4 model = glm::translate(glm::mat4(1.0f), object.position);

            if (object.spinSpeed != 0.0f)
                model = glm::rotate(model, spinTime * object.spinSpeed, glm::vec3(0.0f, 1.0f, 0.0f));

            model = glm::scale(model, object.scale);

            drawItems[i] = {object.mesh, object.material, model};
        }

        // view/projection are shared by the whole batch, so they are set once
        // here; drawMeshes() varies only the model matrix per item.
        r->setTransform(camera.buildUBO());

        // The whole scene in one submission. Handing the list over at once is
        // what lets the Vulkan backend record it across worker threads; the
        // equivalent per-object setTransform/bindMaterial/drawMesh loop is
        // still supported and draws exactly the same thing.
        //
        // Timed because this is the cost the threaded submission path exists
        // to reduce: pure CPU time spent recording draw commands. Frame rate
        // is the wrong signal for it -- a scene can be GPU-bound and show no
        // FPS change while this number falls sharply.
#ifdef AURA_PROFILE_DRAW_RECORDING
        const auto recordStart = std::chrono::steady_clock::now();
#endif
        r->drawMeshes(drawItems);
#ifdef AURA_PROFILE_DRAW_RECORDING
        recordNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now() - recordStart).count();

        if (++recordSamples == 240)
        {
            INK_INFO << "drawMeshes: " << (recordNanos / recordSamples / 1000.0)
                     << " us/frame avg over " << recordSamples << " frames ("
                     << drawItems.size() << " objects)";
            recordNanos = 0;
            recordSamples = 0;
        }
#endif

#ifdef AURA_HAS_UI
        // A second, independent submission for whatever has been spawned
        // through the Objects card -- kept apart from `scene`'s own
        // drawMeshes() call above so that call's cost (and the profiling
        // around it) keeps measuring exactly what it always has.
        if (!spawned.empty())
        {
            spawnedDrawItems.resize(spawned.size());
            for (size_t i = 0; i < spawned.size(); ++i)
            {
                const SceneObject& object = spawned[i];

                glm::mat4 model = glm::translate(glm::mat4(1.0f), object.position);

                if (object.spinSpeed != 0.0f)
                    model = glm::rotate(model, spawnSpinTime * object.spinSpeed, glm::vec3(0.0f, 1.0f, 0.0f));

                model = glm::scale(model, object.scale);

                spawnedDrawItems[i] = {object.mesh, object.material, model};
            }

            r->drawMeshes(spawnedDrawItems);
        }
#endif

        // The 2D pipeline supplies its own orthographic projection, so the
        // overlay needs nothing from the scene camera and leaves the scene's
        // transform untouched.
        overlay.drawFPS(10.0f, 10.0f);

#ifdef AURA_HAS_UI
        //! overlay.fps() rather than a second average of our own: drawFPS()
        //! above already smoothed it this frame, and reading it back is what
        //! keeps the corner and the card agreeing.
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f FPS", static_cast<double>(overlay.fps()));
        fpsLabel.text = fps;

        //! Drawn only while the sidebar is down, so it stops being clutter the
        //! moment it has done its job.
        if (!sidebarOpen)
            overlay.drawText("F1: tool sidebar", 10.0f, 10.0f + overlay.lineHeight());

        // -- 2D: the animated sprite, drawn independently of the UI ---------
        //! Regenerated above the render pass, not here; only the quad is
        //! submitted at this point.

        // The margin grows with the scale slider, so the corner nearest the
        // edge stays a fixed 16px inset at every size instead of the sprite
        // clipping off-screen once Scale is pushed past a fixed margin.
        {
            const float spriteScale = spriteScaleSlider.value.get();
            const glm::vec2 half{spriteScale * 0.5f, spriteScale * 0.5f};
            const float margin = spriteScale * 0.5f + 16.0f;
            const glm::vec2 anchor{static_cast<float>(lastWindowWidth) - margin,
                                   static_cast<float>(lastWindowHeight) - margin};
            const glm::vec4 spriteColor{hueToRgb(spriteHueSlider.value.get()),
                                        spriteAlphaSlider.value.get()};

            spriteVertices.clear();
            spriteIndices.clear();
            appendQuad(spriteVertices, spriteIndices, anchor - half, anchor + half, spriteColor);
            r->drawBatch2D(spriteVertices, spriteIndices, spriteTexture);
        }

        //! Layout, dispatch and the single-batch submission for the whole
        //! sidebar. Called every frame regardless of sidebarOpen: a hidden
        //! (Collapsed) sidebar occupies no space and records nothing, so this
        //! costs one dirty check when the tree has not changed.
        ui.render(dt);
#endif

        r->endRenderPass();
    });

    return 0;
}
