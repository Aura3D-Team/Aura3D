// Axiom -- worked examples from *Mathematics for Machine Learning*
// (Deisenroth, Faisal & Ong, Cambridge University Press 2020), drawn with
// nothing but the public Aura3D API (IRenderer, TextOverlay, Engine) and glm.
//
// Every lesson below is a self-contained function: it takes only a renderer
// and a text overlay, computes its numbers with glm's own vector/matrix
// functions (dot, cross, determinant, inverse, mix, ...), and draws them with
// a handful of tiny quad/line/circle helpers built on
// IRenderer::drawBatch2D() -- the same primitive Sandbox and OrgLogo draw
// their own 2D overlays with. No parallel math library, no custom rendering
// framework: just the engine and the book.
//
// Usage: exactly one lesson call is active in the render loop's "pick ONE
// lesson" block near the bottom of main(). Comment it out and uncomment
// another, rebuild, and that lesson replaces it on screen. Citations in each
// lesson's doc comment give the exact page so the book can sit open beside it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "aura/Core/AuraCore.h"
#include "aura/Core/Engine.h"
#include "aura/Core/TextOverlay/TextOverlay.h"
#include "aura/Renderer/IRenderer.h"

using namespace aura3d;

namespace {

// ===========================================================================
// Palette -- a handful of named colours, reused so every lesson reads x, y
// and "the answer" the same way.
// ===========================================================================

constexpr glm::vec4 kBackground{0.055f, 0.063f, 0.090f, 1.0f};
constexpr glm::vec4 kSurface{0.10f, 0.11f, 0.15f, 0.92f};
constexpr glm::vec4 kGridLine{1.0f, 1.0f, 1.0f, 0.07f};
constexpr glm::vec4 kAxisLine{0.60f, 0.66f, 0.76f, 0.75f};
constexpr glm::vec4 kText{0.90f, 0.92f, 0.96f, 1.0f};
constexpr glm::vec4 kTextMuted{0.58f, 0.63f, 0.72f, 1.0f};
constexpr glm::vec4 kPrimary{0.35f, 0.72f, 1.00f, 1.0f};   // the first object: x, A, b1.
constexpr glm::vec4 kSecondary{1.00f, 0.62f, 0.24f, 1.0f}; // the second object: y, B, b2.
constexpr glm::vec4 kAccent{0.85f, 0.45f, 0.95f, 1.0f};    // the derived answer.
constexpr glm::vec4 kOk{0.30f, 0.85f, 0.60f, 1.0f};        // "this holds".
constexpr glm::vec4 kWarn{1.00f, 0.38f, 0.42f, 1.0f};      // "this fails / is singular".

// ===========================================================================
// Tiny 2D drawing helpers -- all they do is append triangles to a vertex/
// index pair for a single IRenderer::drawBatch2D() call. Same convention
// Sandbox's own appendQuad() uses: top-left, top-right, bottom-right,
// bottom-left, so both triangles share the quad's diagonal.
// ===========================================================================

using Vertices = std::vector<gfx::Vertex2D>;
using Indices = std::vector<u32>;

void appendQuad(Vertices& v, Indices& idx, glm::vec2 min, glm::vec2 max, const glm::vec4& color)
{
    const auto base = static_cast<u32>(v.size());
    v.push_back({{min.x, min.y}, {0.0f, 0.0f}, color});
    v.push_back({{max.x, min.y}, {1.0f, 0.0f}, color});
    v.push_back({{max.x, max.y}, {1.0f, 1.0f}, color});
    v.push_back({{min.x, max.y}, {0.0f, 1.0f}, color});
    idx.insert(idx.end(), {base + 0, base + 1, base + 2, base + 2, base + 3, base + 0});
}

void appendTriangle(Vertices& v, Indices& idx, glm::vec2 a, glm::vec2 b, glm::vec2 c,
                    const glm::vec4& color)
{
    const auto base = static_cast<u32>(v.size());
    v.push_back({a, {0.0f, 0.0f}, color});
    v.push_back({b, {0.0f, 0.0f}, color});
    v.push_back({c, {0.0f, 0.0f}, color});
    idx.insert(idx.end(), {base, base + 1, base + 2});
}

/// A segment of the given pixel thickness between two arbitrary points.
void appendLine(Vertices& v, Indices& idx, glm::vec2 a, glm::vec2 b, const glm::vec4& color,
                float thickness = 2.0f)
{
    const glm::vec2 delta = b - a;
    const float length = glm::length(delta);
    if (length < 1e-4f)
        return;

    const glm::vec2 normal = glm::vec2(-delta.y, delta.x) / length * (thickness * 0.5f);
    const auto base = static_cast<u32>(v.size());
    v.push_back({a + normal, {0.0f, 0.0f}, color});
    v.push_back({b + normal, {0.0f, 0.0f}, color});
    v.push_back({b - normal, {0.0f, 0.0f}, color});
    v.push_back({a - normal, {0.0f, 0.0f}, color});
    idx.insert(idx.end(), {base + 0, base + 1, base + 2, base + 2, base + 3, base + 0});
}

/// Filled circle, @p radius pixels, @p segments triangles around a fan.
void appendCircle(Vertices& v, Indices& idx, glm::vec2 center, float radius, const glm::vec4& color,
                  int segments = 28)
{
    for (int i = 0; i < segments; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 6.2831853f * static_cast<float>(i + 1) / static_cast<float>(segments);
        appendTriangle(v, idx, center, center + glm::vec2{std::cos(a0), std::sin(a0)} * radius,
                      center + glm::vec2{std::cos(a1), std::sin(a1)} * radius, color);
    }
}

/// An arrow (a vector): a shaft plus a solid triangular head at @p to.
void appendArrow(Vertices& v, Indices& idx, glm::vec2 from, glm::vec2 to, const glm::vec4& color,
                 float thickness = 2.5f)
{
    const glm::vec2 delta = to - from;
    const float length = glm::length(delta);
    if (length < 1e-3f)
        return;

    const glm::vec2 direction = delta / length;
    const float head = std::min(thickness * 4.0f, length * 0.4f);
    const glm::vec2 neck = to - direction * head;
    const glm::vec2 side = glm::vec2(-direction.y, direction.x) * (head * 0.5f);

    appendLine(v, idx, from, neck, color, thickness);
    appendTriangle(v, idx, to, neck + side, neck - side, color);
}

/// Prints a small matrix (row-major @p values, @p rows x @p cols) as a grid of
/// numbers with TextOverlay. No brackets, no cell backgrounds -- just numbers,
/// because that is all most of these lessons need to show their arithmetic.
void drawMatrix(TextOverlay& overlay, glm::vec2 origin, int rows, int cols, const float* values,
                const glm::vec4& color, float cellW = 56.0f, float cellH = 22.0f, int decimals = 2)
{
    char format[8];
    std::snprintf(format, sizeof(format), "%%.%df", decimals);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            char text[16];
            std::snprintf(text, sizeof(text), format, static_cast<double>(values[r * cols + c]));
            overlay.drawText(text, origin.x + static_cast<float>(c) * cellW,
                             origin.y + static_cast<float>(r) * cellH, color, 0.6f);
        }
    }
}

