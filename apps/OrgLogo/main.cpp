// OrgLogo — a procedurally generated organization logo, rendered by Aura3D.
//
// The whole logo is baked once on the CPU into an RGBA image and shown on a
// single screen-filling quad:
//   * a black field seeded with a faint, unevenly distributed background shine,
//   * hundreds of small, imperfect stars with variable brightness and colour,
//   * a blue circle "in flames" at the centre — a glowing core wrapped in
//     turbulent blue flame tongues that shine out into the star field.
//
// Nothing here touches the engine: it only uses the public IRenderer surface
// (createTextureFromPixels / createMesh / drawMesh). The engine's built-in 3D
// shader multiplies the albedo by the light term, so the light is configured
// as fully "unlit" (ambient = 1, intensity = 0) to show the baked colours 1:1.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "aura/Core/Engine.h"
#include "aura/Core/AuraCore.h"
#include "aura/Core/Camera/Camera.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Material.h"

using namespace aura3d;

namespace {

constexpr int TEX = 1024; //! Logo texture resolution (square).
constexpr float PI = 3.14159265358979323846f;

// Small deterministic noise toolkit
inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float smooth(float t)  { return t * t * (3.0f - 2.0f * t); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

//! Hash a 2D integer lattice point to [0,1).
inline float hash2(int x, int y)
{
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h) * (1.0f / 4294967296.0f);
}

//! Smoothly interpolated 2D value noise.
inline float vnoise(float x, float y)
{
    const int   xi = int(std::floor(x)), yi = int(std::floor(y));
    const float u  = smooth(x - xi), v = smooth(y - yi);
    const float a  = hash2(xi,     yi);
    const float b  = hash2(xi + 1, yi);
    const float c  = hash2(xi,     yi + 1);
    const float d  = hash2(xi + 1, yi + 1);
    return lerp(lerp(a, b, u), lerp(c, d, u), v);
}

//! Fractal Brownian motion: stacked octaves of value noise.
inline float fbm(float x, float y, int octaves = 5)
{
    float sum = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum  += amp * vnoise(x * freq, y * freq);
        freq *= 2.02f;
        amp  *= 0.5f;
    }
    return sum;
}

struct RGB { float r, g, b; };

//! Blue-flame colour ramp: heat in [0,1] -> flame colour. Red stays low so the
//! flames read as blue (deep blue -> electric blue -> cyan -> pale cyan), never
//! washing out to white.
RGB blueFlame(float h)
{
    h = clamp01(h);
    const float pos[5] = {0.00f, 0.35f, 0.65f, 0.90f, 1.00f};
    const RGB   col[5] = {
        {0.00f, 0.02f, 0.08f},
        {0.02f, 0.12f, 0.55f},
        {0.08f, 0.40f, 1.00f},
        {0.35f, 0.75f, 1.00f},
        {0.75f, 0.95f, 1.00f},
    };
    for (int i = 0; i < 4; ++i) {
        if (h <= pos[i + 1]) {
            const float t = (h - pos[i]) / (pos[i + 1] - pos[i]);
            return {lerp(col[i].r, col[i + 1].r, t),
                    lerp(col[i].g, col[i + 1].g, t),
                    lerp(col[i].b, col[i + 1].b, t)};
        }
    }
    return col[4];
}

