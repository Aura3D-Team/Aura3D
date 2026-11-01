// Rays, bounds, frusta and Camera::rayFromScreen().
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "aura/Core/Camera/Camera.h"
#include "aura/Core/Collision.h"

#include "TestUtils.h"

using namespace aura3d;

namespace
{

[[nodiscard]] bool near(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return std::abs(a - b) <= tolerance;
}

[[nodiscard]] bool near(glm::vec3 a, glm::vec3 b, f32 tolerance = 1e-4f)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
}

void test_ray_box()
{
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    const auto hit = intersect(Ray{{0, 0, 5}, {0, 0, -1}}, box);
    AURA_CHECK(hit && near(*hit, 4.0f), "ray/box: front face at distance 4");
    AURA_CHECK(!intersect(Ray{{0, 3, 5}, {0, 0, -1}}, box), "ray/box: passing above misses");
    AURA_CHECK(!intersect(Ray{{0, 0, 5}, {0, 0, 1}}, box), "ray/box: pointing away misses");
    const auto inside = intersect(Ray{{0, 0, 0}, {1, 0, 0}}, box);
    AURA_CHECK(inside && *inside == 0.0f, "ray/box: from inside hits at 0");
    AURA_CHECK(intersect(Ray{{1, 0, 5}, {0, 0, -1}}, box).has_value(), "ray/box: axis-parallel along a face hits");
    AURA_CHECK(!intersect(Ray{{0, 0, 5}, {0, 0, -1}}, Aabb{}), "ray/box: empty box misses");
}

void test_ray_sphere_plane_triangle()
{
    const Sphere sphere{{0, 0, -10}, 2};
    const auto hit = intersect(Ray{{0, 0, 0}, {0, 0, -1}}, sphere);
    AURA_CHECK(hit && near(*hit, 8.0f), "ray/sphere: near surface at 8");
    AURA_CHECK(!intersect(Ray{{0, 3, 0}, {0, 0, -1}}, sphere), "ray/sphere: offset ray misses");
    AURA_CHECK(!intersect(Ray{{0, 0, 0}, {0, 0, 1}}, sphere), "ray/sphere: pointing away misses");

    const Plane ground{{0, 1, 0}, 0};
    const auto down = intersect(Ray{{2, 5, 3}, glm::normalize(glm::vec3{0, -1, 0})}, ground);
    AURA_CHECK(down && near(*down, 5.0f), "ray/plane: ground hit straight down");
    AURA_CHECK(!intersect(Ray{{0, 5, 0}, {1, 0, 0}}, ground), "ray/plane: parallel misses");

    const glm::vec3 a{-1, -1, -3}, b{1, -1, -3}, c{0, 1, -3};
    const auto tri = intersect(Ray{{0, 0, 0}, {0, 0, -1}}, a, b, c);
    AURA_CHECK(tri && near(*tri, 3.0f), "ray/triangle: hit at 3");
    AURA_CHECK(intersect(Ray{{0, 0, -6}, {0, 0, 1}}, a, b, c).has_value(), "ray/triangle: back face hits too");
    AURA_CHECK(!intersect(Ray{{2, 0, 0}, {0, 0, -1}}, a, b, c), "ray/triangle: outside the edges misses");
}

void test_overlaps_and_transform()
{
    AURA_CHECK(overlaps(Aabb{{0, 0, 0}, {1, 1, 1}}, Aabb{{1, 1, 1}, {2, 2, 2}}), "aabb/aabb: touching corners overlap");
    AURA_CHECK(!overlaps(Aabb{{0, 0, 0}, {1, 1, 1}}, Aabb{{1.1f, 0, 0}, {2, 1, 1}}), "aabb/aabb: apart");
    AURA_CHECK(overlaps(Sphere{{0, 0, 0}, 1}, Sphere{{1.5f, 0, 0}, 0.5f}), "sphere/sphere: touching overlap");
    AURA_CHECK(!overlaps(Sphere{{0, 0, 0}, 1}, Sphere{{3, 0, 0}, 1}), "sphere/sphere: apart");
    AURA_CHECK(overlaps(Sphere{{2, 0, 0}, 1.01f}, Aabb{{-1, -1, -1}, {1, 1, 1}}), "sphere/aabb: reaches the face");
    AURA_CHECK(!overlaps(Sphere{{2, 2, 0}, 1}, Aabb{{-1, -1, -1}, {1, 1, 1}}), "sphere/aabb: corner gap");

    const Aabb unit{{-1, -1, -1}, {1, 1, 1}};
    const glm::mat4 turned = glm::rotate(glm::translate(glm::mat4{1}, {5, 0, 0}), glm::radians(45.0f), {0, 0, 1});
    const Aabb moved = transformed(unit, turned);
    const f32 half = std::sqrt(2.0f);
    AURA_CHECK(near(moved.min, {5 - half, -half, -1}) && near(moved.max, {5 + half, half, 1}),
               "transformed: rotated box grows to the rotated corners");

    const std::array<gfx::Vertex3D, 2> vertices{gfx::Vertex3D{{1, 2, 3}}, gfx::Vertex3D{{-1, 0, 5}}};
    const Aabb bounds = boundsOf(vertices);
    AURA_CHECK(bounds.min == glm::vec3(-1, 0, 3) && bounds.max == glm::vec3(1, 2, 5), "boundsOf: spans the vertices");
}

