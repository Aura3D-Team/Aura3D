// OrgLogo — the organization's logo, generated and animated by Aura3D.
//
// A blue circle in flames, burning at the centre of a living star field:
//   * a black sky carrying a faint, unevenly distributed nebula shine, baked
//     dust stars and the outer half of the orb's glow,
//   * hundreds of stars that twinkle on their own clocks, drift with a parallax
//     across three depth layers, and occasionally flare into full shine,
//   * the occasional meteor crossing behind the logo,
//   * and the orb itself: a solid blue disc with a white-hot centre, wrapped in
//     turbulent flame tongues that are re-simulated from scratch every frame,
//     inside a slowly turning starburst of shine rays.
//
// Layering, and why each layer is drawn the way it is:
//   1. The sky is a screen-filling textured quad through the 3D path -- it is
//      opaque, it never changes, and it is what everything else composites on.
//   2. The stars and the orb go through IRenderer::drawBatch2D(), the unlit
//      overlay path: it blends, it takes window-pixel coordinates, and it turns
//      a whole layer into a single draw call. The 3D path does neither of the
//      first two.
//
// Nothing here reaches inside the engine: the logo is built entirely on the
// public IRenderer surface.

#include <chrono>
#include <cstdlib>
#include <vector>

#include "aura/Core/AuraCore.h"
#include "aura/Core/Camera/Camera.h"
#include "aura/Core/Engine.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Material.h"

#include "LogoAssets.h"
#include "Overlay.h"
#include "StarField.h"

using namespace aura3d;
using namespace orglogo;

namespace {

//! Chooses the sky. Fixed, so the logo is the same logo on every run.
constexpr std::uint32_t kSeed = 1337u;

//! Edge length of the flame texture. It is re-baked every frame, so this is
//! the single knob trading the orb's crispness against CPU time.
constexpr int kFlameResolution = 512;

/**
 * @class FrameClock
 * @brief Wall-clock delta between frames.
 *
 * The engine's run() loop hands the callback no timing of its own, and the
 * animation must not be tied to the frame rate: at 30 fps the flame has to
 * climb the same distance per second it does at 60.
 */
class FrameClock {
public:
    [[nodiscard]] float tick() noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        const float delta = std::chrono::duration<float>(now - _last).count();
        _last = now;

        //! A hitch -- a dragged window, the first frame after the startup bake
        //! -- must not teleport the animation forward.
        return delta < kMaxDelta ? delta : kMaxDelta;
    }

private:
    static constexpr float kMaxDelta = 1.0f / 15.0f;

    std::chrono::steady_clock::time_point _last = std::chrono::steady_clock::now();
};

/**
 * @brief Unit quad in the XY plane facing +Z, toward the camera.
 *
 * The winding is the engine's own createPlane() rotated onto XY, so Vulkan's
 * back-face culling keeps it.
 */
[[nodiscard]] gfx::Mesh3D makeScreenQuad()
{
    gfx::Mesh3D mesh;

    const std::array<glm::vec3, 4> corners{
        glm::vec3{-0.5f, -0.5f, 0.0f},
        glm::vec3{ 0.5f, -0.5f, 0.0f},
        glm::vec3{ 0.5f,  0.5f, 0.0f},
        glm::vec3{-0.5f,  0.5f, 0.0f},
    };
    const std::array<glm::vec2, 4> texCoords{
        glm::vec2{0.0f, 0.0f}, glm::vec2{1.0f, 0.0f},
        glm::vec2{1.0f, 1.0f}, glm::vec2{0.0f, 1.0f},
    };

    mesh.vertices.reserve(corners.size());
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        gfx::Vertex3D vertex{};
        vertex.pos = corners[i];
        vertex.texCoord = texCoords[i];
        vertex.color = glm::vec4(1.0f);
        vertex.normal = {0.0f, 0.0f, 1.0f};
        mesh.vertices.push_back(vertex);
    }

    mesh.indices = {0, 1, 2, 2, 3, 0};
    return mesh;
}

} // namespace