/// Flattens a column-major glm matrix into a row-major float array, the
/// layout drawMatrix() and the book's own printed matrices both use.
template <int C, int R>
void toRowMajor(const glm::mat<C, R, float, glm::defaultp>& m, float* out)
{
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c)
            out[r * C + c] = m[c][r];
}

/// Draws a background card, a light grid and the two axes for a plot centred
/// at @p origin (window pixels) with @p scale pixels per data unit. Every
/// lesson below builds its own local `toScreen` from these two numbers rather
/// than sharing a "Canvas" object -- each lesson is meant to be read on its
/// own, with nothing to look up in another file.
void drawGridBackdrop(Vertices& v, Indices& idx, glm::vec2 topLeft, glm::vec2 size,
                      glm::vec2 origin, float scale)
{
    appendQuad(v, idx, topLeft, topLeft + size, kSurface);

    for (float x = std::fmod(topLeft.x - origin.x, scale) + topLeft.x; x < topLeft.x + size.x;
        x += scale)
        appendQuad(v, idx, {x, topLeft.y}, {x + 1.0f, topLeft.y + size.y}, kGridLine);
    for (float y = std::fmod(topLeft.y - origin.y, scale) + topLeft.y; y < topLeft.y + size.y;
        y += scale)
        appendQuad(v, idx, {topLeft.x, y}, {topLeft.x + size.x, y + 1.0f}, kGridLine);

    if (origin.y >= topLeft.y && origin.y <= topLeft.y + size.y)
        appendQuad(v, idx, {topLeft.x, origin.y - 0.8f}, {topLeft.x + size.x, origin.y + 0.8f},
                  kAxisLine);
    if (origin.x >= topLeft.x && origin.x <= topLeft.x + size.x)
        appendQuad(v, idx, {origin.x - 0.8f, topLeft.y}, {origin.x + 0.8f, topLeft.y + size.y},
                  kAxisLine);
}

// ===========================================================================
// Lessons
//
// Each one is independent: it opens its own vertex/index batch, draws one
// figure plus its captions, and submits with a single drawBatch2D() call.
// ===========================================================================

/**
 * @brief Example 2.2 (p.20-21, Figure 2.3) -- a system of two linear
 *        equations is the intersection of two lines.
 *
 * @par The book
 * "4x1 + 4x2 = 5,  2x1 - 4x2 = 1 ... the solution space is the point
 * (x1, x2) = (1, 1/4)." Every linear equation in two variables draws a line;
 * a system's solution is where all of them meet.
 *
 * @par In code
 * The system is solved with glm::inverse() on the 2x2 coefficient matrix --
 * built as columns, since glm matrices are column-major -- and both lines are
 * drawn from the same coefficients that were just solved, so the picture and
 * the answer cannot disagree.
 */
