#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
#include "aura/UI/AuraUI.h"
#include "aura/UI/InputRouter.h"
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
 * to keep in sync than a few extra panels cost to read. See docs/12 and
 * docs/13 for what the widgets themselves do; this is still their reference
 * usage.
 *
 * All of it is additive: the WASD/mouse-look/touch camera, the audio demo and
 * the floor-plus-three-objects scene above are untouched. What follows spawns
 * *more* objects alongside them (offset behind the original scene so the two
 * do not overlap on spawn) and layers three more panels beside "Scene".
 */

constexpr size_t kMaxSpawnedObjects = 48;

//! One width and one row of x positions for all five panels, rather than five
//! hand-placed rectangles that drift apart the moment one of them is widened.
//! Only the first frame reads these -- a panel remembers where it was dragged
//! to -- so they are the opening layout, not a constraint.
constexpr float kPanelWidth = 252.0f;
constexpr float kPanelY = 52.0f;
constexpr float kPanelGap = 16.0f;

constexpr std::array<float, 5> kPanelX = {
    16.0f,
    16.0f + (kPanelWidth + kPanelGap),
    16.0f + (kPanelWidth + kPanelGap) * 2.0f,
    16.0f + (kPanelWidth + kPanelGap) * 3.0f,
    16.0f + (kPanelWidth + kPanelGap) * 4.0f,
};

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
 * them separable is what lets the Theme panel's drop-down swap one without
 * throwing the other away, and it is the same split Theme::applyPalette draws.
 */

/// A deep slate ground under one cyan accent. Sits cooler and darker than the
/// engine default so the panels read as glass laid over the midnight-blue
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

/// @p palette plus this app's shape language and spacing.
[[nodiscard]] ui::Theme shapedTheme(const ui::Palette& palette)
{
    ui::Theme theme(palette);

    //! Room to breathe. The engine's defaults are sized for a debug overlay
    //! squeezed into a corner; a tool panel that is the point of the screen can
    //! afford two more pixels a row, and reads as designed rather than dense.
    theme.metrics.rowHeight   = 24.0f;
    theme.metrics.itemSpacing = 6.0f;
    theme.metrics.padding     = 10.0f;
    theme.metrics.indent      = 14.0f;

    //! One shape language: controls are softly rounded, anything that slides is
    //! a pill. Panels, bars and separators stay square deliberately -- there is
    //! no antialiasing, so a large radius on a big translucent surface reads as
    //! a staircase rather than a curve.
    for (const ui::Part part : {ui::Part::Button, ui::Part::Checkbox, ui::Part::TextField,
                                ui::Part::Dropdown, ui::Part::Header, ui::Part::Selectable,
                                ui::Part::Tab, ui::Part::TreeNode})
    {
        theme[part].rounding = 5.0f;
    }

    theme[ui::Part::Slider].rounding = 1000.0f;
    theme[ui::Part::ScrollThumb].rounding = 1000.0f;

    //! A slider draws its caption *over* its own fill, so the fill cannot be
    //! the full-strength accent -- the text stops being readable on the filled
    //! half. Halfway to the surface, rather than a fixed colour: the surface is
    //! whatever the text is *not*, in any palette, so this keeps its distance
    //! from the caption in a light theme as well as a dark one.
    theme[ui::Part::Slider].accent = glm::mix(palette.accent, palette.surface, 0.5f);

    //! A hairline is what makes a button read as a raised thing rather than a
    //! flat patch, when button and panel are two greys from the same family.
    for (const ui::Part part : {ui::Part::Button, ui::Part::Dropdown, ui::Part::Tab})
    {
        theme[part].border = ui::ColorSet::flat(palette.border);
        theme[part].borderWidth = 1.0f;
    }

    theme[ui::Part::Button].padding = 10.0f;
    theme[ui::Part::TitleBar].height = 28.0f;

    //! A tree node's padding is also the gap after its expand glyph (see
    //! _leadingGlyph), and the engine default of 2px leaves ">Rendering" with
    //! the arrow welded to the word.
    theme[ui::Part::TreeNode].padding = 6.0f;

    return theme;
}

