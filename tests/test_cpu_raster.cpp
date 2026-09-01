/*
 * Software rasteriser conventions.
 *
 * The CPU backend culls back faces at the vertex stage, which is only correct
 * if its notion of "front" matches the GPU backends' -- those cull with
 * VK_FRONT_FACE_COUNTER_CLOCKWISE, so an asset that renders solid under Vulkan
 * must render solid here. Getting the sign backwards does not crash or warn;
 * it renders every closed mesh inside-out, showing its far faces through its
 * near ones. That is exactly the kind of silent, convention-level mistake worth
 * pinning down in a test rather than in a comment.
 *
 * Ground truth here is geometric and independent of the rasteriser: a triangle
 * faces the camera when its outward face normal points back towards the eye.
 * The test asserts isFrontFacing() agrees with that on every triangle of a
 * cube, from several viewpoints.
 */

#include <vector>

#include <glm/glm.hpp>

#include "aura/Core/Camera/Camera.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/Renderer/Software/CpuAura/CpuFrameBufferManager.h"

#include "TestUtils.h"

using namespace aura3d;
using aura3d::cpu::ScreenVertex;

namespace {

constexpr float kWidth = 800.0f;
constexpr float kHeight = 600.0f;

/*
 * The position half of CPURenderer's projectVertex(): clip space through the
 * perspective divide and the viewport transform. Deliberately spelled out
 * rather than reused, so that a change to the renderer's viewport mapping
 * shows up here as a failing test instead of being silently tracked.
 */
[[nodiscard]] ScreenVertex projectPosition(const glm::vec3& worldPos, const glm::mat4& viewProj)
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
[[nodiscard]] bool facesCamera(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                               const glm::vec3& eye)
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
int checkCubeFromEye(const glm::vec3& eye, const char* label)
{
    const gfx::Mesh3D cube = MeshLoader::createCube();

    //! The software backend runs in the OpenGL [-1,1] clip space (Engine only
    //! selects Vulkan's [0,1] for the Vulkan backend), and the Y flip the
    //! winding convention hinges on is a consequence of that choice.
    Camera::setClipSpace(Camera::ClipSpace::OpenGL);

    Camera camera = Camera::perspective({.fovDeg = 60.0f,
                                         .aspect = kWidth / kHeight,
                                         .nearZ = 0.1f,
                                         .farZ = 100.0f});
    camera.setPosition(eye);
    camera.lookAt(glm::vec3(0.0f));

    const glm::mat4 viewProj = camera.projectionMatrix() * camera.viewMatrix();

    int kept = 0;
    int disagreements = 0;
    int considered = 0;

    for (size_t i = 0; i + 2 < cube.indices.size(); i += 3)
    {
        const glm::vec3& a = cube.vertices[cube.indices[i + 0]].pos;
        const glm::vec3& b = cube.vertices[cube.indices[i + 1]].pos;
        const glm::vec3& c = cube.vertices[cube.indices[i + 2]].pos;

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
    AURA_CHECK(disagreements == 0,
               std::string("isFrontFacing matches face normals from ") + label);

    return kept;
}

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

    AURA_CHECK(keptCorner > 0 && keptCorner < 12,
               "corner view culls some but not all of the cube's 12 triangles");
    AURA_CHECK(keptFront > 0 && keptFront < 12,
               "front view culls some but not all of the cube's 12 triangles");
    AURA_CHECK(keptBelow > 0 && keptBelow < 12,
               "below-left view culls some but not all of the cube's 12 triangles");

    AURA_TEST_MAIN_RETURN();
}