//! Logo baking
//! Renders the full logo into a tightly packed RGBA8 buffer (TEX x TEX).
std::vector<uint8_t> buildLogo()
{
    std::vector<float> R(size_t(TEX) * TEX, 0.0f);
    std::vector<float> G(size_t(TEX) * TEX, 0.0f);
    std::vector<float> B(size_t(TEX) * TEX, 0.0f);

    const auto add = [&](int x, int y, float ar, float ag, float ab) {
        if (x < 0 || y < 0 || x >= TEX || y >= TEX) return;
        const size_t i = size_t(y) * TEX + x;
        R[i] += ar; G[i] += ag; B[i] += ab;
    };

    // Faint, unevenly distributed cool background shine.
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const float u = float(x) / TEX, v = float(y) / TEX;
            const float n1 = fbm(u * 3.0f,        v * 3.0f,        4);
            const float n2 = fbm(u * 1.3f + 5.0f, v * 1.3f - 3.0f, 3);
            const float glow = std::pow(clamp01(n1 * 0.6f + n2 * 0.5f - 0.35f), 2.0f);
            const size_t i = size_t(y) * TEX + x;
            R[i] += glow * 0.015f;
            G[i] += glow * 0.028f;
            B[i] += glow * 0.055f;
        }

    // Star field: many small, imperfect stars with variable shine.
    std::mt19937 rng(1337u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    constexpr int STAR_COUNT = 850;
    for (int s = 0; s < STAR_COUNT; ++s) {
        const float sx = U(rng) * TEX;
        const float sy = U(rng) * TEX;

        // Keep the area around the central orb clear so no star sits on it.
        const float du = sx / TEX - 0.5f, dv = sy / TEX - 0.5f;
        if (du * du + dv * dv < 0.20f * 0.20f) continue;

        // Brightness biased toward dim, so only a few stars are bright.
        const float br     = std::pow(U(rng), 2.2f);
        const float bright = 0.12f + br * 1.05f;

        // Size in pixels, mostly tiny.
        const float sigma = 0.55f + std::pow(U(rng), 2.0f) * 1.7f;

        // Imperfect shape: elliptical stretch at a random angle (never a perfect disc).
        const float ang   = U(rng) * PI;
        const float aniso = 1.0f + U(rng) * 0.8f;
        const float ca = std::cos(ang), sa = std::sin(ang);
        const float sigX = sigma * aniso, sigY = sigma / aniso;

        // Subtle colour temperature: mostly white, some blue-white, a few warm.
        const float tint = U(rng);
        RGB col = {1.0f, 1.0f, 1.0f};
        if (tint < 0.30f)      col = {0.75f, 0.85f, 1.00f};
        else if (tint > 0.85f) col = {1.00f, 0.92f, 0.80f};

        const int reach = int(std::ceil(std::max(sigX, sigY) * 3.0f)) + 1;
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx) {
                const float lx =  ca * dx + sa * dy;   // rotate into the star's local frame
                const float ly = -sa * dx + ca * dy;
                const float e  = (lx * lx) / (2.0f * sigX * sigX)
                               + (ly * ly) / (2.0f * sigY * sigY);
                const float fall = std::exp(-e);
                if (fall < 0.003f) continue;
                const float in = fall * bright;
                add(int(sx) + dx, int(sy) + dy, in * col.r, in * col.g, in * col.b);
            }

        // Diffraction spikes on the brighter stars — the "not perfectly round" real look.
        if (bright > 0.75f) {
            const float spikeLen = sigma * (5.0f + br * 10.0f);
            const float spikeI   = (bright - 0.6f) * 0.5f;
            const int   L = int(std::ceil(spikeLen));
            for (int t = -L; t <= L; ++t) {
                const float f = std::exp(-std::abs(float(t)) / (spikeLen * 0.35f)) * spikeI;
                if (f < 0.004f) continue;
                add(int(sx) + t, int(sy),     f * col.r, f * col.g, f * col.b);
                add(int(sx),     int(sy) + t, f * col.r, f * col.g, f * col.b);
            }
        }
    }

    // Central blue circle "in flames": a saturated blue disc with a small
    // white-hot centre, wrapped in turbulent blue flame tongues and a halo.
    constexpr float cx = 0.5f, cy = 0.5f;   // centre in UV space
    constexpr float coreR = 0.12f;          // disc radius in UV units
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const float u  = (float(x) + 0.5f) / TEX;
            const float v  = (float(y) + 0.5f) / TEX;
            const float dx = u - cx, dy = v - cy;
            const float d  = std::sqrt(dx * dx + dy * dy);

            // Domain-warped turbulence gives organic flame tongues.
            const float wx   = dx * 7.0f, wy = dy * 7.0f;
            const float warp = fbm(wx + 11.3f, wy - 4.7f, 4);
            const float turb = fbm(wx + warp * 1.7f, wy + warp * 1.7f, 5);

            // Solid blue disc — the "circle" — brighter toward its middle.
            const float discMask = clamp01((coreR - d) / 0.02f);
            const float inner    = clamp01(1.0f - d / coreR);
            const RGB   discCol  = {lerp(0.05f, 0.35f, inner),
                                    lerp(0.28f, 0.72f, inner),
                                    lerp(0.90f, 1.00f, inner)};

            // Small white-blue shining hot spot at the very centre.
            float hot = clamp01((coreR * 0.38f - d) / (coreR * 0.38f));
            hot = std::pow(hot, 1.5f);

            // Blue flame tongues licking outward past the rim.
            const float flameEdge = coreR + 0.16f + (turb - 0.5f) * 0.28f;
            float corona = std::pow(clamp01((flameEdge - d) / 0.13f), 1.5f);
            corona *= clamp01((d - coreR * 0.70f) / 0.03f);        // only outside the disc
            const float coronaHeat = clamp01(0.35f + corona * 0.70f + (turb - 0.5f) * 0.30f);

            // Broad blue shine into the star field.
            const float halo = std::exp(-d / 0.14f);

            const RGB fc = blueFlame(coronaHeat);
            const size_t i = size_t(y) * TEX + x;
            R[i] += discMask * discCol.r * 1.25f + hot * 0.80f + corona * fc.r * 1.15f + halo * 0.10f;
            G[i] += discMask * discCol.g * 1.25f + hot * 0.92f + corona * fc.g * 1.15f + halo * 0.30f;
            B[i] += discMask * discCol.b * 1.25f + hot * 1.00f + corona * fc.b * 1.15f + halo * 0.78f;
        }

    // Tone-map and pack to RGBA8.
    std::vector<uint8_t> px(size_t(TEX) * TEX * 4);
    const auto enc = [](float c) {
        return uint8_t(clamp01(std::pow(clamp01(c), 0.85f)) * 255.0f + 0.5f);
    };
    for (size_t i = 0; i < size_t(TEX) * TEX; ++i) {
        px[i * 4 + 0] = enc(R[i]);
        px[i * 4 + 1] = enc(G[i]);
        px[i * 4 + 2] = enc(B[i]);
        px[i * 4 + 3] = 255;
    }
    return px;
}