/*
 * Semantic one-off styles, as ui::Style patches. Each names only what it
 * changes, so every one of them still follows the theme for everything else --
 * which is why they keep tracking the Theme panel's Rounding slider while it is
 * being dragged, instead of freezing whatever the shape was when they were
 * written.
 */

/// A quieter label, for the small-caps headings that group a panel's rows.
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
/// a panel pulls the eye.
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

#ifdef AURA_HAS_UI
    /*
     * The engine's built-in immediate-mode UI. It draws through the same
     * backend-agnostic 2D pipeline the text overlay uses, so this same code
     * runs unchanged on Vulkan, OpenGL and the software rasterizer, and the
     * whole panel below costs a single draw call.
     *
     * Built before the gameplay bindings below because ui::InputRouter has to
     * exist first: it owns the wma input contexts those bindings are registered
     * into, which is what lets a menu suspend them wholesale.
     */
    ui::ContextDesc uiDesc;
    uiDesc.pixelHeight = 15.0f;
    ui::Context gui(r, uiDesc);

    //! Assigned whole rather than through applyPalette(): shapedTheme() sets
    //! metrics and per-Part shape as well as colour, and applyPalette() would
    //! discard exactly those.
    gui.theme = shapedTheme(auraPalette());

    /*
     * Mouse-look and a pointer-driven UI want opposite things from the cursor:
     * the first needs it captured and invisible, the second needs it free and
     * on screen. The router owns that swap -- along with suspending a mode's
     * bindings while another is current, which is what stops typing "east" into
     * a field from also firing the E binding and walking the camera.
     *
     * How many modes there are, and which key reaches which, is the
     * application's call rather than the engine's. This sandbox needs only the
     * built-in kGameplay and one tool screen, so it uses kMenu as-is; a game
     * with a pause screen and an inventory declares createMode() for each and
     * gives them a bindToggle() apiece.
     */
    constexpr auto kTools = ui::InputRouter::kMenu;

    ui::InputRouter input(*windowManager, gui);

    /*
     * F1 rather than Tab, which this used to use. Tab is the UI's own focus
     * traversal key, and a toggle bound to it fires for every widget that is
     * not a text field -- so tabbing between controls dismissed the panels
     * instead of advancing focus, leaving keyboard navigation unreachable in
     * the one demo built to show it off.
     */
    input.bindToggle(wma::KEY_F1, kTools);
    input.bindClose(wma::KEY_ESCAPE, kTools);

    //! Said out loud, and drawn on screen below, because a toggle key nobody
    //! can guess is a UI nobody finds: the obvious guess is Tab, which this
    //! deliberately is not.
    INK_INFO << "UI: F1 opens the tool panels, Escape closes them";

    //! gui.attachInput() is not called here: the router already did it, from
    //! inside the context of each mode that asked for a UI.

#ifdef __ANDROID__
    //! Touch is the only pointer there is and it is never captured, so there is
    //! no mode to swap out of -- the panels are simply always up.
    input.switchTo(kTools);
#endif

#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * A benchmark run never presses F1, so every capture taken so far measured
     * the panels-closed path twice and concluded the UI was free. Only the
     * debug build reads this -- a shipping Sandbox has no env backdoor.
     */
    if (const char* panels = std::getenv("AURA_SANDBOX_PANELS");
        panels != nullptr && *panels != '0')
    {
        input.switchTo(kTools);
        INK_INFO << "Sandbox: AURA_SANDBOX_PANELS set; tool panels open from frame one";
    }
#endif
#endif

#ifdef __ANDROID__
    // Twin-stick touch controls: left thumb walks, right thumb looks, both at
    // the same time. This needs wma's TouchListener rather than its
    // MouseListener -- SDL can synthesize mouse events from touch, but a mouse
    // has one cursor, so every finger collapses into a single stream and only
    // one stick could ever be active at a time.
    //
    // Relative mouse mode (setCursorEnabled(false)) is deliberately *not* used
    // here: there is no cursor to hide or unbound, and it would only interfere.
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
    const auto look = [&](const wma::WMAMousePosition& pos) {
        camYaw += static_cast<float>(pos.deltaX) * kMouseSensitivity;
        camPitch += static_cast<float>(pos.deltaY) * kMouseSensitivity;
        camera.setRotation(camYaw, camPitch);
    };