[[maybe_unused]] void Example_2_2_LinearSystems(IRenderer* renderer, TextOverlay* overlay,
                                                float width, float height)
{
    // Column 0 = coefficients of x1 in both equations, column 1 = coefficients
    // of x2. A * (x1, x2) then reproduces the two equations exactly.
    const glm::mat2 a(4.0f, 2.0f, 4.0f, -4.0f);
    const glm::vec2 b(5.0f, 1.0f);
    const glm::vec2 x = glm::inverse(a) * b; // Expect (1, 0.25).

    const glm::vec2 origin{width * 0.5f, height * 0.55f};
    const float scale = 90.0f;
    const auto toScreen = [&](glm::vec2 p) {
        return origin + glm::vec2{p.x, -p.y} * scale;
    };

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 380.0f, height * 0.5f - 260.0f};
    drawGridBackdrop(v, idx, topLeft, {760.0f, 520.0f}, origin, scale);

    // Sample each line at two x1 values and draw the segment between them --
    // simpler than clipping an infinite line, and exact since both equations
    // are linear.
    for (int line = 0; line < 2; ++line) {
        const float a0 = a[0][line];
        const float a1 = a[1][line];
        const float rhs = b[line];
        const auto atX1 = [&](float x1) { return glm::vec2{x1, (rhs - a0 * x1) / a1}; };
        appendLine(v, idx, toScreen(atX1(-1.5f)), toScreen(atX1(3.0f)),
                  line == 0 ? kPrimary : kSecondary, 3.0f);
    }

    appendCircle(v, idx, toScreen(x), 6.0f, kAccent);
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Example 2.2 (p.20) -- a system of linear equations is two lines",
                      topLeft.x, topLeft.y - 34.0f, kText, 0.8f);
    overlay->drawText("4x1 + 4x2 = 5", topLeft.x, topLeft.y + 8.0f, kPrimary, 0.62f);
    overlay->drawText("2x1 - 4x2 = 1", topLeft.x, topLeft.y + 30.0f, kSecondary, 0.62f);
    char answer[96];
    std::snprintf(answer, sizeof(answer), "solved with glm::inverse(A) * b  ->  x = (%.2f, %.2f)",
                 x.x, x.y);
    overlay->drawText(answer, topLeft.x, topLeft.y + 60.0f, kAccent, 0.64f);
}

/**
 * @brief Example 2.3 (p.23) -- matrix multiplication is not commutative.
 *
 * @par The book
 * "For A = [1 2 3; 3 2 1] in R^{2x3}, B = [0 2; 1 -1; 0 1] in R^{3x2}, we
 * obtain AB = [2 3; 2 5] in R^{2x2}, BA = [6 4 2; -2 0 2; 3 2 1] in R^{3x3}.
 * Matrix multiplication is not commutative."
 *
 * @par In code
 * glm has rectangular matrix types for exactly this: A (2 rows, 3 columns) is
 * glm::mat3x2 (glm names a matrix by column count x row count), B is
 * glm::mat2x3. Their products glm computes directly -- no custom matrix type
 * needed to reproduce a non-square example.
 */
[[maybe_unused]] void Example_2_3_MatrixMultiplication(IRenderer* renderer, TextOverlay* overlay,
                                                        float width, float height)
{
    // Columns first: A's row0=(1,2,3), row1=(3,2,1), so column0=(1,3), etc.
    const glm::mat3x2 a(glm::vec2(1.0f, 3.0f), glm::vec2(2.0f, 2.0f), glm::vec2(3.0f, 1.0f));
    const glm::mat2x3 b(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(2.0f, -1.0f, 1.0f));

    const glm::mat2 ab = a * b;   // 2x2, expect [2 3; 2 5].
    const glm::mat3 ba = b * a;   // 3x3, expect [6 4 2; -2 0 2; 3 2 1].

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 420.0f, height * 0.5f - 220.0f};
    appendQuad(v, idx, topLeft, topLeft + glm::vec2{840.0f, 440.0f}, kSurface);
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Example 2.3 (p.23) -- AB and BA are different matrices, of different shapes",
                      topLeft.x + 20.0f, topLeft.y + 16.0f, kText, 0.8f);

    float aRow[6];
    toRowMajor(a, aRow);
    float bRow[6];
    toRowMajor(b, bRow);
    float abRow[4];
    toRowMajor(ab, abRow);
    float baRow[9];
    toRowMajor(ba, baRow);

    overlay->drawText("A  (2x3)", topLeft.x + 20.0f, topLeft.y + 60.0f, kPrimary, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 20.0f, topLeft.y + 84.0f}, 2, 3, aRow, kText);

    overlay->drawText("B  (3x2)", topLeft.x + 220.0f, topLeft.y + 60.0f, kSecondary, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 220.0f, topLeft.y + 84.0f}, 3, 2, bRow, kText);

    overlay->drawText("A B  (2x2)", topLeft.x + 380.0f, topLeft.y + 60.0f, kAccent, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 380.0f, topLeft.y + 84.0f}, 2, 2, abRow, kText);

    overlay->drawText("B A  (3x3)", topLeft.x + 560.0f, topLeft.y + 60.0f, kWarn, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 560.0f, topLeft.y + 84.0f}, 3, 3, baRow, kText);

    overlay->drawText("glm::mat3x2 (3 columns, 2 rows) times glm::mat2x3 gives glm::mat2 --",
                      topLeft.x + 20.0f, topLeft.y + 220.0f, kTextMuted, 0.58f);
    overlay->drawText("the reverse product gives glm::mat3 instead. Same two matrices, two",
                      topLeft.x + 20.0f, topLeft.y + 242.0f, kTextMuted, 0.58f);
    overlay->drawText("different answers, of two different shapes.", topLeft.x + 20.0f,
                      topLeft.y + 264.0f, kTextMuted, 0.58f);
}

/**
 * @brief Example 2.4 (p.25) -- a matrix and its inverse.
 *
 * @par The book
 * "The matrices A = [1 2 1; 4 4 5; 6 7 7] and B = [-7 -7 6; 2 1 -1; 4 5 -4]
 * are inverse to each other since AB = I = BA."
 *
 * @par In code
 * Both products are formed with glm::mat3 multiplication, and B is
 * independently recovered with glm::inverse(A) -- so the claim is checked,
 * not just illustrated.
 */