void test_frustum()
{
    for (const bool zeroToOne : {true, false})
    {
        const glm::mat4 projection = zeroToOne ? glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f)
                                               : glm::perspectiveRH_NO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const glm::mat4 view = glm::lookAt(glm::vec3{0, 0, 0}, glm::vec3{0, 0, -1}, glm::vec3{0, 1, 0});
        const Frustum frustum = Frustum::fromMatrix(projection * view, zeroToOne);
        const char *space = zeroToOne ? " ([0, 1])" : " ([-1, 1])";
        AURA_CHECK(visible(frustum, Aabb{{-1, -1, -11}, {1, 1, -9}}),
                   std::string("frustum: box ahead visible") + space);
        AURA_CHECK(!visible(frustum, Aabb{{-1, -1, 9}, {1, 1, 11}}), std::string("frustum: box behind culled") + space);
        AURA_CHECK(!visible(frustum, Aabb{{-1, -1, -201}, {1, 1, -199}}),
                   std::string("frustum: box past far culled") + space);
        AURA_CHECK(!visible(frustum, Sphere{{50, 0, -10}, 1}), std::string("frustum: sphere far left culled") + space);
        AURA_CHECK(visible(frustum, Sphere{{0, 0, -0.05f}, 0.1f}),
                   std::string("frustum: sphere straddling near visible") + space);
    }
}

void test_ray_from_screen()
{
    for (const auto clip : {Camera::ClipSpace::Vulkan, Camera::ClipSpace::OpenGL})
    {
        Camera::setClipSpace(clip);
        Camera camera = Camera::perspective({.fovDeg = 60.0f, .aspect = 2.0f, .nearZ = 0.1f, .farZ = 100.0f});
        camera.setPosition({0, 0, 5});
        camera.lookAt({0, 0, 0});
        const glm::vec2 viewport{800, 400};
        const char *space = clip == Camera::ClipSpace::Vulkan ? " (Vulkan)" : " (OpenGL)";

        const Ray centre = camera.rayFromScreen(viewport * 0.5f, viewport);
        AURA_CHECK(near(centre.direction, {0, 0, -1}) && near(centre.origin, {0, 0, 4.9f}, 1e-3f),
                   std::string("rayFromScreen: centre ray starts on the near plane, looks ahead") + space);

        //! A pixel left of centre and above it aims left and up: screen y grows down.
        const Ray corner = camera.rayFromScreen({100, 50}, viewport);
        AURA_CHECK(corner.direction.x < 0.0f && corner.direction.y > 0.0f && near(glm::length(corner.direction), 1),
                   std::string("rayFromScreen: upper-left pixel aims up-left with unit length") + space);

        //! Round trip: the hit point projects back onto the pixel the ray came from.
        const f32 t = intersect(corner, Plane{{0, 0, 1}, 0}).value_or(-1.0f);
        const glm::vec4 clipPoint = camera.projectionMatrix() * camera.viewMatrix() * glm::vec4(corner.at(t), 1);
        const glm::vec2 ndc = glm::vec2(clipPoint) / clipPoint.w;
        const glm::vec2 pixel{(ndc.x + 1) * 0.5f * viewport.x, (1 - ndc.y) * 0.5f * viewport.y};
        AURA_CHECK(t >= 0.0f && near(pixel.x, 100, 1e-2f) && near(pixel.y, 50, 1e-2f),
                   std::string("rayFromScreen: hit projects back onto its pixel") + space);
    }
    Camera::setClipSpace(Camera::ClipSpace::OpenGL);
}

} // namespace

int main()
{
    test_ray_box();
    test_ray_sphere_plane_triangle();
    test_overlaps_and_transform();
    test_frustum();
    test_ray_from_screen();
    return aura3d::test::failureCount() == 0 ? 0 : 1;
}