#ifdef AURA_HAS_UI
    //! The router suppresses this while a screen is up and owns the cursor
    //! capture that goes with it, so neither is tested here.
    input.bindLook(look);
#else
    wma::MouseListener& mouse = windowManager->getMouseListener();
    mouse.setCursorEnabled(false);
    mouse.setMoveAction(wma::MouseAction{wma::MouseAction::PositionCallback(look)});
#endif
#endif

    /*
     * Movement, bound through the router where the UI exists: it registers them
     * in kGameplay's input context alone, so W/A/S/D/Space are plain letters
     * while a text field has focus rather than also walking the camera, and a
     * key still held when a screen opens is dropped rather than left stuck.
     */
#ifdef AURA_HAS_UI
    const auto bindHeld = [&input](wma::Key key, bool& flag) { input.bindHeld(key, flag); };
    const auto bindPress = [&input](wma::Key key, std::function<void()> action) {
        input.bindPress(key, std::move(action));
    };
#else
    const auto bindHeld = [&keyboard](wma::Key key, bool& flag) {
        keyboard.addKeyAction(key, wma::KeyAction{
            [&flag]() { flag = true; },
            [&flag]() { flag = false; }
        });
    };
    const auto bindPress = [&keyboard](wma::Key key, std::function<void()> action) {
        keyboard.addKeyAction(key, wma::KeyAction{[action = std::move(action)]() { action(); }});
    };
#endif

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
    float lightIntensity = light.intensity;
    float lightAmbient = light.ambient;

    // -- Spawnable objects: same meshes/materials as the scene above, placed
    // -- on a spiral behind it so the two never overlap on spawn.
    std::vector<SceneObject> spawned;
    std::vector<IRenderer::DrawItem> spawnedDrawItems;

    bool spawnSpinEnabled = true;
    float spawnSpinSpeedScale = 1.0f;
    float spawnSpinTime = 0.0f; // Its own clock, independent of the scene's.

    // -- The animated sprite: allocated once, rewritten in full every frame
    // -- it animates. See regenerateSprite()'s comment for why that upload
    // -- shape is deliberate here rather than a bug.
    const TextureHandle spriteTexture = r->createDynamicTexture(kSpriteSize, kSpriteSize);
    std::vector<u8> spritePixels(static_cast<size_t>(kSpriteSize) * kSpriteSize * 4);

    bool spriteAnimate = true;
    float spriteSpeed = 1.0f;
    float spriteHue = 0.55f;
    float spriteScale = 160.0f;
    float spriteAlpha = 0.9f;
    float spriteTime = 0.0f;

    //! Time banked towards the next sprite refresh; see where it is spent.
    float spriteRefreshAccum = 0.0f;

    std::vector<gfx::Vertex2D> spriteVertices;
    std::vector<u32> spriteIndices;

    // -- Inspector demo state: a text field, a drop-down, a radio group and a
    // -- scrolling selection list -- exercises the widgets that needed the
    // -- keyboard/scroll/focus work behind them to be reachable at all.
    std::string sceneName = "sandbox";
    int shadingMode = 0;
    int detailLevel = 1;
    int selectedSpawned = -1;

    // -- Theme demo state. The theme is edited live, mid-frame, because it is
    // -- read as each widget is submitted rather than latched anywhere.
    int themeChoice = 0;
    float uiRounding = 3.0f;
    bool advancedUnlocked = false;
#endif

    //! Rotation runs off its own clock rather than off the wall clock, so
    //! pausing the spin holds the objects where they are instead of letting
    //! them jump forward when it resumes.
    bool spinning = true;
    float spinTime = 0.0f;

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

        if (spinning)
            spinTime += dt;