[[maybe_unused]] void Example_2_4_Inverse(IRenderer* renderer, TextOverlay* overlay, float width,
                                          float height)
{
    const glm::mat3 a(glm::vec3(1.0f, 4.0f, 6.0f), glm::vec3(2.0f, 4.0f, 7.0f),
                      glm::vec3(1.0f, 5.0f, 7.0f));
    const glm::mat3 b(glm::vec3(-7.0f, 2.0f, 4.0f), glm::vec3(-7.0f, 1.0f, 5.0f),
                      glm::vec3(6.0f, -1.0f, -4.0f));

    const glm::mat3 ab = a * b;
    const glm::mat3 computedInverse = glm::inverse(a);

    bool matches = true;
    for (int c = 0; c < 3 && matches; ++c)
        for (int r = 0; r < 3 && matches; ++r)
            if (std::abs(computedInverse[c][r] - b[c][r]) > 1e-4f)
                matches = false;

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 420.0f, height * 0.5f - 200.0f};
    appendQuad(v, idx, topLeft, topLeft + glm::vec2{840.0f, 400.0f}, kSurface);
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Example 2.4 (p.25) -- AB = I = BA, checked both ways", topLeft.x + 20.0f,
                      topLeft.y + 16.0f, kText, 0.8f);

    float aRow[9];
    toRowMajor(a, aRow);
    float bRow[9];
    toRowMajor(b, bRow);
    float abRow[9];
    toRowMajor(ab, abRow);
    float invRow[9];
    toRowMajor(computedInverse, invRow);

    overlay->drawText("A", topLeft.x + 20.0f, topLeft.y + 60.0f, kPrimary, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 20.0f, topLeft.y + 84.0f}, 3, 3, aRow, kText);

    overlay->drawText("B", topLeft.x + 200.0f, topLeft.y + 60.0f, kSecondary, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 200.0f, topLeft.y + 84.0f}, 3, 3, bRow, kText);

    overlay->drawText("A B", topLeft.x + 380.0f, topLeft.y + 60.0f, matches ? kOk : kWarn, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 380.0f, topLeft.y + 84.0f}, 3, 3, abRow, kText);

    overlay->drawText("glm::inverse(A)", topLeft.x + 560.0f, topLeft.y + 60.0f, kAccent, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 560.0f, topLeft.y + 84.0f}, 3, 3, invRow, kText);

    overlay->drawText(matches ? "glm::inverse(A) matches B exactly -- the book's claim holds."
                              : "mismatch -- something is wrong.",
                      topLeft.x + 20.0f, topLeft.y + 320.0f, matches ? kOk : kWarn, 0.62f);
}

/**
 * @brief Example 2.13 (p.40, Figure 2.7) -- linear (in)dependence, told as a
 *        journey.
 *
 * @par The book
 * "You can get to Kigali by first going 506 km Northwest to Kampala and then
 * 374 km Southwest ... it is about 751 km West of here. Although this last
 * statement is true, it is not necessary to find Kigali given the previous
 * information." The two legs are independent; the direct route is their sum,
 * and adding it makes the set of three dependent.
 *
 * @par In code
 * Both legs are built from their compass bearings with glm::vec2 and std::sin
 * /cos, summed to get the direct route, and drawn as three arrows -- the
 * addition in the picture is the addition that makes the three dependent.
 */
[[maybe_unused]] void Example_2_13_Kigali(IRenderer* renderer, TextOverlay* overlay, float width,
                                          float height)
{
    constexpr float kInvRoot2 = 0.70710678f;
    const glm::vec2 northwest = glm::vec2{-1.0f, 1.0f} * kInvRoot2 * 506.0f;
    const glm::vec2 southwest = glm::vec2{-1.0f, -1.0f} * kInvRoot2 * 374.0f;
    const glm::vec2 direct = northwest + southwest;

    const glm::vec2 origin{width * 0.5f + 260.0f, height * 0.5f - 40.0f};
    const float scale = 0.55f;
    const auto toScreen = [&](glm::vec2 p) { return origin + glm::vec2{p.x, -p.y} * scale; };

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 420.0f, height * 0.5f - 260.0f};
    appendQuad(v, idx, topLeft, topLeft + glm::vec2{840.0f, 520.0f}, kSurface);

    const glm::vec2 nairobi = toScreen({0.0f, 0.0f});
    const glm::vec2 kampala = toScreen(northwest);
    const glm::vec2 kigali = toScreen(direct);

    appendArrow(v, idx, nairobi, kampala, kPrimary, 3.0f);
    appendArrow(v, idx, kampala, kigali, kAccent, 3.0f);
    appendArrow(v, idx, nairobi, kigali, kSecondary, 3.0f);
    appendCircle(v, idx, nairobi, 5.0f, kText);
    appendCircle(v, idx, kampala, 5.0f, kText);
    appendCircle(v, idx, kigali, 5.0f, kText);

    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Example 2.13 (p.40) -- two directions are enough", topLeft.x + 20.0f,
                      topLeft.y + 16.0f, kText, 0.8f);
    overlay->drawText("506 km Northwest", nairobi.x - 90.0f, nairobi.y - 140.0f, kPrimary, 0.6f);
    overlay->drawText("374 km Southwest", kampala.x + 10.0f, kampala.y + 60.0f, kAccent, 0.6f);
    overlay->drawText("direct route (dependent on the other two)", nairobi.x - 260.0f,
                      nairobi.y + 20.0f, kSecondary, 0.6f);
    overlay->drawText("Nairobi", nairobi.x + 10.0f, nairobi.y + 8.0f, kText, 0.55f);
    overlay->drawText("Kampala", kampala.x + 10.0f, kampala.y - 24.0f, kText, 0.55f);
    overlay->drawText("Kigali", kigali.x - 60.0f, kigali.y + 8.0f, kText, 0.55f);

    char lengths[128];
    std::snprintf(lengths, sizeof(lengths), "|direct| = %.0f km = |NW + SW|, computed with glm::length",
                 glm::length(direct));
    overlay->drawText(lengths, topLeft.x + 20.0f, topLeft.y + 470.0f, kTextMuted, 0.58f);
}