//! Unit quad in the XY plane facing +Z (toward the camera). The winding is the
//! engine's createPlane() rotated onto XY, so Vulkan back-face culling keeps it.
gfx::Mesh3D makeQuad()
{
    gfx::Mesh3D m;
    const glm::vec3 corners[4] = {
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        { 0.5f,  0.5f, 0.0f},
        {-0.5f,  0.5f, 0.0f},
    };
    const glm::vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 4; ++i) {
        gfx::Vertex3D v{};
        v.pos      = corners[i];
        v.texCoord = uvs[i];
        v.color    = glm::vec4(1.0f);
        v.normal   = {0.0f, 0.0f, 1.0f};
        m.vertices.push_back(v);
    }
    m.indices = {0, 1, 2, 2, 3, 0};
    return m;
}

} // namespace

int main()
{
    Engine    engine("settings.json");
    IRenderer* r = engine.getRenderer();

    // Unlit: built-in shader does albedo * (ambient + diffuse * intensity).
    // ambient = 1, intensity = 0 -> the baked texture shows at its true colours.
    gfx::LightUBO light;
    light.direction = {0.0f, -1.0f, 0.0f};
    light.color     = {1.0f, 1.0f, 1.0f, 1.0f};
    light.intensity = 0.0f;
    light.ambient   = 1.0f;
    r->setLight(light);

    // Bake the logo once and upload it as a texture.
    const std::vector<uint8_t> pixels = buildLogo();
    const TextureHandle logoTex = r->createTextureFromPixels(pixels.data(), TEX, TEX);

    Material logoMat;
    logoMat.albedo = logoTex;
    const MaterialHandle logoMatH = r->createMaterial(logoMat);

    const MeshHandle quad = r->createMesh(makeQuad());

    // Orthographic camera; Camera::ortho handles the per-backend Y flip.
    Camera cam = Camera::ortho({.left = -1.0f, .right = 1.0f,
                                .bottom = -1.0f, .top = 1.0f,
                                .nearZ = 0.1f, .farZ = 10.0f});
    cam.setPosition({0.0f, 0.0f, 2.0f});
    cam.lookAt({0.0f, 0.0f, 0.0f});

    // Fill the [-1,1] view box: the unit quad scaled by 2.
    const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 1.0f));

    r->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    r->setTransform(cam.buildUBO(model));

    r->run([&]() {
        r->beginRenderPass();
        r->setTransform(cam.buildUBO(model));
        r->bindMaterial(logoMatH);
        r->drawMesh(quad);
        r->endRenderPass();
    });

    return 0;
}
