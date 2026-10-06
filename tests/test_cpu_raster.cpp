// CPU winding, coverage, interpolation and ordered depth/blend behavior.
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "aura/Core/Camera/Camera.h"
#include "aura/Core/JobSystem/JobSystem.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/Renderer/Software/CpuAura/CpuFrameBufferManager.h"
#ifdef AURA_HAS_CPU
#include "aura/Renderer/Software/CPURenderer.h"
#endif

#include "TestUtils.h"
#include "WindowTestUtils.h"

using namespace aura3d;
using aura3d::cpu::ScreenVertex;

namespace
{

constexpr float kWidth = 800.0f;
constexpr float kHeight = 600.0f;

/*
 * The position half of CPURenderer's projectVertex(): clip space through the
 * perspective divide and the viewport transform. Deliberately spelled out
 * rather than reused, so that a change to the renderer's viewport mapping
 * shows up here as a failing test instead of being silently tracked.
 */
[[nodiscard]] ScreenVertex projectPosition(const glm::vec3 &worldPos, const glm::mat4 &viewProj)
{
    ScreenVertex sv;

    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f)
    {
        sv.invW = -1.0f; //! Behind the camera.
        return sv;
    }

    const float invW = 1.0f / clip.w;
    const float ndcX = clip.x * invW;
    const float ndcY = clip.y * invW;

    sv.x = (ndcX + 1.0f) * 0.5f * kWidth;
    sv.y = (1.0f - (ndcY + 1.0f) * 0.5f) * kHeight; //! Y flipped: screen Y grows down.
    sv.invW = invW;
    return sv;
}

//! Geometric ground truth: the triangle's outward normal points at the eye.
[[nodiscard]] bool facesCamera(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &eye)
{
    const glm::vec3 normal = glm::cross(b - a, c - a);
    return glm::dot(normal, eye - a) > 0.0f;
}

/**
 * @brief Checks isFrontFacing() against face normals for every cube triangle
 *        seen from @p eye.
 *
 * @return Number of triangles the culler would keep.
 */
int checkCubeFromEye(const glm::vec3 &eye, const char *label)
{
    const gfx::Mesh3D cube = MeshLoader::createCube();

    //! The software backend runs in the OpenGL [-1,1] clip space (Engine only
    //! selects Vulkan's [0,1] for the Vulkan backend), and the Y flip the
    //! winding convention hinges on is a consequence of that choice.
    Camera::setClipSpace(Camera::ClipSpace::OpenGL);

    Camera camera = Camera::perspective({.fovDeg = 60.0f, .aspect = kWidth / kHeight, .nearZ = 0.1f, .farZ = 100.0f});
    camera.setPosition(eye);
    camera.lookAt(glm::vec3(0.0f));

    const glm::mat4 viewProj = camera.projectionMatrix() * camera.viewMatrix();

    int kept = 0;
    int disagreements = 0;
    int considered = 0;

    for (size_t i = 0; i + 2 < cube.indices.size(); i += 3)
    {
        const glm::vec3 &a = cube.vertices[cube.indices[i + 0]].pos;
        const glm::vec3 &b = cube.vertices[cube.indices[i + 1]].pos;
        const glm::vec3 &c = cube.vertices[cube.indices[i + 2]].pos;

        const ScreenVertex sv0 = projectPosition(a, viewProj);
        const ScreenVertex sv1 = projectPosition(b, viewProj);
        const ScreenVertex sv2 = projectPosition(c, viewProj);

        //! Near-plane rejection happens before culling in the real path too.
        if (sv0.invW < 0.0f || sv1.invW < 0.0f || sv2.invW < 0.0f)
            continue;

        //! A triangle seen exactly edge-on has no reliable winding; skip the
        //! degenerate case rather than assert on a coin flip.
        if (std::abs(cpu::signedArea2(sv0, sv1, sv2)) < 1e-3f)
            continue;

        ++considered;

        const bool expected = facesCamera(a, b, c, eye);
        const bool actual = cpu::isFrontFacing(sv0, sv1, sv2);

        if (expected != actual)
            ++disagreements;

        if (actual)
            ++kept;
    }

    AURA_CHECK(considered > 0, std::string("cube has visible triangles from ") + label);
    AURA_CHECK(disagreements == 0, std::string("isFrontFacing matches face normals from ") + label);

    return kept;
}

using Framebuffer = cpu::CpuFrameBufferManager;
using Mode = Framebuffer::RasterMode;

void queueQuad(Framebuffer &frame, f32 depth, glm::vec4 color, Mode mode, bool reverse = false)
{
    const ScreenVertex a{0, 0, depth, 1, {}, color}, b{16, 0, depth, 1, {}, color};
    const ScreenVertex c{16, 16, depth, 1, {}, color}, d{0, 16, depth, 1, {}, color};
    frame.queueTriangle(reverse ? cpu::ScreenTriangle{c, b, a} : cpu::ScreenTriangle{a, b, c}, nullptr, mode);
    frame.queueTriangle(reverse ? cpu::ScreenTriangle{d, c, a} : cpu::ScreenTriangle{a, c, d}, nullptr, mode);
}