/**
 * @brief Example 2.18 (p.47-48) -- rank, shown as a row dependency.
 *
 * @par The book
 * "A = [1 2 1; -2 -3 1; 3 5 0]. We use Gaussian elimination to determine the
 * rank ... the number of linearly independent rows and columns is 2, such
 * that rk(A) = 2."
 *
 * @par In code
 * Rather than re-implementing Gaussian elimination, the concrete dependency
 * is exposed directly: row3 turns out to equal row1 - row2, computed and
 * compared with glm::vec3 arithmetic. A matrix is singular (and rank-
 * deficient) exactly when glm::determinant() is (numerically) zero, which is
 * checked as well.
 */
[[maybe_unused]] void Example_2_18_Rank(IRenderer* renderer, TextOverlay* overlay, float width,
                                        float height)
{
    const glm::vec3 row1{1.0f, 2.0f, 1.0f};
    const glm::vec3 row2{-2.0f, -3.0f, 1.0f};
    const glm::vec3 row3{3.0f, 5.0f, 0.0f};

    // glm::mat3 is column-major, so build A from its columns (the transpose
    // of how the rows are written above).
    const glm::mat3 a(glm::vec3(row1.x, row2.x, row3.x), glm::vec3(row1.y, row2.y, row3.y),
                      glm::vec3(row1.z, row2.z, row3.z));

    const float determinant = glm::determinant(a);
    const glm::vec3 predicted = row1 - row2;
    const bool dependency = glm::length(predicted - row3) < 1e-4f;

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 380.0f, height * 0.5f - 200.0f};
    appendQuad(v, idx, topLeft, topLeft + glm::vec2{760.0f, 400.0f}, kSurface);
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Example 2.18 (p.47) -- rank, found as a dependency between rows",
                      topLeft.x + 20.0f, topLeft.y + 16.0f, kText, 0.8f);

    float aRow[9];
    toRowMajor(a, aRow);
    overlay->drawText("A", topLeft.x + 20.0f, topLeft.y + 60.0f, kPrimary, 0.62f);
    drawMatrix(*overlay, {topLeft.x + 20.0f, topLeft.y + 84.0f}, 3, 3, aRow, kText);

    char det[80];
    std::snprintf(det, sizeof(det), "glm::determinant(A) = %.3f", static_cast<double>(determinant));
    overlay->drawText(det, topLeft.x + 220.0f, topLeft.y + 66.0f,
                      std::abs(determinant) < 1e-3f ? kWarn : kOk, 0.62f);
    overlay->drawText(std::abs(determinant) < 1e-3f
                          ? "~ 0, so A is singular: rk(A) < 3"
                          : "not 0, so A is regular: rk(A) = 3",
                      topLeft.x + 220.0f, topLeft.y + 90.0f, kTextMuted, 0.58f);

    char rowCheck[96];
    std::snprintf(rowCheck, sizeof(rowCheck), "row1 - row2 = (%.0f, %.0f, %.0f)", predicted.x,
                 predicted.y, predicted.z);
    overlay->drawText(rowCheck, topLeft.x + 220.0f, topLeft.y + 130.0f, kAccent, 0.6f);
    char rowCheck2[96];
    std::snprintf(rowCheck2, sizeof(rowCheck2), "row3          = (%.0f, %.0f, %.0f)", row3.x, row3.y,
                 row3.z);
    overlay->drawText(rowCheck2, topLeft.x + 220.0f, topLeft.y + 152.0f, kAccent, 0.6f);
    overlay->drawText(dependency ? "they match: row3 carries no new direction, so rk(A) = 2"
                                 : "they do not match",
                      topLeft.x + 220.0f, topLeft.y + 182.0f, dependency ? kOk : kWarn, 0.6f);
}

/**
 * @brief Definition 3.1 & Figure 3.3 (p.71) -- the Manhattan and Euclidean
 *        norms, and their unit balls.
 *
 * @par The book
 * "||x||1 := sum |xi|" and "||x||2 := sqrt(sum xi^2)". Figure 3.3 shows the
 * set of vectors of norm 1 for each: a diamond for the Manhattan norm, a
 * circle for the Euclidean one.
 *
 * @par In code
 * The Euclidean ball is glm::length()'s own definition of "distance 1 from
 * the origin", drawn by sampling a circle. The Manhattan diamond is drawn as
 * its four corners (1,0), (0,1), (-1,0), (0,-1) -- the shape |x|+|y|=1 traces
 * exactly. A sample vector's two norms are computed and compared.
 */
