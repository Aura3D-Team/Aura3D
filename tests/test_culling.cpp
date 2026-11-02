// Bounds and frusta.
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "aura/Core/Culling.h"

#include "TestUtils.h"

using namespace aura3d;

namespace
{

[[nodiscard]] bool near(glm::vec3 a, glm::vec3 b, f32 tolerance = 1e-4f)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
}

void test_transform()
{
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
    }
}

} // namespace

int main()
{
    test_transform();
    test_frustum();
    return aura3d::test::failureCount() == 0 ? 0 : 1;
}
