// gfx::Canvas geometry: shapes, rejected input, index rebasing and world-line clipping.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#include "aura/Renderer/Canvas.h"

#include "TestUtils.h"

using namespace aura3d;

namespace
{

constexpr glm::vec4 kRed{1, 0, 0, 1};

[[nodiscard]] bool near(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return std::abs(a - b) <= tolerance;
}

[[nodiscard]] bool indicesInRange(const gfx::Canvas &canvas)
{
    return std::ranges::all_of(canvas.indices(),
                               [&](u32 i)
                               {
                                   return i < canvas.vertices().size();
                               });
}

[[nodiscard]] bool allFinite(const gfx::Canvas &canvas)
{
    return std::ranges::all_of(canvas.vertices(),
                               [](const gfx::BatchVertex &v)
                               {
                                   return std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.pos.z);
                               });
}

[[nodiscard]] glm::vec2 xRange(const gfx::Canvas &canvas)
{
    constexpr f32 kInf = std::numeric_limits<f32>::infinity();
    glm::vec2 range{kInf, -kInf};
    for (const gfx::BatchVertex &v : canvas.vertices())
        range = {std::min(range.x, v.pos.x), std::max(range.y, v.pos.x)};
    return range;
}

void test_rect()
{
    gfx::Canvas canvas;
    canvas.rect({10, 20}, {30, 40}, kRed);
    const auto v = canvas.vertices();
    const auto i = canvas.indices();
    AURA_CHECK(v.size() == 4 && i.size() == 6, "rect: four vertices, two triangles");
    AURA_CHECK(v[0].pos == glm::vec3(10, 20, 0) && v[2].pos == glm::vec3(40, 60, 0), "rect: corners at origin and end");
    AURA_CHECK(std::ranges::all_of(v,
                                   [](const gfx::BatchVertex &x)
                                   {
                                       return x.color == kRed;
                                   }),
               "rect: color on every vertex");
    AURA_CHECK(indicesInRange(canvas), "rect: indices in range");
}

void test_line_quad()
{
    gfx::Canvas canvas;
    canvas.line({0, 0}, {10, 0}, kRed, 2.0f);
    const auto v = canvas.vertices();
    AURA_CHECK(v.size() == 4 && canvas.indices().size() == 6, "line: one quad");
    AURA_CHECK(v[0].pos == glm::vec3(0, -1, 0) && v[1].pos == glm::vec3(10, -1, 0) && v[2].pos == glm::vec3(10, 1, 0) &&
                   v[3].pos == glm::vec3(0, 1, 0),
               "line: flat ends, half the width either side");

    gfx::Canvas diagonal;
    diagonal.line({0, 0}, {3, 4}, kRed, 10.0f);
    const auto d = diagonal.vertices();
    AURA_CHECK(near(glm::distance(glm::vec2(d[0].pos), glm::vec2(d[3].pos)), 10.0f), "line: width holds at any angle");
}

void test_append_rebases()
{
    gfx::Canvas canvas;
    canvas.rect({0, 0}, {1, 1}, kRed);
    const std::array<gfx::BatchVertex, 3> extra{{{{0, 0, 0}, {}, kRed}, {{1, 0, 0}, {}, kRed}, {{0, 1, 0}, {}, kRed}}};
    const std::array<u32, 3> extraIndices{0, 1, 2};
    canvas.append(extra, extraIndices);
    const auto i = canvas.indices();
    AURA_CHECK(canvas.vertices().size() == 7 && i.size() == 9, "append: geometry follows the existing");
    AURA_CHECK(i[6] == 4 && i[7] == 5 && i[8] == 6, "append: indices rebased past the existing vertices");

    canvas.append({}, {});
    AURA_CHECK(canvas.indices().size() == 9, "append: empty spans are a no-op");
}

void test_clear()
{
    gfx::Canvas canvas;
    canvas.rect({0, 0}, {1, 1}, kRed);
    canvas.clear();
    AURA_CHECK(canvas.empty() && canvas.vertices().empty(), "clear: empty again");
    canvas.rect({0, 0}, {1, 1}, kRed);
    AURA_CHECK(canvas.indices()[0] == 0, "clear: indices restart at zero");
}

void test_world_line_zero_to_one()
{
    const gfx::CanvasView view{glm::mat4{1.0f}, {100, 100}, true};
    gfx::Canvas canvas;
    canvas.line(view, {-0.5f, 0, 0.5f}, {0.5f, 0, 0.5f}, kRed, 2.0f);
    const auto v = canvas.vertices();
    AURA_CHECK(v.size() == 4, "world line: one quad");
    AURA_CHECK(near(xRange(canvas).x, 25) && near(xRange(canvas).y, 75), "world line: x maps NDC to pixels");
    AURA_CHECK(near(v[0].pos.y, 49) && near(v[3].pos.y, 51), "world line: width in pixels around y = 50");
    AURA_CHECK(std::ranges::all_of(v,
                                   [](const gfx::BatchVertex &x)
                                   {
                                       return near(x.pos.z, 0.5f);
                                   }),
               "world line: [0, 1] depth passes through");

    //! z = -1 .. 1 crosses the near plane z = 0 halfway, at x = 0.
    gfx::Canvas clipped;
    clipped.line(view, {-1, 0, -1}, {1, 0, 1}, kRed);
    AURA_CHECK(near(xRange(clipped).x, 50) && near(xRange(clipped).y, 100), "world line: clipped at the near plane");
    AURA_CHECK(near(clipped.vertices()[0].pos.z, 0.0f), "world line: clipped end sits at depth 0");

    gfx::Canvas behind;
    behind.line(view, {-1, 0, -1}, {1, 0, -0.5f}, kRed);
    AURA_CHECK(behind.empty(), "world line: wholly behind the near plane adds nothing");
}

void test_world_line_minus_one_to_one()
{
    const gfx::CanvasView view{glm::mat4{1.0f}, {100, 100}, false};
    gfx::Canvas canvas;
    canvas.line(view, {-0.5f, 0, 0}, {0.5f, 0, 0}, kRed);
    AURA_CHECK(near(canvas.vertices()[0].pos.z, 0.5f), "world line: [-1, 1] depth maps to [0, 1]");

    gfx::Canvas clipped;
    clipped.line(view, {-1, 0, -3}, {1, 0, 1}, kRed);
    AURA_CHECK(near(xRange(clipped).x, 50) && near(clipped.vertices()[0].pos.z, 0.0f),
               "world line: [-1, 1] near plane is z = -w");
}

void test_world_line_behind_camera()
{
    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    const glm::mat4 view = glm::lookAtRH(glm::vec3{0, 0, 0}, glm::vec3{0, 0, -1}, glm::vec3{0, 1, 0});
    const gfx::CanvasView canvasView{projection * view, {200, 200}, true};

    gfx::Canvas crossing;
    crossing.line(canvasView, {0, 0, -5}, {1, 0, 5}, kRed);
    AURA_CHECK(crossing.indices().size() == 6 && allFinite(crossing),
               "world line: through the camera plane stays finite");

    gfx::Canvas behind;
    behind.line(canvasView, {0, 0, 1}, {1, 0, 5}, kRed);
    AURA_CHECK(behind.empty(), "world line: behind the camera adds nothing");
}

} // namespace

int main()
{
    test_rect();
    test_line_quad();
    test_append_rebases();
    test_clear();
    test_world_line_zero_to_one();
    test_world_line_minus_one_to_one();
    test_world_line_behind_camera();
    AURA_TEST_MAIN_RETURN();
}
