#include <chrono>
#include <cmath>
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
#endif

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
     * Mouse-look and a pointer-driven UI want opposite things from the cursor:
     * the first needs it captured and invisible, the second needs it free and
     * on screen. Tab swaps between the two modes, which is also what makes the
     * cursor position the UI reads meaningful -- in relative mode there is no
     * cursor for it to hit-test against.
     */
#ifdef __ANDROID__
    //! Touch is the only pointer there is and it is never captured, so there is
    //! no mode to swap out of: the UI is simply always up.
    bool uiVisible = true;
#else
    bool uiVisible = false;
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
    mouse.setCursorEnabled(false);
    mouse.setMoveAction(wma::MouseAction{[&](const wma::WMAMousePosition& pos) {
        //! Motion belongs to the UI while it is up, not to the camera.
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
#endif

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

    // E fires a one-shot. Bound on press only (the release lambda is empty), so
    // holding the key does not retrigger it every frame.
    keyboard.addKeyAction(wma::KEY_E, wma::KeyAction{
        [&]() { (void)audio->play(blipClip, 0.8f); },
        []() {}
    });

    // M mutes and unmutes, which is also the quickest way to confirm the master
    // gain is reaching the mixer.
    keyboard.addKeyAction(wma::KEY_M, wma::KeyAction{
        [&]() { audio->setMasterVolume(audio->masterVolume() > 0.0f ? 0.0f : 1.0f); },
        []() {}
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
     * The engine's built-in immediate-mode UI. It draws through the same
     * backend-agnostic 2D pipeline the text overlay uses, so this same code
     * runs unchanged on Vulkan, OpenGL and the software rasteriser, and the
     * whole panel below costs a single draw call.
     */
    ui::ContextDesc uiDesc;
    uiDesc.pixelHeight = 15.0f;
    ui::Context gui(r, uiDesc);

    //! Binds the left button. The cursor is polled rather than bound, so the
    //! camera's move callback above keeps working untouched.
    gui.attachInput(*windowManager);

    float lightIntensity = light.intensity;
    float lightAmbient = light.ambient;
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

        // The 2D pipeline supplies its own orthographic projection, so the
        // overlay needs nothing from the scene camera and leaves the scene's
        // transform untouched.
        overlay.drawFPS(10.0f, 10.0f);

#ifdef AURA_HAS_UI
        if (uiVisible)
        {
            // Rebuilt from scratch every frame, which is what keeps it from
            // ever disagreeing with the state it edits: there is no widget
            // object holding a stale copy of `spinning` or of the light.
            gui.newFrame();

            if (gui.beginPanel("Scene", {16.0f, 48.0f}, 260.0f))
            {
                gui.label(RendererChoiceToString(r->getBackendType()));
                gui.separator();

                gui.checkbox("Spin objects", spinning);

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

                if (gui.button("Reset camera"))
                {
                    camYaw = -90.0f;
                    camPitch = -8.0f;
                    camera.setPosition({0.0f, 0.8f, 4.5f});
                    camera.setRotation(camYaw, camPitch);
                }

                gui.endPanel();
            }

            // Single draw call, whatever the panel contains.
            gui.render();
        }
#endif

        r->endRenderPass();
    });

    return 0;
}