#ifdef AURA_HAS_UI
        if (spawnSpinEnabled)
            spawnSpinTime += dt * spawnSpinSpeedScale;
        if (spriteAnimate)
            spriteTime += dt * spriteSpeed;
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
        // through the "Objects" panel -- kept apart from `scene`'s own
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
        //! Drawn only while the panels are down, so it stops being clutter the
        //! moment it has done its job.
        if (!input.isMode(kTools))
            overlay.drawText("F1: tool panels", 10.0f, 10.0f + overlay.lineHeight());

        // -- 2D: the animated sprite, drawn independently of the UI ---------
        //! Regenerated above the render pass, not here; only the quad is
        //! submitted at this point.

        // The margin grows with the scale slider, so the corner nearest the
        // edge stays a fixed 16px inset at every size instead of the sprite
        // clipping off-screen once Scale is pushed past a fixed margin.
        {
            const glm::vec2 half{spriteScale * 0.5f, spriteScale * 0.5f};
            const float margin = spriteScale * 0.5f + 16.0f;
            const glm::vec2 anchor{static_cast<float>(lastWindowWidth) - margin,
                                   static_cast<float>(lastWindowHeight) - margin};
            const glm::vec4 spriteColor{hueToRgb(spriteHue), spriteAlpha};

            spriteVertices.clear();
            spriteIndices.clear();
            appendQuad(spriteVertices, spriteIndices, anchor - half, anchor + half, spriteColor);
            r->drawBatch2D(spriteVertices, spriteIndices, spriteTexture);
        }

        /*
         * Opens the UI's frame in every mode, not only while the panels are
         * up: that is what keeps gui.isCapturingMouse()/isCapturingKeyboard()
         * describing the frame that just happened, which is in turn what the
         * router's gameplay suppression reads. A frame that submits no panel
         * costs a buffer clear.
         */
        input.newFrame();

        if (input.isMode(kTools))
        {
            // Rebuilt from scratch every frame, which is what keeps it from
            // ever disagreeing with the state it edits: there is no widget
            // object holding a stale copy of `spinning` or of the light.
            if (gui.beginPanel("Scene", {kPanelX[0], kPanelY}, kPanelWidth))
            {
                gui.label("RENDERER", heading(gui.theme));
                gui.label(RendererChoiceToString(r->getBackendType()));

                //! overlay.fps() rather than a second average of our own:
                //! drawFPS() above already smoothed it this frame, and reading
                //! it back is what keeps the corner and the panel agreeing.
                char fps[32];
                std::snprintf(fps, sizeof(fps), "%.0f FPS", static_cast<double>(overlay.fps()));
                gui.label(fps);

                gui.separator();

                gui.label("MOTION", heading(gui.theme));
                gui.checkbox("Spin objects", spinning);

                gui.separator();

                gui.label("LIGHTING", heading(gui.theme));

                // Light changes are pushed only when a slider actually moved,
                // so a still frame costs no uniform upload.
                if (gui.sliderFloat("Light", lightIntensity, 0.0f, 3.0f))
                {
                    light.intensity = lightIntensity;
                    r->setLight(light);
                }

                if (gui.sliderFloat("Ambient", lightAmbient, 0.0f, 1.0f))
                {
                    light.ambient = lightAmbient;
                    r->setLight(light);
                }

                gui.separator();

                if (gui.button("Reset camera", primary()))
                {
                    camYaw = -90.0f;
                    camPitch = -8.0f;
                    camera.setPosition({0.0f, 0.8f, 4.5f});
                    camera.setRotation(camYaw, camPitch);
                }

                gui.endPanel();
            }

            if (gui.beginPanel("Objects", {kPanelX[1], kPanelY}, kPanelWidth))
            {
                gui.label("POPULATION", heading(gui.theme));

                char count[48];
                std::snprintf(count, sizeof(count), "%zu / %zu spawned",
                              spawned.size(), kMaxSpawnedObjects);
                gui.label(count);

                gui.separator();

                //! The two spawns share the row, so "add" reads as one choice
                //! of two rather than as two separate decisions stacked up.
                //! Sizing the first is what splits it: the second takes the
                //! rest of the row, and a widget cannot be narrowed after the
                //! fact. See Context::sameLine().
                gui.setNextItemWidth((kPanelWidth - gui.theme.metrics.padding * 2.0f -
                                      gui.theme.metrics.itemSpacing) * 0.5f);

                if (gui.button("+ Crate", primary()) && spawned.size() < kMaxSpawnedObjects)
                {
                    spawned.push_back({cubeMesh, crateMat, spawnPosition(spawned.size()),
                                       glm::vec3(1.0f), 0.6f});
                }

                gui.sameLine();

                if (gui.button("+ Orb", primary()) && spawned.size() < kMaxSpawnedObjects)
                {
                    spawned.push_back({sphereMesh, orbMat, spawnPosition(spawned.size()),
                                       glm::vec3(1.0f), -0.5f});
                }

                //! Disabled rather than hidden while there is nothing to
                //! remove: the panel keeps its height, so the rows below it
                //! don't jump every time the list empties.
                gui.beginDisabled(spawned.empty());
                if (gui.button("Remove last") && !spawned.empty())
                    spawned.pop_back();
                gui.endDisabled();

                gui.separator();

                gui.label("MOTION", heading(gui.theme));
                gui.checkbox("Spin", spawnSpinEnabled);
                gui.sliderFloat("Spin speed", spawnSpinSpeedScale, 0.0f, 3.0f);

                gui.endPanel();
            }

            if (gui.beginPanel("Sprite", {kPanelX[2], kPanelY}, kPanelWidth))
            {
                gui.label("TEXTURE", heading(gui.theme));
                gui.label("96x96 procedural");

                gui.separator();

                gui.label("ANIMATION", heading(gui.theme));
                gui.checkbox("Animate", spriteAnimate);
                gui.sliderFloat("Speed", spriteSpeed, 0.0f, 4.0f);

                gui.separator();

                gui.label("APPEARANCE", heading(gui.theme));
                gui.sliderFloat("Hue", spriteHue, 0.0f, 1.0f);
                gui.sliderFloat("Scale", spriteScale, 32.0f, 320.0f);
                gui.sliderFloat("Alpha", spriteAlpha, 0.0f, 1.0f);

                gui.endPanel();
            }

            /*
             * Exercises the input and widget work built for text/scroll/focus:
             * a text field driven by the platform's committed-text stream, a
             * fixed-height scrolling list, and the widgets that needed focus
             * to be reachable without a mouse. Tab and Shift+Tab walk every
             * control on screen.
             */
            if (gui.beginPanel("Inspector", {kPanelX[3], kPanelY}, kPanelWidth))
            {
                gui.label("PROPERTIES", heading(gui.theme));
                gui.inputText("Name", sceneName);
                gui.inputFloat("Spin", spawnSpinSpeedScale);

                static constexpr std::string_view kModes[] = {"Lit", "Unlit", "Wireframe"};
                gui.dropdown("Shading", shadingMode, kModes);

                gui.separator();

                if (gui.beginTabBar("InspectorTabs"))
                {
                    if (gui.tabItem("Scene"))
                    {
                        gui.radioButton("Low", detailLevel, 0);
                        gui.radioButton("Medium", detailLevel, 1);
                        gui.radioButton("High", detailLevel, 2);
                    }

                    if (gui.tabItem("Objects"))
                    {
                        //! Fixed height, so a long list scrolls rather than
                        //! growing the panel past the bottom of the window.
                        if (gui.beginScroll("SpawnedList", 120.0f))
                        {
                            for (size_t i = 0; i < spawned.size(); ++i)
                            {
                                char row[32];
                                std::snprintf(row, sizeof(row), "Object %zu", i);
                                if (gui.selectable(row, selectedSpawned == static_cast<int>(i)))
                                    selectedSpawned = static_cast<int>(i);
                            }
                            gui.endScroll();
                        }
                    }

                    gui.endTabBar();
                }

                if (gui.collapsingHeader("Advanced", false))
                {
                    if (gui.treeNode("Rendering", true))
                    {
                        gui.checkbox("Spin enabled", spawnSpinEnabled);
                        gui.treePop();
                    }
                }

                gui.separator();

                //! Half the content width, so Apply and Reset split the row
                //! evenly instead of Apply taking a fixed 110px that stops
                //! matching the moment the panel or the padding changes.
                gui.setNextItemWidth((kPanelWidth - gui.theme.metrics.padding * 2.0f -
                                      gui.theme.metrics.itemSpacing) * 0.5f);

                gui.button("Apply", primary());
                gui.tooltip("Nothing to apply -- this is a layout demo");
                gui.sameLine();
                if (gui.button("Reset"))
                {
                    spawnSpinSpeedScale = 1.0f;
                    detailLevel = 1;
                    shadingMode = 0;
                }

                gui.endPanel();
            }

            /*
             * Styling, all four scopes of it. Nothing here reaches into a
             * widget: a Part's entry in the theme is the whole description of
             * how that component looks, so the same button() call draws a
             * pill, a slab or a red danger button depending only on the table
             * it reads.
             */
            if (gui.beginPanel("Theme", {kPanelX[4], kPanelY}, kPanelWidth))
            {
                static constexpr std::string_view kThemes[] = {"Aura", "Dark", "Light"};

                gui.label("PALETTE", heading(gui.theme));

                if (gui.dropdown("Palette", themeChoice, kThemes))
                {
                    //! Rebuilt through shapedTheme() rather than
                    //! applyPalette(), so swapping the colours keeps this app's
                    //! spacing and shape instead of falling back to the
                    //! engine's. Colours and shape are separate decisions and
                    //! the drop-down only makes one of them.
                    const ui::Palette palette = themeChoice == 0   ? auraPalette()
                                                : themeChoice == 1 ? ui::Palette::dark()
                                                                   : ui::Palette::light();

                    gui.theme = shapedTheme(palette);
                    uiRounding = gui.theme[ui::Part::Button].rounding;
                }

                //! One slider, every control that shares a shape: restyling
                //! them together is what keeps a theme coherent rather than a
                //! pile of unrelated radii. The slider and scroll thumb are
                //! left out on purpose -- they are pills, and a pill is a
                //! decision, not a radius on a scale.
                if (gui.sliderFloat("Rounding", uiRounding, 0.0f, 12.0f))
                {
                    for (const ui::Part part : {ui::Part::Button, ui::Part::Checkbox,
                                                ui::Part::TextField, ui::Part::Dropdown,
                                                ui::Part::Tab, ui::Part::Header,
                                                ui::Part::Selectable, ui::Part::TreeNode})
                    {
                        gui.theme[part].rounding = uiRounding;
                    }
                }

                gui.separator();

                gui.label("DANGER ZONE", heading(gui.theme));

                //! One instance, styled for itself. The patch names only the
                //! colours; rounding, padding and height still come from
                //! Part::Button, so the Rounding slider above still reaches it.
                gui.beginDisabled(spawned.empty());
                if (gui.button("Remove every object", danger()))
                    spawned.clear();
                gui.endDisabled();

                gui.separator();

                gui.label("GATED", heading(gui.theme));
                gui.checkbox("Unlock advanced", advancedUnlocked);

                //! Greyed out rather than hidden: the panel keeps its shape as
                //! the option becomes available.
                gui.beginDisabled(!advancedUnlocked);
                gui.sliderFloat("Spin speed", spawnSpinSpeedScale, 0.0f, 3.0f);
                gui.button("Recalculate");
                gui.endDisabled();

                gui.endPanel();
            }
        }

        //! Outside the screen check, so the frame the panels are dismissed on
        //! still gets its (now empty) batch closed out. Costs nothing when
        //! nothing was submitted, and is a single draw call when it was.
        gui.render();
#endif

        r->endRenderPass();
    });

    return 0;
}
