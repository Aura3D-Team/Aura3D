#ifndef AURA_TEST_GOLDEN_SCENE_H
#define AURA_TEST_GOLDEN_SCENE_H

#pragma once

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "aura/Core/Camera/Camera.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/Renderer/Canvas.h"
#include "aura/Renderer/IRenderer.h"

// The scene the golden-image checks render on every backend: lit, textured instanced cubes, world-space
// debug lines, and a second render target sampled onto a quad. Deterministic: nothing depends on time.

namespace aura3d::test
{

constexpr u32 kGoldenSize = 96;
constexpr int kGoldenGrid = 6; // cubes per side: 36 drawMeshes() items, enough to split across workers

struct GoldenScene
{
    MeshHandle cube;
    MeshHandle sphere;
    MeshHandle floor;
    MaterialHandle material;
    MaterialHandle metal;  //!< Shiny metal: specular from the sun and the point light.
    MaterialHandle ridged; //!< A normal map of ridges on the floor.
    RenderTargetHandle inset;
    RenderTargetHandle frame;
};

inline GoldenScene buildGoldenScene(IRenderer &renderer)
{
    GoldenScene scene;
    scene.cube = renderer.createMesh(MeshLoader::createCube());
    std::vector<u8> checker(usize{16} * 16 * 4);
    for (u32 i = 0; i < 16 * 16; ++i)
    {
        //! 8-texel squares: low frequency, so the software backend's per-triangle mip choice and the
        //! GPUs' per-pixel one sample the same colours.
        const bool light = ((i % 16) / 8 + (i / 16) / 8) % 2 == 0;
        checker[i * 4 + 0] = light ? 230 : 40;
        checker[i * 4 + 1] = light ? 200 : 60;
        checker[i * 4 + 2] = light ? 120 : 150;
        checker[i * 4 + 3] = 255;
    }
    const TextureHandle texture = renderer.createTexture(
        {.width = 16, .height = 16, .format = TextureFormat::Rgba8Srgb}, std::as_bytes(std::span(checker)));
    scene.material = renderer.createMaterial(Material{.albedo = texture});

    //! Explicit: an invalid albedo would keep whatever the previous draw bound.
    const TextureHandle white = renderer.createSolidColorTexture(255, 255, 255);
    scene.sphere = renderer.createMesh(MeshLoader::createSphere(3));
    scene.metal = renderer.createMaterial(
        Material{.albedo = white, .tint = {0.95f, 0.75f, 0.45f, 1.0f}, .roughness = 0.25f, .metallic = 1.0f});
    scene.floor = renderer.createMesh(MeshLoader::createPlane());
    //! Ridges along u: the normal tilts back and forth across x, flat along y.
    std::vector<u8> ridges(usize{32} * 32 * 4);
    for (u32 i = 0; i < 32 * 32; ++i)
    {
        const f32 tilt = std::sin(static_cast<f32>(i % 32) / 32.0f * 6.2831853f * 4.0f) * 0.6f;
        const glm::vec3 n = glm::normalize(glm::vec3{tilt, 0.0f, 1.0f});
        ridges[i * 4 + 0] = static_cast<u8>(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
        ridges[i * 4 + 1] = static_cast<u8>(std::lround((n.y * 0.5f + 0.5f) * 255.0f));
        ridges[i * 4 + 2] = static_cast<u8>(std::lround((n.z * 0.5f + 0.5f) * 255.0f));
        ridges[i * 4 + 3] = 255;
    }
    const TextureHandle normals = renderer.createTexture(
        {.width = 32, .height = 32, .format = TextureFormat::Rgba8Unorm}, std::as_bytes(std::span(ridges)));
    scene.ridged = renderer.createMaterial(
        Material{.albedo = white, .tint = {0.35f, 0.4f, 0.45f, 1.0f}, .roughness = 0.6f, .normalMap = normals});
    scene.inset = renderer.createRenderTarget({.width = 32, .height = 32, .depth = false});
    scene.frame = renderer.createRenderTarget({.width = kGoldenSize, .height = kGoldenSize});
    return scene;
}

//! @p instanced draws the cubes with one drawMeshInstanced() call instead of drawMeshes(): same pixels.
inline void drawGoldenScene(IRenderer &renderer, const GoldenScene &scene, bool instanced = false)
{
    renderer.setClearColor(0.1f, 0.2f, 0.6f, 1);
    renderer.beginRenderPass(scene.inset);
    gfx::Canvas inset;
    inset.triangle(glm::vec2{4, 28}, glm::vec2{28, 28}, glm::vec2{16, 4}, {1, 0.3f, 0.1f, 1});
    renderer.drawBatch(inset);
    renderer.endRenderPass();

    Camera camera = Camera::perspective({.fovDeg = 50.0f, .aspect = 1.0f, .nearZ = 0.1f, .farZ = 50.0f});
    camera.setPosition({4.0f, 3.5f, 5.0f});
    camera.lookAt({0.0f, 0.0f, 0.0f});
    renderer.setTransform(camera.buildUBO());
    //! Directional first and ambient last: the order the shaders sum them in is part of the reference image.
    const std::array<gfx::Light, 4> lights{
        gfx::Light{.direction = glm::normalize(glm::vec3{-1, -2, -1.5f}), .intensity = 0.8f},
        gfx::Light{.type = gfx::LightType::Point,
                   .position = {-1.5f, 1.2f, 1.5f},
                   .color = {1.0f, 0.4f, 0.2f},
                   .intensity = 3.0f,
                   .range = 6.0f},
        gfx::Light{.type = gfx::LightType::Spot,
                   .position = {2.0f, 2.5f, -1.0f},
                   .direction = glm::normalize(glm::vec3{-1.0f, -1.5f, 0.6f}),
                   .color = {0.3f, 0.6f, 1.0f},
                   .intensity = 6.0f,
                   .range = 8.0f,
                   .innerAngle = 0.25f,
                   .outerAngle = 0.45f},
        gfx::Light{.type = gfx::LightType::Ambient, .intensity = 0.2f}};
    renderer.setLights(lights);

    //! The floor and the sphere, before the cubes so drawMeshes() and drawMeshInstanced() see the same state.
    const std::array<IRenderer::DrawItem, 2> props{
        IRenderer::DrawItem{scene.floor, scene.ridged,
                            glm::scale(glm::translate(glm::mat4{1.0f}, {0, -0.32f, 0}), glm::vec3{5.0f})},
        IRenderer::DrawItem{scene.sphere, scene.metal,
                            glm::scale(glm::translate(glm::mat4{1.0f}, {0.4f, 0.55f, 0.4f}), glm::vec3{0.5f})}};
    std::vector<glm::mat4> models;
    for (int x = 0; x < kGoldenGrid; ++x)
        for (int z = 0; z < kGoldenGrid; ++z)
        {
            const glm::vec3 at{(static_cast<f32>(x) - 2.5f) * 0.8f, 0.0f, (static_cast<f32>(z) - 2.5f) * 0.8f};
            models.push_back(glm::scale(glm::translate(glm::mat4{1.0f}, at), glm::vec3{0.3f}));
        }

    renderer.setClearColor(0.08f, 0.08f, 0.1f, 1);
    renderer.beginRenderPass(scene.frame);
    renderer.drawMeshes(props);
    if (instanced)
        renderer.drawMeshInstanced(scene.cube, scene.material, models);
    else
    {
        std::vector<IRenderer::DrawItem> items;
        items.reserve(models.size());
        for (const glm::mat4 &model : models)
            items.push_back({scene.cube, scene.material, model});
        renderer.drawMeshes(items);
    }

    gfx::Canvas lines;
    const gfx::CanvasView view = renderer.canvasView();
    //! A box's twelve edges join corners one bit apart (bit k set: max on axis k), then the X, Y and Z axes.
    const glm::vec3 boxMin{-2.4f, -0.4f, -2.4f};
    const glm::vec3 boxMax{2.4f, 0.4f, 2.4f};
    const auto corner = [&](u32 i)
    {
        return glm::vec3{(i & 1u) ? boxMax.x : boxMin.x, (i & 2u) ? boxMax.y : boxMin.y,
                         (i & 4u) ? boxMax.z : boxMin.z};
    };
    for (u32 i = 0; i < 8; ++i)
        for (u32 bit = 1; bit < 8; bit <<= 1u)
            if ((i & bit) == 0)
                lines.line(view, corner(i), corner(i | bit), {1, 1, 0, 1});
    const glm::vec3 origin{0, 0.6f, 0};
    for (u32 axis = 0; axis < 3; ++axis)
    {
        glm::vec4 color{0, 0, 0, 1};
        color[static_cast<i32>(axis)] = 1;
        lines.line(view, origin, origin + glm::vec3(color), color, 2.0f);
    }
    renderer.drawBatch(lines);

    const std::array<gfx::BatchVertex, 4> corners{{{{4, 4, 0}, {0, 0}, {1, 1, 1, 1}},
                                                   {{28, 4, 0}, {1, 0}, {1, 1, 1, 1}},
                                                   {{28, 28, 0}, {1, 1}, {1, 1, 1, 1}},
                                                   {{4, 28, 0}, {0, 1}, {1, 1, 1, 1}}}};
    constexpr std::array<u32, 6> indices{0, 1, 2, 0, 2, 3};
    renderer.drawBatch(corners, indices, renderer.renderTargetTexture(scene.inset));
    renderer.endRenderPass();
}

//! RGB of @p rgba as a binary PPM, the format the screenshot layer writes too.
inline bool writeGolden(const std::string &path, std::span<const u8> rgba, u32 width, u32 height)
{
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";
    for (usize i = 0; i < rgba.size(); i += 4)
        file.write(reinterpret_cast<const char *>(rgba.data() + i), 3);
    return static_cast<bool>(file);
}

//! RGB triples, or empty when @p path is missing or not a @p width x @p height P6.
inline std::vector<u8> readGolden(const std::string &path, u32 width, u32 height)
{
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    u32 w = 0, h = 0, max = 0;
    file >> magic >> w >> h >> max;
    file.get();
    if (!file || magic != "P6" || w != width || h != height || max != 255)
        return {};
    std::vector<u8> rgb(static_cast<usize>(width) * height * 3);
    file.read(reinterpret_cast<char *>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return file ? rgb : std::vector<u8>{};
}

struct GoldenDiff
{
    usize differing = 0; //!< Pixels with any channel further than the tolerance.
    int worst = 0;       //!< Largest channel difference anywhere.
};

inline GoldenDiff compareGolden(std::span<const u8> rgba, std::span<const u8> rgb, int tolerance)
{
    GoldenDiff diff;
    for (usize p = 0; p * 3 < rgb.size() && p * 4 < rgba.size(); ++p)
    {
        bool off = false;
        for (usize c = 0; c < 3; ++c)
        {
            const int delta = std::abs(int(rgba[p * 4 + c]) - int(rgb[p * 3 + c]));
            diff.worst = std::max(diff.worst, delta);
            off |= delta > tolerance;
        }
        diff.differing += off ? 1u : 0u;
    }
    return diff;
}

} // namespace aura3d::test

#endif // AURA_TEST_GOLDEN_SCENE_H
