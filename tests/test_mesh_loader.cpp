#include "aura/Core/MeshLoader/MeshLoader.h"

#include <cmath>
#include <string>

#include "TestUtils.h"

using namespace aura3d;

namespace {

constexpr float kEpsilon = 1e-4f;

std::string assetPath(const std::string& name)
{
    return std::string(AURA_TEST_ASSETS_DIR) + "/" + name;
}

bool isUnitLength(const glm::vec3& v)
{
    return std::fabs(glm::length(v) - 1.0f) < kEpsilon;
}

bool nearlyEqual(const glm::vec3& a, const glm::vec3& b)
{
    return glm::length(a - b) < kEpsilon;
}

void test_create_cube()
{
    const gfx::Mesh3D cube = MeshLoader::createCube();

    AURA_CHECK(!cube.empty(), "createCube: mesh is non-empty");
    AURA_CHECK(cube.vertices.size() == 24, "createCube: 24 vertices (4 per face x 6 faces)");
    AURA_CHECK(cube.indices.size() == 36, "createCube: 36 indices (6 per face x 6 faces)");

    bool allUnit = true;
    for (const auto& v : cube.vertices) {
        if (!isUnitLength(v.normal)) allUnit = false;
    }
    AURA_CHECK(allUnit, "createCube: every vertex normal is unit length");

    bool allIndicesInRange = true;
    for (u32 index : cube.indices) {
        if (index >= cube.vertices.size()) allIndicesInRange = false;
    }
    AURA_CHECK(allIndicesInRange, "createCube: every index references a real vertex");

    // Every face is axis-aligned, so every normal must be exactly one of the
    // six signed unit axes -- nothing oblique should ever come out of here.
    bool allAxisAligned = true;
    for (const auto& v : cube.vertices) {
        const glm::vec3& n = v.normal;
        const bool axisAligned =
            nearlyEqual(n, {1, 0, 0}) || nearlyEqual(n, {-1, 0, 0}) ||
            nearlyEqual(n, {0, 1, 0}) || nearlyEqual(n, {0, -1, 0}) ||
            nearlyEqual(n, {0, 0, 1}) || nearlyEqual(n, {0, 0, -1});
        if (!axisAligned) allAxisAligned = false;
    }
    AURA_CHECK(allAxisAligned, "createCube: every normal is a signed unit axis");
}

void test_create_plane()
{
    const gfx::Mesh3D plane = MeshLoader::createPlane();

    AURA_CHECK(plane.vertices.size() == 4, "createPlane: 4 vertices");
    AURA_CHECK(plane.indices.size() == 6, "createPlane: 6 indices (two triangles)");

    bool allUp = true;
    for (const auto& v : plane.vertices) {
        if (!nearlyEqual(v.normal, {0.0f, 1.0f, 0.0f})) allUp = false;
    }
    AURA_CHECK(allUp, "createPlane: every normal points straight up (+Y)");
}

void test_create_sphere()
{
    for (int subdivisions : {1, 2, 4}) {
        const gfx::Mesh3D sphere = MeshLoader::createSphere(subdivisions);
        const std::string tag = " (subdivisions=" + std::to_string(subdivisions) + ")";

        AURA_CHECK(!sphere.empty(), "createSphere: mesh is non-empty" + tag);

        bool allUnit = true;
        bool allOutward = true;
        for (const auto& v : sphere.vertices) {
            if (!isUnitLength(v.normal)) allUnit = false;

            // The sphere is centered on the origin, so at every point the
            // normal must point exactly along that point's own position.
            if (!nearlyEqual(glm::normalize(v.pos), v.normal)) allOutward = false;
        }
        AURA_CHECK(allUnit, "createSphere: every normal is unit length" + tag);
        AURA_CHECK(allOutward, "createSphere: every normal points outward from the center" + tag);

        bool allIndicesInRange = true;
        for (u32 index : sphere.indices) {
            if (index >= sphere.vertices.size()) allIndicesInRange = false;
        }
        AURA_CHECK(allIndicesInRange, "createSphere: every index references a real vertex" + tag);
    }
}

void test_load_obj_valid()
{
    const gfx::Mesh3D mesh = MeshLoader::loadOBJ(assetPath("valid_mesh.obj"));

    AURA_CHECK(!mesh.empty(), "loadOBJ: valid_mesh.obj produces a non-empty mesh");
    AURA_CHECK(mesh.vertices.size() == 3, "loadOBJ: valid_mesh.obj has 3 vertices");
    AURA_CHECK(mesh.indices.size() == 3, "loadOBJ: valid_mesh.obj has 3 indices (one triangle)");

    // The file carries no 'vn' records, so loadOBJ must have generated them
    // via its flat-normal fallback (see MeshLoader.cpp's generateNormals()).
    bool allUnit = true;
    bool allFacingForward = true;
    for (const auto& v : mesh.vertices) {
        if (!isUnitLength(v.normal)) allUnit = false;
        if (!nearlyEqual(v.normal, {0.0f, 0.0f, 1.0f})) allFacingForward = false;
    }
    AURA_CHECK(allUnit, "loadOBJ: generated normals are unit length");
    AURA_CHECK(allFacingForward, "loadOBJ: generated normal matches the triangle's winding (+Z)");
}

void test_load_obj_fallback()
{
    const gfx::Mesh3D fallbackCube = MeshLoader::createCube();

    // A missing file: tinyobj can't even open it.
    const gfx::Mesh3D missing = MeshLoader::loadOBJ(assetPath("does_not_exist.obj"));
    AURA_CHECK(missing.vertices.size() == fallbackCube.vertices.size() &&
              missing.indices.size() == fallbackCube.indices.size(),
              "loadOBJ: a missing file falls back to createCube()");

    // A file that parses fine but carries no geometry at all.
    const gfx::Mesh3D corrupt = MeshLoader::loadOBJ(assetPath("corrupt_mesh.obj"));
    AURA_CHECK(corrupt.vertices.size() == fallbackCube.vertices.size() &&
              corrupt.indices.size() == fallbackCube.indices.size(),
              "loadOBJ: a geometry-free file falls back to createCube()");
}

} // namespace

int main()
{
    test_create_cube();
    test_create_plane();
    test_create_sphere();
    test_load_obj_valid();
    test_load_obj_fallback();
    AURA_TEST_MAIN_RETURN();
}