[[maybe_unused]] void Example_3_1_Norms(IRenderer* renderer, TextOverlay* overlay, float width,
                                        float height)
{
    const glm::vec2 origin{width * 0.5f - 140.0f, height * 0.5f};
    const float scale = 150.0f;
    const auto toScreen = [&](glm::vec2 p) { return origin + glm::vec2{p.x, -p.y} * scale; };

    const glm::vec2 sample{0.6f, -1.4f};
    const float l1 = std::abs(sample.x) + std::abs(sample.y);
    const float l2 = glm::length(sample);

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 420.0f, height * 0.5f - 260.0f};
    drawGridBackdrop(v, idx, topLeft, {840.0f, 520.0f}, origin, scale * 0.5f);

    // Euclidean unit circle.
    glm::vec2 previous = toScreen({1.0f, 0.0f});
    for (int i = 1; i <= 64; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / 64.0f;
        const glm::vec2 current = toScreen({std::cos(angle), std::sin(angle)});
        appendLine(v, idx, previous, current, kPrimary, 2.5f);
        previous = current;
    }
    // Manhattan unit diamond: |x| + |y| = 1.
    const glm::vec2 diamond[4] = {toScreen({1.0f, 0.0f}), toScreen({0.0f, 1.0f}),
                                  toScreen({-1.0f, 0.0f}), toScreen({0.0f, -1.0f})};
    for (int i = 0; i < 4; ++i)
        appendLine(v, idx, diamond[i], diamond[(i + 1) % 4], kSecondary, 2.5f);

    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(sample), kAccent, 3.0f);
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Definition 3.1, Figure 3.3 (p.71) -- two norms, two unit balls",
                      topLeft.x + 20.0f, topLeft.y + 16.0f, kText, 0.8f);
    overlay->drawText("Euclidean:  ||x||2 = 1  (circle)", topLeft.x + 20.0f, topLeft.y + 470.0f,
                      kPrimary, 0.6f);
    overlay->drawText("Manhattan:  ||x||1 = 1  (diamond)", topLeft.x + 320.0f, topLeft.y + 470.0f,
                      kSecondary, 0.6f);
    char norms[128];
    std::snprintf(norms, sizeof(norms), "x = (0.6, -1.4):  ||x||1 = %.2f    ||x||2 = %.2f (glm::length)",
                 static_cast<double>(l1), static_cast<double>(l2));
    overlay->drawText(norms, topLeft.x + 20.0f, topLeft.y + 60.0f, kAccent, 0.62f);
}

/**
 * @brief Example 3.6 (p.77, Figure 3.5) -- the angle between two vectors.
 *
 * @par The book
 * "Let us compute the angle between x = (1,1) and y = (1,2) ...
 * cos(omega) = <x,y> / (||x|| ||y||) = 3/sqrt(10), and the angle between the
 * two vectors is arccos(3/sqrt(10)) ~ 0.32 rad, which corresponds to about
 * 18 degrees."
 *
 * @par In code
 * glm::dot() and glm::length() are the whole formula; std::acos() turns the
 * cosine into the angle, drawn as an arc between the two arrows.
 */
[[maybe_unused]] void Example_3_6_Angle(IRenderer* renderer, TextOverlay* overlay, float width,
                                        float height)
{
    const glm::vec2 x{1.0f, 1.0f};
    const glm::vec2 y{1.0f, 2.0f};
    const float cosine = glm::dot(x, y) / (glm::length(x) * glm::length(y));
    const float angle = std::acos(std::clamp(cosine, -1.0f, 1.0f));

    const glm::vec2 origin{width * 0.5f - 200.0f, height * 0.5f + 120.0f};
    const float scale = 130.0f;
    const auto toScreen = [&](glm::vec2 p) { return origin + glm::vec2{p.x, -p.y} * scale; };

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 420.0f, height * 0.5f - 220.0f};
    drawGridBackdrop(v, idx, topLeft, {840.0f, 440.0f}, origin, scale * 0.5f);

    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(x), kPrimary, 3.0f);
    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(y), kSecondary, 3.0f);

    // The arc between them, swept from x's bearing to y's bearing.
    const float startAngle = std::atan2(-x.y, x.x); // Screen y is flipped.
    const float endAngle = std::atan2(-y.y, y.x);
    glm::vec2 arcPrevious = toScreen({0.0f, 0.0f}) + glm::vec2{std::cos(startAngle), std::sin(startAngle)} * 44.0f;
    for (int i = 1; i <= 24; ++i) {
        const float t = static_cast<float>(i) / 24.0f;
        const float a = startAngle + (endAngle - startAngle) * t;
        const glm::vec2 point =
            toScreen({0.0f, 0.0f}) + glm::vec2{std::cos(a), std::sin(a)} * 44.0f;
        appendLine(v, idx, arcPrevious, point, kAccent, 2.0f);
        arcPrevious = point;
    }
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Example 3.6 (p.77) -- the angle between x = (1,1) and y = (1,2)",
                      topLeft.x + 20.0f, topLeft.y + 16.0f, kText, 0.8f);
    char formula[128];
    std::snprintf(formula, sizeof(formula),
                 "cos(w) = dot(x,y) / (|x| |y|) = %.4f   ->   w = %.4f rad = %.1f deg",
                 static_cast<double>(cosine), static_cast<double>(angle),
                 static_cast<double>(glm::degrees(angle)));
    overlay->drawText(formula, topLeft.x + 20.0f, topLeft.y + 400.0f, kAccent, 0.62f);
    overlay->drawText("book: ~ 0.32 rad, ~ 18 degrees", topLeft.x + 20.0f, topLeft.y + 424.0f,
                      kTextMuted, 0.58f);
}

/**
 * @brief Section 3.8.1, formula (3.42) (p.82-84) -- orthogonal projection
 *        onto a one-dimensional subspace.
 *
 * @par The book
 * "pi_U(x) = lambda b = <x,b>/||b||^2 b = (b^T x / b^T b) b." The segment
 * connecting x to its projection is orthogonal to the line U spans -- that
 * orthogonality is what makes pi_U(x) the closest point on the line to x.
 *
 * @par In code
 * (3.42) applied directly with glm::dot() to a chosen b and x -- there is
 * nothing else in the formula to implement. The dashed segment from x to the
 * projection is drawn, and checked to be perpendicular to b via glm::dot.
 */