int main()
{
    Engine engine("settings.json");

    IRenderer* renderer = engine.getRenderer();
    if (!renderer)
    {
        INK_ERROR << "OrgLogo: the engine came up without a renderer";
        return EXIT_FAILURE;
    }

    const wma::WindowDetails& window = renderer->getWindowDetails();
    const SceneLayout layout = makeLayout(window.width, window.height);

    /*
     * Unlit. The built-in 3D shader computes albedo * (ambient + diffuse *
     * intensity); ambient 1 with intensity 0 leaves the baked sky at exactly
     * the colours it was baked with. The overlay path is unlit by definition,
     * so this only concerns the backdrop.
     */
    gfx::LightUBO light;
    light.direction = {0.0f, -1.0f, 0.0f};
    light.color = {1.0f, 1.0f, 1.0f, 1.0f};
    light.intensity = 0.0f;
    light.ambient = 1.0f;
    renderer->setLight(light);

    /*
     * The sky is baked at the framebuffer's own resolution rather than at some
     * round power of two. The software rasteriser samples textures
     * nearest-neighbour, so matching the resolution puts one texel under one
     * pixel -- no resampling, and no elliptical halo on a non-square window.
     */
    BackdropDesc backdropDesc;
    backdropDesc.width = window.width;
    backdropDesc.height = window.height;
    backdropDesc.unit = layout.unit;
    backdropDesc.haloRadius = haloRadiusPx(layout);
    backdropDesc.seed = kSeed;

    const Image backdrop = bakeBackdrop(backdropDesc, engine.jobs());
    const TextureHandle backdropTexture =
        renderer->createTextureFromPixels(backdrop.pixels.data(),
                                          static_cast<u32>(backdrop.width),
                                          static_cast<u32>(backdrop.height));

    Material backdropMaterial;
    backdropMaterial.albedo = backdropTexture;
    const MaterialHandle backdropMaterialHandle = renderer->createMaterial(backdropMaterial);
    const MeshHandle backdropMesh = renderer->createMesh(makeScreenQuad());

    StarField stars(*renderer, kSeed);

    /*
     * The flame's texture is allocated once and rewritten in place. Creating a
     * texture per frame would work exactly once per backend's descriptor
     * budget; createDynamicTexture()/updateTextureRegion() is the pair the
     * interface provides for contents that change.
     */
    FlameField flame(kFlameResolution, engine.jobs());
    const auto flameResolution = static_cast<u32>(flame.resolution());
    const TextureHandle flameTexture = renderer->createDynamicTexture(flameResolution,
                                                                     flameResolution);

    //! Orthographic camera for the backdrop; Camera::ortho handles the
    //! per-backend Y flip and depth convention.
    Camera camera = Camera::ortho({.left = -1.0f, .right = 1.0f,
                                   .bottom = -1.0f, .top = 1.0f,
                                   .nearZ = 0.1f, .farZ = 10.0f});
    camera.setPosition({0.0f, 0.0f, 2.0f});
    camera.lookAt({0.0f, 0.0f, 0.0f});

    //! Fill the [-1,1] view box: the unit quad scaled by 2.
    const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 1.0f));

    renderer->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    FrameClock clock;
    float elapsed = 0.0f;

    std::vector<gfx::Vertex2D> orbVertices;
    std::vector<u32> orbIndices;
    orbVertices.reserve(4);
    orbIndices.reserve(6);

    renderer->run([&]() {
        const float deltaTime = clock.tick();
        elapsed += deltaTime;

        //! Simulation first, outside the render pass: the flame bake is the
        //! frame's heaviest CPU work and has nothing to do with the pass.
        flame.bake(elapsed);
        renderer->updateTextureRegion(flameTexture, 0, 0, flameResolution, flameResolution,
                                      flame.pixels().data());
        stars.update(elapsed, deltaTime, flame.pulse());

        renderer->beginRenderPass();

        renderer->setTransform(camera.buildUBO(model));
        renderer->bindMaterial(backdropMaterialHandle);
        renderer->drawMesh(backdropMesh);

        stars.submit(*renderer, layout);

        //! The orb goes last, so its glow washes over the stars behind it and
        //! any meteor passes below rather than across it.
        orbVertices.clear();
        orbIndices.clear();
        appendQuad(orbVertices, orbIndices, layout.center,
                            {layout.orbHalfExtent, layout.orbHalfExtent}, 0.0f,
                            UvRect{}, glm::vec4(1.0f));
        renderer->drawBatch2D(orbVertices, orbIndices, flameTexture);

        renderer->endRenderPass();
    });

    return EXIT_SUCCESS;
}