// An independent reference for the GPU backends' sRGB targets: linear colors and blending, stored
// encoded. The rasterizer uses tables; hardware is allowed 0.6 of a code, so channels match within one.
u32 encodeByte(f32 linear)
{
    const f32 c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<u32>(std::lround(c * 255.0f));
}

f32 decodeByte(u32 byte)
{
    const f32 c = static_cast<f32>(byte) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

u32 stored(glm::vec4 linear)
{
    return (static_cast<u32>(std::lround(linear.a * 255.0f)) << 24) | (encodeByte(linear.r) << 16) |
           (encodeByte(linear.g) << 8) | encodeByte(linear.b);
}

//! What a straight-alpha batch of @p source leaves over the stored @p destination.
u32 over(u32 destination, glm::vec4 source)
{
    const glm::vec3 background{decodeByte((destination >> 16) & 255u), decodeByte((destination >> 8) & 255u),
                               decodeByte(destination & 255u)};
    const f32 keep = 1.0f - source.a;
    return stored({glm::vec3(source) * source.a + background * keep,
                   source.a + static_cast<f32>(destination >> 24) / 255.0f * keep});
}

bool sameColor(u32 actual, u32 expected)
{
    for (u32 shift : {0u, 8u, 16u, 24u})
        if (std::abs(static_cast<i32>((actual >> shift) & 255u) - static_cast<i32>((expected >> shift) & 255u)) > 1)
            return false;
    return true;
}

bool allPixels(const Framebuffer &frame, u32 color, f32 depth)
{
    for (i32 y = 0; y < 16; ++y)
        for (i32 x = 0; x < 16; ++x)
        {
            const cpu::Pixel pixel = frame.getPixel({x, y});
            if (!sameColor(pixel.rgb, color) || pixel.z != depth)
                return false;
        }
    return true;
}

void checkRasterization()
{
    test::FakeWindow window;
    JobSystem jobs{4};
    Framebuffer frame{window, {.width = 16, .height = 16}, jobs};
    for (bool reverse : {false, true})
    {
        frame.clear();
        queueQuad(frame, 0, {1, 0, 0, 0.5f}, Mode::Batch, reverse);
        frame.flush();
        AURA_CHECK(allPixels(frame, over(0, {1, 0, 0, 0.5f}), 1),
                   "shared diagonal blends exactly once in either winding, preserving framebuffer alpha");
    }

    frame.clear();
    queueQuad(frame, 0.5f, {1, 0, 0, 1}, Mode::Scene);
    queueQuad(frame, 0.75f, {0, 1, 0, 1}, Mode::Batch);
    queueQuad(frame, 0.5f, {0, 0, 1, 0.5f}, Mode::Batch);
    frame.flush();
    const u32 blueOverRed = over(stored({1, 0, 0, 1}), {0, 0, 1, 0.5f});
    AURA_CHECK(allPixels(frame, blueOverRed, 0.5f), "batches reject hidden pixels and blend at equal scene depth");
    queueQuad(frame, 0, {0, 1, 0, 0.5f}, Mode::Batch);
    frame.flush();
    AURA_CHECK(allPixels(frame, over(blueOverRed, {0, 1, 0, 0.5f}), 0.5f),
               "screen depth overlays the scene without modifying depth");

    frame.clear();
    queueQuad(frame, 0.1f, {1, 0, 0, 1}, Mode::Batch);
    queueQuad(frame, 0.8f, {0, 1, 0, 1}, Mode::Batch);
    frame.flush();
    AURA_CHECK(allPixels(frame, 0xFF00FF00u, 1), "batch call order wins even when the later batch is farther away");
    queueQuad(frame, 0.5f, {0, 0, 1, 1}, Mode::Scene);
    queueQuad(frame, 0, {1, 0, 0, 0.5f}, Mode::Batch);
    frame.flush();
    AURA_CHECK(allPixels(frame, over(stored({0, 0, 1, 1}), {1, 0, 0, 0.5f}), 0.5f),
               "scene and batch submissions retain their order across row bands");

    frame.clear();
    ScreenVertex a{0, 0, 0.5f, 1, {}, {1, 0, 0, 1}};
    ScreenVertex b{8, 0, 0.5f, 0.5f, {}, {0, 1, 0, 1}};
    ScreenVertex c{0, 8, 0.5f, 0.25f, {}, {0, 0, 1, 1}};
    for (Mode mode : {Mode::Scene, Mode::Batch})
    {
        frame.clear();
        frame.queueTriangle({a, b, c}, nullptr, mode);
        frame.flush();
        // At (2.5,2.5), screen weights are 3/8, 5/16, 5/16; reciprocal w sums to 39/64.
        const u32 expected = stored({24.0f / 39.0f, 10.0f / 39.0f, 5.0f / 39.0f, 1.0f});
        AURA_CHECK(sameColor(frame.getPixel({2, 2}).rgb, expected),
                   "scene and batch colors use perspective-correct weights");
    }

    frame.clear();
    a.x = std::numeric_limits<f32>::quiet_NaN();
    frame.queueTriangle({a, b, c}, nullptr, Mode::Batch);
    a.x = -1e20f;
    b.x = 1e20f;
    c.x = 0;
    a.y = b.y = c.y = 1e20f;
    frame.queueTriangle({a, b, c}, nullptr, Mode::Batch);
    frame.flush();
    AURA_CHECK(allPixels(frame, 0, 1), "non-finite and far-offscreen triangles leave the framebuffer unchanged");
}

void checkBandBoundaries()
{
    test::FakeWindow window;
    JobSystem jobs{4};
    Framebuffer frame{window, {.width = 16, .height = 16}, jobs};
    for (i32 height : {1, 3, 15, 16, 17, 31})
    {
        frame.resizeFramebuffer(16, height);
        frame.clear();
        for (i32 row = -2; row < height + 2; ++row)
        {
            const f32 y = static_cast<f32>(row);
            const glm::vec4 color = row % 2 ? glm::vec4{0, 1, 0, 1} : glm::vec4{1, 0, 0, 1};
            const ScreenVertex a{0, y, 0, 1, {}, color}, b{16, y, 0, 1, {}, color};
            const ScreenVertex c{16, y + 1, 0, 1, {}, color}, d{0, y + 1, 0, 1, {}, color};
            frame.queueTriangle({a, b, c}, nullptr, Mode::Batch);
            frame.queueTriangle({a, c, d}, nullptr, Mode::Batch);
        }
        frame.flush();
        bool correct = true;
        for (i32 y = 0; y < height; ++y)
            for (i32 x = 0; x < 16; ++x)
                correct &= frame.getPixel({x, y}).rgb == (y % 2 ? 0xFF00FF00u : 0xFFFF0000u);
        AURA_CHECK(correct, "resized and uneven row bands preserve thin strips at every boundary");
    }
}

#ifdef AURA_HAS_CPU
void checkSingularLighting()
{
    JobSystem jobs{1};
    cpu::CPURenderer renderer(wma::WindowDetails{.width = 16, .height = 16});
    renderer.setWindowFactory(
        [](auto, const auto &, auto)
        {
            return std::make_unique<test::FakeWindow>();
        });
    renderer.initialize(AuraSettings::get(), &jobs);
    renderer.bindVertexBuffer(renderer.createVertexBuffer({{{-1, -1, 0}, {}, {1, 1, 1, 1}, {0, 0, 1}},
                                                           {{1, -1, 0}, {}, {1, 1, 1, 1}, {0, 0, 1}},
                                                           {{-1, 1, 0}, {}, {1, 1, 1, 1}, {0, 0, 1}}}));
    renderer.bindIndexBuffer(renderer.createIndexBuffer(std::vector<u32>{0, 1, 2}));
    renderer.setLight({.direction = {0, 0, -1}, .intensity = 1, .ambient = 0});
    for (f32 scale : {0.f, 1.f, 2.f})
        for (bool indexed : {false, true})
        {
            gfx::TransformUBO transform{glm::mat4{1}, glm::mat4{1}, glm::mat4{1}};
            transform.model[2][2] = scale;
            renderer.setTransform(transform);
            renderer.beginRenderPass();
            if (indexed)
                renderer.drawIndexed(3);
            else
                renderer.draw(3);
            renderer.endRenderPass();
            AURA_CHECK(renderer.getFrameBufferManager()->getPixel({2, 12}).rgb == 0xFFFFFFFFu,
                       "indexed and direct draws keep finite lighting under nonuniform and zero scale");
        }
}
#endif

} // namespace

