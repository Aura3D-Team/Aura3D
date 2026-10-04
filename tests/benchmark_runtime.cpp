#include "RuntimeAudioDevice.h"
#include "aura/Core/Engine.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/UI/UI.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <thread>
#ifdef AURA_HAS_OPENGL
#include <glad/glad.h>
#endif
#ifdef AURA_HAS_CPU
#include "aura/Renderer/Software/CPURenderer.h"
#endif

namespace
{
thread_local bool counting = false;
thread_local std::size_t allocations = 0, bytes = 0;
} // namespace
#ifndef AURA_ENABLE_DEBUG_MODE
void *operator new(std::size_t n)
{
    if (counting)
    {
        ++allocations;
        bytes += n;
    }
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete[](void *p) noexcept
{
    ::operator delete(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    ::operator delete(p);
}
#endif
using namespace aura3d;
using namespace aura3d::ui;
using Clock = std::chrono::steady_clock;

template <class Body> void measure(const char *label, int samples, Body body)
{
    allocations = bytes = 0;
    counting = true;
    const auto cold = Clock::now();
    body();
    const double coldUs = std::chrono::duration<double, std::micro>(Clock::now() - cold).count();
    counting = false;
    const auto coldAllocations = allocations, coldBytes = bytes;
    for (int i = 0; i < 5; ++i)
        body();
    std::vector<double> times(samples);
    allocations = bytes = 0;
    counting = true;
    for (double &time : times)
    {
        const auto start = Clock::now();
        body();
        time = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    }
    counting = false;
    std::sort(times.begin(), times.end());
    std::printf("BENCH %s n=%d cold_us=%.3f cold_new_count=%zu cold_new_bytes=%zu p50_us=%.3f p95_us=%.3f p99_us=%.3f "
                "new_count=%zu new_bytes=%zu\n",
                label, samples, coldUs, coldAllocations, coldBytes, times[samples / 2], times[samples * 95 / 100],
                times[samples * 99 / 100], allocations, bytes);
}

int main(int argc, char **argv)
{
    ink::LogManager::getInstance().setGlobalLevel(ink::LogLevel::ERROR);
    if (argc > 1)
    {
        AuraConfig config;
        config.renderer.backend = argv[1];
        config.renderer.validationLayers = std::getenv("AURA_BENCH_VALIDATION") != nullptr;
        config.window.width = 320;
        config.window.height = 240;
        config.window.fpsLimit = 0;
        config.window.vsync = false;
        config.graphics.cpuThreads = 4;
        config.graphics.gpuPreference = "any";
        config.audio.backend = wma::AudioBackend::Null;
        config.logging.level = ink::LogLevel::ERROR;
        Engine engine(config);
        auto &renderer = *engine.getRenderer();
        if (argc > 2 && std::string_view(argv[2]) == "primitives")
        {
            struct Probe
            {
                i32 x;
                i32 y;
                std::array<u8, 3> rgb;
            };
            // Interior, outside-edge and flat-end probes also catch double
            // blending where a translucent line's two triangles meet.
            const std::array probes{
                Probe{48, 24, {255, 0, 0}}, Probe{15, 24, {0, 0, 0}}, Probe{96, 24, {0, 0, 0}},
                Probe{48, 19, {0, 0, 0}}, Probe{48, 28, {0, 0, 0}}, Probe{128, 40, {0, 255, 0}},
                Probe{128, 15, {0, 0, 0}}, Probe{128, 72, {0, 0, 0}}, Probe{200, 40, {0, 0, 255}},
                Probe{193, 47, {0, 0, 0}}, Probe{48, 56, {0, 255, 255}}, Probe{15, 56, {0, 0, 0}},
                Probe{96, 56, {0, 0, 0}}, Probe{48, 95, {128, 128, 128}}, Probe{48, 96, {128, 128, 128}},
                Probe{200, 112, {128, 0, 128}}, Probe{48, 120, {128, 128, 0}}, Probe{32, 160, {255, 255, 0}},
                Probe{15, 160, {0, 0, 0}}, Probe{96, 160, {0, 0, 0}}, Probe{136, 152, {255, 0, 255}},
                Probe{172, 184, {0, 0, 0}}, Probe{248, 152, {0, 255, 255}}, Probe{284, 184, {0, 0, 0}},
                Probe{280, 40, {128, 0, 128}}, Probe{280, 20, {0, 0, 128}}, Probe{264, 40, {255, 0, 0}},
                // Scene depth: the mesh hides the red batch triangle behind it, the
                // blue 3D line in front of it shows, and the triangle shows beside it.
                Probe{180, 215, {0, 255, 0}}, Probe{155, 215, {255, 0, 0}}, Probe{185, 222, {0, 0, 255}},
                Probe{210, 215, {0, 0, 0}}};
            struct Segment
            {
                glm::vec2 from;
                glm::vec2 to;
                glm::vec4 color;
            };
            const std::array segments{
                Segment{{128, 16}, {128, 72}, {0, 1, 0, 1}},    Segment{{176, 16}, {224, 64}, {0, 0, 1, 1}},
                Segment{{96, 56}, {16, 56}, {0, 1, 1, 1}},      Segment{{16, 96}, {96, 96}, {1, 1, 1, .5f}},
                Segment{{176, 88}, {224, 136}, {1, 0, 1, .5f}}, Segment{{96, 120}, {16, 120}, {1, 1, 0, .5f}}};
            const auto emptyCoverage = renderer.createCoverageTexture(1, 1);
            if (!isValidHandle(emptyCoverage))
                return 1;

            //! Window pixels with depth, in the backend's clip-space convention: z = 0.5 is
            //! nearer than z = -0.5.
            const bool depthZeroToOne = renderer.getBackendType() == RendererChoice::VULKAN ||
                                        renderer.getBackendType() == RendererChoice::METAL;
            const glm::mat4 pixels = depthZeroToOne ? glm::orthoRH_ZO(0.0f, 320.0f, 240.0f, 0.0f, -1.0f, 1.0f)
                                                    : glm::orthoRH_NO(0.0f, 320.0f, 240.0f, 0.0f, -1.0f, 1.0f);
            renderer.setLight({.intensity = 0.0f, .ambient = 1.0f});
            const auto white = renderer.createSolidColorTexture(255, 255, 255, 255);
            gfx::Mesh3D occluder;
            for (const glm::vec2 corner :
                 {glm::vec2{170, 205}, glm::vec2{200, 205}, glm::vec2{200, 225}, glm::vec2{170, 225}})
                occluder.vertices.push_back({{corner, 0.5f}, {}, {0, 1, 0, 1}, {0, 0, 1}});
            //! Both windings, so culling cannot hide it on any backend.
            occluder.indices = {0, 1, 2, 2, 3, 0, 0, 2, 1, 2, 0, 3};
            const MeshHandle occluderMesh = renderer.createMesh(std::move(occluder));
            if (!isValidHandle(occluderMesh) || !isValidHandle(white))
                return 1;
            const std::array<gfx::BatchVertex, 3> textured{{{{0, 0, 0}, {.5f, .5f}, {1, 0, 1, 1}},
                                                            {{8, 0, 0}, {.5f, .5f}, {1, 0, 1, 1}},
                                                            {{0, 8, 0}, {.5f, .5f}, {1, 0, 1, 1}}}};
            constexpr std::array<u32, 3> texturedIndices{0, 1, 2};
            renderer.setClearColor(0, 0, 0, 1);
            u32 rendered = 0;
            u32 checkedPixels = 0;
            for (u32 frame = 0; frame < 12; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                renderer.beginRenderPass();
                renderer.setTransform({glm::mat4{1.0f}, glm::mat4{1.0f}, pixels});
                renderer.drawMesh(occluderMesh, white);
                renderer.fillTriangle(glm::vec3{150, 200, -0.5f}, glm::vec3{230, 200, -0.5f},
                                      glm::vec3{150, 236, -0.5f}, {1, 0, 0, 1});
                renderer.drawLine(glm::vec3{172, 222, 0.9f}, glm::vec3{198, 222, 0.9f}, {0, 0, 1, 1}, 2);
                // Primitives must select the white fallback after a coverage draw.
                renderer.drawBatch(textured, texturedIndices, emptyCoverage);
                renderer.drawLine({16, 24}, {96, 24}, {1, 0, 0, 1}, 8);
                for (const Segment &segment : segments)
                    renderer.drawLine(segment.from, segment.to, segment.color, 8);
                renderer.fillRect({16, 144}, {80, 48}, {1, 1, 0, 1});
                renderer.fillTriangle({128, 144}, {176, 144}, {128, 192}, {1, 0, 1, 1});
                renderer.fillTriangle({240, 192}, {288, 144}, {240, 144}, {0, 1, 1, 1});
                renderer.fillRect({256, 24}, {48, 48}, {1, 0, 0, 1});
                renderer.drawLine({280, 16}, {280, 80}, {0, 0, 1, .5f}, 16);
                renderer.endRenderPass();
                for (const auto &probe : probes)
                {
                    std::array<u8, 4> actual{};
                    bool readable = false;
#ifdef AURA_HAS_OPENGL
                    if (renderer.getBackendType() == RendererChoice::OPENGL)
                    {
                        glReadPixels(probe.x, 239 - probe.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
                        readable = true;
                    }
#endif
#ifdef AURA_HAS_CPU
                    if (renderer.getBackendType() == RendererChoice::SOFTWARE)
                    {
                        const u32 pixel = static_cast<cpu::CPURenderer &>(renderer)
                                              .getFrameBufferManager()
                                              ->getPixel({probe.x, probe.y})
                                              .rgb;
                        actual = {static_cast<u8>(pixel >> 16), static_cast<u8>(pixel >> 8), static_cast<u8>(pixel),
                                  static_cast<u8>(pixel >> 24)};
                        readable = true;
                    }
#endif
                    if (!readable)
                        continue;
                    for (usize c = 0; c < 3; ++c)
                        if (std::abs(int(actual[c]) - int(probe.rgb[c])) > 1)
                        {
                            std::fprintf(stderr, "PRIMITIVES pixel (%d,%d): got %u,%u,%u expected %u,%u,%u\n", probe.x,
                                         probe.y, actual[0], actual[1], actual[2], probe.rgb[0], probe.rgb[1],
                                         probe.rgb[2]);
                            return 2;
                        }
                    ++checkedPixels;
                }
#ifdef AURA_HAS_OPENGL
                if (renderer.getBackendType() == RendererChoice::OPENGL && glGetError() != GL_NO_ERROR)
                    return 2;
#endif
                renderer.endFrame();
                ++rendered;
            }
            std::printf("PRIMITIVES backend=%s rendered_frames=%u checked_pixels=%u\n", argv[1], rendered,
                        checkedPixels);
            return rendered == 12 ? 0 : 1;
        }
        if (argc > 2 && std::string_view(argv[2]) == "coverage")
        {
            // Screenshot swatches: black, black, half-green, green, magenta;
            // the second row samples a large atlas after upload-slot rotation.
            const auto untouched = renderer.createCoverageTexture(7, 3);
            const auto coverage = renderer.createCoverageTexture(7, 3);
            const auto rgba = renderer.createDynamicTexture(1, 1);
            const auto large = renderer.createCoverageTexture(2048, 2048);
            if (!isValidHandle(untouched) || !isValidHandle(coverage) || !isValidHandle(rgba) || !isValidHandle(large))
                return 1;
            const std::array<u8, 3> patch{0, 128, 255};
            const std::array<u8, 4> magenta{255, 0, 255, 255};
            const std::vector<u8> full(usize{2048} * 2048, 255);
            const std::vector<u8> edge(2045, 255);
            const std::array<u32, 6> indices{0, 1, 2, 2, 3, 0};
#ifdef AURA_HAS_OPENGL
            VertexBufferHandle sceneVertices;
            IndexBufferHandle sceneIndices;
            if (renderer.getBackendType() == RendererChoice::OPENGL)
            {
                // The lower middle swatch is a scene draw after the UI, with
                // its texture bound before the UI overwrites texture unit 0.
                std::vector<gfx::Vertex3D> vertices;
                for (const glm::vec2 p : {glm::vec2{88, 112}, {136, 112}, {136, 160}, {136, 160}, {88, 160}, {88, 112}})
                    vertices.push_back({{p.x / 160 - 1, 1 - p.y / 120, 0}, {.5f, .5f}, {1, 1, 1, 1}, {0, 0, 1}});
                sceneVertices = renderer.createVertexBuffer(std::move(vertices));
                sceneIndices = renderer.createIndexBuffer(std::vector<u32>{0, 1, 2, 3, 4, 5});
                auto light = renderer.getLight();
                light.intensity = 0;
                light.ambient = 1;
                renderer.setLight(light);
                renderer.setTransform({glm::mat4{1}, glm::mat4{1}, glm::mat4{1}});
            }
#endif
            const auto swatch = [&](TextureHandle texture, f32 x, f32 y, glm::vec2 uv, glm::vec4 color)
            {
                const std::array<gfx::BatchVertex, 4> quad{{{{x, y, 0}, uv, color},
                                                            {{x + 48, y, 0}, uv, color},
                                                            {{x + 48, y + 48, 0}, uv, color},
                                                            {{x, y + 48, 0}, uv, color}}};
                renderer.drawBatch(quad, indices, texture);
            };
            renderer.setClearColor(0, 0, 0, 1);
            u32 rendered = 0;
            for (u32 frame = 0; frame < 12; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                for (u32 batch = 0; batch < 4; ++batch)
                {
                    renderer.updateCoverageTextureRegion(coverage, 1, 1, 3, 1, patch.data());
                    renderer.updateTextureRegion(rgba, 0, 0, 1, 1, magenta.data());
                    renderer.updateCoverageTextureRegion(large, 0, 0, 2048, 2048, full.data());
                }
                // 4 MiB minus three bytes, then a one-byte copy whose alignment
                // padding must rotate the staging slot instead of exceeding it.
                renderer.updateCoverageTextureRegion(large, 0, 0, 2048, 2047, full.data());
                renderer.updateCoverageTextureRegion(large, 0, 2047, 2045, 1, edge.data());
                renderer.updateCoverageTextureRegion(coverage, 3, 1, 1, 1, &patch[2]);
                renderer.updateTextureRegion(rgba, 0, 0, 1, 1, magenta.data());
                if (std::getenv("AURA_BENCH_INVALID_REGIONS"))
                {
                    renderer.updateCoverageTextureRegion(coverage, UINT32_MAX, 0, 1, 1, patch.data());
                    renderer.updateCoverageTextureRegion(coverage, 1, 0, UINT32_MAX, 1, patch.data());
                    renderer.updateCoverageTextureRegion(coverage, 0, 0, 7, UINT32_MAX, patch.data());
                }
                renderer.beginRenderPass();
#ifdef AURA_HAS_OPENGL
                if (renderer.getBackendType() == RendererChoice::OPENGL)
                    renderer.bindTexture(rgba);
#endif
                swatch(untouched, 16, 32, {.5f, .5f}, {0, 1, 0, 1});
                for (u32 i = 0; i < 3; ++i)
                    swatch(coverage, 72 + 56 * static_cast<f32>(i), 32, {(1.5f + static_cast<f32>(i)) / 7.0f, .5f},
                           {0, 1, 0, 1});
                swatch(rgba, 240, 32, {.5f, .5f}, {1, 1, 1, 1});
                swatch(large, 16, 112, {.5f, .5f}, {0, 1, 0, 1});
#ifdef AURA_HAS_OPENGL
                if (renderer.getBackendType() == RendererChoice::OPENGL)
                {
                    renderer.bindVertexBuffer(sceneVertices);
                    renderer.bindIndexBuffer(sceneIndices);
                    if (frame % 2 == 0)
                        renderer.drawIndexed(6);
                    else
                        renderer.draw(6);

                    const auto checkPixel = [&](i32 x, i32 y, std::array<u8, 3> expected)
                    {
                        std::array<u8, 4> actual{};
                        glReadPixels(x, 239 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
                        for (usize c = 0; c < 3; ++c)
                            if (std::abs(int(actual[c]) - int(expected[c])) > 1)
                            {
                                std::fprintf(stderr, "COVERAGE pixel (%d,%d): got %u,%u,%u expected %u,%u,%u\n", x, y,
                                             actual[0], actual[1], actual[2], expected[0], expected[1], expected[2]);
                                return false;
                            }
                        return true;
                    };
                    if (!checkPixel(40, 56, {0, 0, 0}) || !checkPixel(96, 56, {0, 0, 0}) ||
                        !checkPixel(152, 56, {0, 128, 0}) || !checkPixel(208, 56, {0, 255, 0}) ||
                        !checkPixel(264, 56, {255, 0, 255}) || !checkPixel(40, 136, {0, 255, 0}) ||
                        !checkPixel(112, 136, {255, 0, 255}) || glGetError() != GL_NO_ERROR)
                        return 2;
                }
#endif
                renderer.endRenderPass();
                renderer.endFrame();
                ++rendered;
            }
            std::printf("COVERAGE backend=%s rendered_frames=%u\n", argv[1], rendered);
            return rendered == 12 ? 0 : 1;
        }
        if (argc > 2 && std::string_view(argv[2]) == "cache")
        {
            // Sparse indexed draws alternate buffer sizes without raster work.
            // Run with: SDL_VIDEODRIVER=dummy benchmark_runtime software cache
            std::vector<gfx::Vertex3D> small(3);
            for (auto &vertex : small)
                vertex.pos = {3, 3, 0};
            std::vector<gfx::Vertex3D> large(100000, small.front());
            const auto smallBuffer = renderer.createVertexBuffer(std::move(small));
            const auto largeBuffer = renderer.createVertexBuffer(std::move(large));
            renderer.bindIndexBuffer(renderer.createIndexBuffer(std::vector<u32>{0, 1, 2}));
            renderer.setTransform({glm::mat4{1}, glm::mat4{1}, glm::mat4{1}});
            for (const bool alternate : {false, true})
                measure(alternate ? "cpu_cache_alternating" : "cpu_cache_same", 2000,
                        [&]
                        {
                            renderer.bindVertexBuffer(alternate ? smallBuffer : largeBuffer);
                            renderer.drawIndexed(3);
                            renderer.bindVertexBuffer(largeBuffer);
                            renderer.drawIndexed(3);
                        });
            return 0;
        }
        gfx::Mesh3D mesh;
        mesh.vertices = {{{-.8f, -.8f, .5f}, {0, 0}, {1, 1, 1, 1}, {0, 0, 1}},
                         {{.8f, -.8f, .5f}, {1, 0}, {1, 1, 1, 1}, {0, 0, 1}},
                         {{0, .8f, .5f}, {.5f, 1}, {1, 1, 1, 1}, {0, 0, 1}}};
        mesh.indices = {0, 1, 2};
        const bool indexed = argc > 2 && std::string_view(argv[2]) == "indexed";
        if (indexed)
        {
            mesh.vertices.clear();
            mesh.indices.clear();
            constexpr u32 side = 100;
            for (u32 y = 0; y < side; ++y)
                for (u32 x = 0; x < side; ++x)
                    mesh.vertices.push_back({{-0.9f + 1.8f * static_cast<f32>(x) / (side - 1),
                                              -0.9f + 1.8f * static_cast<f32>(y) / (side - 1), .5f},
                                             {f32(x) / (side - 1), f32(y) / (side - 1)},
                                             {1, 1, 1, 1},
                                             {0, 0, 1}});
            for (u32 y = 0; y + 1 < side; ++y)
                for (u32 x = 0; x + 1 < side; ++x)
                {
                    const u32 i = y * side + x;
                    mesh.indices.insert(mesh.indices.end(), {i, i + 1, i + side, i + 1, i + side + 1, i + side});
                }
        }
        const auto handle = renderer.createMesh(std::move(mesh));
        const auto red = renderer.createSolidColorTexture(255, 0, 0);
        const auto green = renderer.createSolidColorTexture(0, 255, 0);
        const auto redMaterial = renderer.createMaterial(Material{.albedo = red});
        std::vector<IRenderer::DrawItem> draws(indexed ? 1 : 512);
        for (auto &draw : draws)
        {
            draw.mesh = handle;
            draw.material = redMaterial;
            draw.model = glm::mat4{1};
        }
        gfx::LightUBO light;
        light.ambient = 1;
        light.intensity = 0;
        renderer.setLight(light);
        renderer.setTransform({glm::mat4{1}, glm::mat4{1}, glm::mat4{1}});
        std::array<TextureHandle, 8> textures{};
        for (auto &texture : textures)
            texture = renderer.createDynamicTexture(64, 64);
        std::vector<u8> pixels(usize{64} * 64 * 4, 255);
        const std::array<u32, 6> indices{0, 1, 2, 2, 3, 0};
        const bool check = std::getenv("AURA_BENCH_VALIDATE_IMAGE") != nullptr;
        TextureHandle large;
        std::vector<u8> largePixels;
        if (check)
        {
            large = renderer.createDynamicTexture(1024, 1024);
            largePixels.assign(usize{1024} * 1024 * 4, 255);
        }
        measure(indexed ? "cpu_indexed" : argv[1], check ? 4 : 120,
                [&]
                {
                    renderer.beginFrame();
                    if (check)
                        for (int i = 0; i < 4; ++i)
                            renderer.updateTextureRegion(large, 0, 0, 1024, 1024, largePixels.data());
                    for (auto texture : textures)
                        renderer.updateTextureRegion(texture, 0, 0, 64, 64, pixels.data());
                    if (check)
                    {
                        std::array<u8, usize{32} * 32 * 4> patch{};
                        for (usize i = 0; i < patch.size(); i += 4)
                        {
                            patch[i] = patch[i + 1] = patch[i + 3] = 255;
                        }
                        renderer.updateTextureRegion(textures[0], 32, 32, 32, 32, patch.data());
                    }
                    if (check && std::getenv("AURA_BENCH_INVALID_REGIONS"))
                    {
                        const std::array<u8, 4> invalid{255, 0, 0, 255};
                        renderer.updateTextureRegion(textures[0], UINT32_MAX, 0, 2, 1, invalid.data());
                        renderer.updateTextureRegion(textures[0], 1, 0, UINT32_MAX, 1, invalid.data());
                    }
                    renderer.beginRenderPass();
                    renderer.drawMeshes(draws);
                    renderer.drawMesh(handle, green); // red must win equal-depth testing
                    if (check)
                    {
                        renderer.drawMeshes(draws);
                        renderer.drawMesh(handle, green);
                    }
                    for (int i = 0; i < (check ? 1200 : 400); ++i)
                    {
                        const f32 x = static_cast<f32>(i % 80) * 4;
                        // Integer division selects the overlay's grid row.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        const f32 y = static_cast<f32>(i / 80) * 4;
                        std::array<gfx::BatchVertex, 4> quad{{{{x, y, 0}, {0, 0}, {0, 0, 1, 1}},
                                                              {{x + 3, y, 0}, {1, 0}, {0, 0, 1, 1}},
                                                              {{x + 3, y + 3, 0}, {1, 1}, {0, 0, 1, 1}},
                                                              {{x, y + 3, 0}, {0, 1}, {0, 0, 1, 1}}}};
                        if (check && (i == 0 || i == 8))
                            for (auto &vertex : quad)
                            {
                                vertex.texCoord = glm::vec2(i == 0 ? .75f : .25f);
                                vertex.color = glm::vec4{1};
                            }
                        renderer.drawBatch(quad, indices, textures[i % 8]);
                    }
                    renderer.endRenderPass();
                    renderer.endFrame();
                });
        return 0;
    }
    JobSystem jobs(4);
    std::array<int, 4096> values{};
    measure("dispatch", 2000,
            [&]
            {
                jobs.dispatch(values.size(),
                              [&](i32 first, i32 end)
                              {
                                  for (i32 i = first; i < end; ++i)
                                      ++values[i];
                              });
            });
    const char *path = "/tmp/aura-runtime-grid.obj";
    {
        std::ofstream file(path);
        constexpr int side = 120;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                file << "v " << x << ' ' << y << " 0\n";
        for (int y = 0; y < side - 1; ++y)
            for (int x = 0; x < side - 1; ++x)
            {
                const int i = y * side + x + 1;
                file << "f " << i << ' ' << i + 1 << ' ' << i + side << "\nf " << i + 1 << ' ' << i + side + 1 << ' '
                     << i + side << '\n';
            }
    }
    measure("obj", 30,
            [&]
            {
                auto mesh = MeshLoader::loadOBJ(path);
                if (mesh.empty())
                    std::abort();
            });
    std::remove(path);
    AtlasTextShaper shaper;
    UIRoot root(shaper);
    auto &column = root.setContent<Column>();
    auto &first = column.add<Button>("first");
    auto &second = column.add<Button>("second");
    root.resize({320, 240});
    root.update(0);
    measure("hover", 10000,
            [&]
            {
                root.pointerMoved(first.bounds().center());
                root.pointerMoved(second.bounds().center());
            });
    auto device = std::make_unique<RuntimeAudioDevice>();
    auto *pump = device.get();
    AudioEngine audio(std::move(device), 32);
    AudioClipData pcm;
    pcm.sampleRate = audio.sampleRate();
    pcm.channelCount = 1;
    pcm.samples.assign(8192, .01f);
    auto clip = audio.createClip(pcm);
    for (int i = 0; i < 32; ++i)
        (void)audio.play(AudioSourceDesc{.clip = clip, .loop = true});
    std::array<f32, 512> output{};
    measure("audio", 2000,
            [&]
            {
                pump->callback(output);
            });
    std::atomic<bool> stop{false};
    std::thread control(
        [&]
        {
            while (!stop.load())
            {
                auto c = audio.createClip(pcm);
                audio.unloadClip(c);
                audio.update(0);
            }
        });
    measure("audio_contention", 2000,
            [&]
            {
                pump->callback(output);
            });
    stop = true;
    control.join();
}