[[maybe_unused]] void Example_3_8_Projection(IRenderer* renderer, TextOverlay* overlay, float width,
                                             float height)
{
    const glm::vec2 b{2.0f, 1.0f};
    const glm::vec2 x{1.0f, 3.0f};
    const float lambda = glm::dot(x, b) / glm::dot(b, b);
    const glm::vec2 projection = lambda * b;
    const float perpendicularity = glm::dot(x - projection, b);

    const glm::vec2 origin{width * 0.5f - 160.0f, height * 0.5f + 60.0f};
    const float scale = 90.0f;
    const auto toScreen = [&](glm::vec2 p) { return origin + glm::vec2{p.x, -p.y} * scale; };

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 420.0f, height * 0.5f - 240.0f};
    drawGridBackdrop(v, idx, topLeft, {840.0f, 480.0f}, origin, scale * 0.5f);

    // The line U = span[b], drawn well past b in both directions.
    appendLine(v, idx, toScreen(-2.0f * b), toScreen(2.5f * b), glm::vec4{kPrimary.r, kPrimary.g, kPrimary.b, 0.35f}, 2.0f);
    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(b), kPrimary, 3.0f);
    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(x), kSecondary, 3.0f);
    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(projection), kAccent, 3.0f);

    // Dashed connector from x down to its projection.
    const glm::vec2 from = toScreen(x);
    const glm::vec2 to = toScreen(projection);
    const glm::vec2 delta = to - from;
    const float length = glm::length(delta);
    constexpr float kDash = 8.0f;
    constexpr float kGap = 6.0f;
    for (float travelled = 0.0f; travelled < length; travelled += kDash + kGap) {
        const glm::vec2 segmentStart = from + delta * (travelled / length);
        const glm::vec2 segmentEnd = from + delta * (std::min(travelled + kDash, length) / length);
        appendLine(v, idx, segmentStart, segmentEnd, kWarn, 2.0f);
    }
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Formula (3.42), p.83 -- orthogonal projection onto a line U = span[b]",
                      topLeft.x + 20.0f, topLeft.y + 16.0f, kText, 0.8f);
    char text1[128];
    std::snprintf(text1, sizeof(text1), "b = (%.0f, %.0f)    x = (%.0f, %.0f)", b.x, b.y, x.x, x.y);
    overlay->drawText(text1, topLeft.x + 20.0f, topLeft.y + 420.0f, kTextMuted, 0.6f);
    char text2[160];
    std::snprintf(text2, sizeof(text2), "lambda = dot(x,b)/dot(b,b) = %.3f   pi_U(x) = lambda*b = (%.3f, %.3f)",
                 static_cast<double>(lambda), static_cast<double>(projection.x),
                 static_cast<double>(projection.y));
    overlay->drawText(text2, topLeft.x + 20.0f, topLeft.y + 444.0f, kAccent, 0.6f);
    char text3[96];
    std::snprintf(text3, sizeof(text3), "dot(x - pi_U(x), b) = %.5f  (0 = orthogonal, as it must be)",
                 static_cast<double>(perpendicularity));
    overlay->drawText(text3, topLeft.x + 20.0f, topLeft.y + 468.0f,
                      std::abs(perpendicularity) < 1e-3f ? kOk : kWarn, 0.58f);
}

/**
 * @brief Example 3.12 (p.89, Figure 3.12) -- Gram-Schmidt orthogonalisation.
 *
 * @par The book
 * "Consider a basis (b1, b2) of R^2, where b1 = (2,0), b2 = (1,1) ...
 * u1 := b1 = (2,0), u2 := b2 - pi_{span[u1]}(b2) = (0,1)."
 *
 * @par In code
 * u2 is built exactly as the book defines it -- b2 minus its own projection
 * onto u1, via glm::dot() -- and the result is checked to be perpendicular to
 * u1, which is the entire point of the construction.
 */
[[maybe_unused]] void Example_3_12_GramSchmidt(IRenderer* renderer, TextOverlay* overlay,
                                               float width, float height)
{
    const glm::vec2 b1{2.0f, 0.0f};
    const glm::vec2 b2{1.0f, 1.0f};

    const glm::vec2 u1 = b1;
    const glm::vec2 u2 = b2 - (glm::dot(u1, b2) / glm::dot(u1, u1)) * u1; // Expect (0, 1).

    const glm::vec2 origin{width * 0.5f - 100.0f, height * 0.5f + 100.0f};
    const float scale = 110.0f;
    const auto toScreen = [&](glm::vec2 p) { return origin + glm::vec2{p.x, -p.y} * scale; };

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 420.0f, height * 0.5f - 240.0f};
    drawGridBackdrop(v, idx, topLeft, {840.0f, 480.0f}, origin, scale * 0.5f);

    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(b2), kSecondary, 2.5f);
    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(u1), kPrimary, 3.0f);
    appendArrow(v, idx, toScreen({0.0f, 0.0f}), toScreen(u2), kAccent, 3.0f);
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Example 3.12 (p.89) -- turning (b1, b2) into an orthogonal basis",
                      topLeft.x + 20.0f, topLeft.y + 16.0f, kText, 0.8f);
    overlay->drawText("b1 = (2,0)  ->  u1 = b1", toScreen(u1).x + 12.0f, toScreen(u1).y - 8.0f,
                      kPrimary, 0.6f);
    overlay->drawText("b2 = (1,1)", toScreen(b2).x + 12.0f, toScreen(b2).y - 8.0f, kSecondary, 0.6f);
    char u2text[96];
    std::snprintf(u2text, sizeof(u2text), "u2 = b2 - proj_u1(b2) = (%.0f, %.0f)", u2.x, u2.y);
    overlay->drawText(u2text, toScreen(u2).x + 12.0f, toScreen(u2).y + 8.0f, kAccent, 0.6f);
    char dotText[64];
    std::snprintf(dotText, sizeof(dotText), "dot(u1, u2) = %.4f  (0 = orthogonal)",
                 static_cast<double>(glm::dot(u1, u2)));
    overlay->drawText(dotText, topLeft.x + 20.0f, topLeft.y + 460.0f,
                      std::abs(glm::dot(u1, u2)) < 1e-4f ? kOk : kWarn, 0.6f);
}