int main()
{
    /*
     * Three generic viewpoints. A cube seen from a corner shows three of its
     * six faces, so a correct culler keeps half the triangles -- the sharpest
     * signal available that the sign is right, since an inverted test keeps
     * exactly the other half and would still "keep about half".
     * That is why the normal-agreement check above, not the count, is the
     * real assertion; the count only guards against culling everything or
     * nothing.
     */
    const int keptCorner = checkCubeFromEye({3.0f, 3.0f, 3.0f}, "a corner");
    const int keptFront = checkCubeFromEye({0.0f, 0.0f, 4.0f}, "straight on");
    const int keptBelow = checkCubeFromEye({-2.5f, -3.0f, 2.5f}, "below-left");

    AURA_CHECK(keptCorner > 0 && keptCorner < 12, "corner view culls some but not all of the cube's 12 triangles");
    AURA_CHECK(keptFront > 0 && keptFront < 12, "front view culls some but not all of the cube's 12 triangles");
    AURA_CHECK(keptBelow > 0 && keptBelow < 12, "below-left view culls some but not all of the cube's 12 triangles");

    checkRasterization();
    checkBandBoundaries();
#ifdef AURA_HAS_CPU
    checkSingularLighting();
#endif

    AURA_TEST_MAIN_RETURN();
}