/**
 * @brief Figure 2.10 & equation (2.97) (p.52-53) -- what a linear map does to
 *        a square of points.
 *
 * @par The book
 * "A1 = [cos(pi/4) -sin(pi/4); sin(pi/4) cos(pi/4)]" rotates a 400-point
 * square by 45 degrees; Figure 2.10(b) shows the result.
 *
 * @par In code
 * The point grid is transformed by glm::mix()-ing between the identity and A1
 * every frame, so the deformation is watched happening rather than shown only
 * as a before/after pair. Each point's own transformed matrix-vector product
 * is `a * point`, glm's ordinary operator*.
 */
[[maybe_unused]] void Example_2_22_LinearMap(IRenderer* renderer, TextOverlay* overlay, float width,
                                             float height, float elapsed)
{
    constexpr float kQuarterTurn = 0.78539816f; // pi/4
    const glm::mat2 rotation(std::cos(kQuarterTurn), std::sin(kQuarterTurn), -std::sin(kQuarterTurn),
                             std::cos(kQuarterTurn));

    // A slow triangle wave: 0 -> 1 -> 0, so the deformation runs out and back.
    const float cycle = std::fmod(elapsed * 0.3f, 2.0f);
    const float blend = cycle < 1.0f ? cycle : 2.0f - cycle;

    glm::mat2 current;
    current[0] = glm::mix(glm::vec2(1.0f, 0.0f), rotation[0], blend);
    current[1] = glm::mix(glm::vec2(0.0f, 1.0f), rotation[1], blend);

    const glm::vec2 origin{width * 0.5f, height * 0.5f};
    const float scale = 130.0f;
    const auto toScreen = [&](glm::vec2 p) { return origin + glm::vec2{p.x, -p.y} * scale; };

    Vertices v;
    Indices idx;
    const glm::vec2 topLeft{width * 0.5f - 380.0f, height * 0.5f - 260.0f};
    drawGridBackdrop(v, idx, topLeft, {760.0f, 520.0f}, origin, scale * 0.5f);

    constexpr int kSide = 16;
    for (int j = 0; j < kSide; ++j) {
        for (int i = 0; i < kSide; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kSide - 1);
            const float t = static_cast<float>(j) / static_cast<float>(kSide - 1);
            const glm::vec2 point = current * glm::vec2{-1.0f + 2.0f * u, -1.0f + 2.0f * t};
            appendCircle(v, idx, toScreen(point), 3.0f,
                        glm::mix(kPrimary, kSecondary, t));
        }
    }
    renderer->drawBatch2D(v, idx, {});

    overlay->drawText("Figure 2.10 / (2.97) (p.52) -- A1 rotates a square by 45 degrees",
                      topLeft.x + 20.0f, topLeft.y + 16.0f, kText, 0.8f);
    char blendText[80];
    std::snprintf(blendText, sizeof(blendText), "identity  --(%.0f%%)-->  A1 = rotation by 45 degrees",
                 static_cast<double>(blend * 100.0f));
    overlay->drawText(blendText, topLeft.x + 20.0f, topLeft.y + 470.0f, kAccent, 0.6f);
}

} // namespace

int main()
{
    Engine engine("settings.json");

    IRenderer* renderer = engine.getRenderer();
    auto* windowManager = renderer->getWindowManager();
    const wma::WindowDetails* windowDetails = windowManager->getWindowDetails();

    TextOverlayDesc overlayDesc;
    overlayDesc.pixelHeight = 20.0f;
    TextOverlay overlay(renderer, overlayDesc);

    renderer->setClearColor(kBackground.r, kBackground.g, kBackground.b, 1.0f);

    float elapsed = 0.0f;

    renderer->run([&] {
        const auto width = static_cast<float>(windowDetails->width);
        const auto height = static_cast<float>(windowDetails->height);
        elapsed += static_cast<float>(windowManager->getWindowFlags()->deltaTime) / 1000.0f;

        renderer->beginRenderPass();

        // ---- pick ONE lesson to see: comment out the rest, rebuild --------
        Example_2_2_LinearSystems(renderer, &overlay, width, height);
        // Example_2_3_MatrixMultiplication(renderer, &overlay, width, height);
        // Example_2_4_Inverse(renderer, &overlay, width, height);
        // Example_2_13_Kigali(renderer, &overlay, width, height);
        // Example_2_18_Rank(renderer, &overlay, width, height);
        // Example_3_1_Norms(renderer, &overlay, width, height);
        // Example_3_6_Angle(renderer, &overlay, width, height);
        // Example_3_8_Projection(renderer, &overlay, width, height);
        // Example_3_12_GramSchmidt(renderer, &overlay, width, height);
        // Example_2_22_LinearMap(renderer, &overlay, width, height, elapsed);

        overlay.drawFPS(width - 90.0f, height - 24.0f, 0.55f);

        renderer->endRenderPass();
    });

    return 0;
}
