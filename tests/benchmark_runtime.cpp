#include "GoldenScene.h"
#include "RuntimeAudioDevice.h"
#include "aura/Core/Camera/Camera.h"
#include "aura/Core/Engine.h"
#include "aura/Core/Files.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/UI/UI.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <thread>

#include <glm/gtc/matrix_transform.hpp>
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
        const bool raster = argc > 2 && std::string_view(argv[2]) == "raster";
        config.set(setting::rendererApi, std::string{argv[1]})
            .set(setting::vulkanValidation, std::getenv("AURA_BENCH_VALIDATION") != nullptr)
            .set(setting::windowWidth, raster ? 1920 : 320)
            .set(setting::windowHeight, raster ? 1080 : 240)
            .set(setting::presentMode, VSyncMode::Uncapped)
            .set(setting::presentFpsLimit, 0)
            .set(setting::rendererCpuThreads, raster ? 0 : 4)
            .set(setting::vulkanGpu, std::string{"any"})
            .set(setting::audioBackend, wma::AudioBackend::Null)
            .set(setting::loggingLevel, ink::LogLevel::ERROR);
        //! golden: every drawMeshes() chunk on a worker, unless AURA_BENCH_SERIAL records them all inline,
        //! so two runs prove parallel recording draws exactly what serial recording does.
        if (argc > 2 && std::string_view(argv[2]) == "golden")
        {
            const bool serial = std::getenv("AURA_BENCH_SERIAL") != nullptr;
            config.set(setting::vulkanMinDrawsToThread, serial ? 1000000 : 1)
                .set(setting::vulkanMinDrawsPerChunk, serial ? 1000000 : 4);
        }
        // AURA_BENCH_WINDOW=sdl3|x11|wayland|glfw runs any mode through that window backend.
        if (const char *window = std::getenv("AURA_BENCH_WINDOW"))
        {
            wma::WindowBackend backend{};
            if (!WindowBackendFromString(window, backend))
                return 1;
            config.set(setting::windowBackend, backend);
        }
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
            // blending where a translucent line's two triangles meet. Every
            // backend blends in linear and stores sRGB: a 50% blend reads 188.
            const std::array probes{
                Probe{48, 24, {255, 0, 0}}, Probe{15, 24, {0, 0, 0}}, Probe{96, 24, {0, 0, 0}},
                Probe{48, 19, {0, 0, 0}}, Probe{48, 28, {0, 0, 0}}, Probe{128, 40, {0, 255, 0}},
                Probe{128, 15, {0, 0, 0}}, Probe{128, 72, {0, 0, 0}}, Probe{200, 40, {0, 0, 255}},
                Probe{193, 47, {0, 0, 0}}, Probe{48, 56, {0, 255, 255}}, Probe{15, 56, {0, 0, 0}},
                Probe{96, 56, {0, 0, 0}}, Probe{48, 95, {188, 188, 188}}, Probe{48, 96, {188, 188, 188}},
                Probe{200, 112, {188, 0, 188}}, Probe{48, 120, {188, 188, 0}}, Probe{32, 160, {255, 255, 0}},
                Probe{15, 160, {0, 0, 0}}, Probe{96, 160, {0, 0, 0}}, Probe{136, 152, {255, 0, 255}},
                Probe{172, 184, {0, 0, 0}}, Probe{248, 152, {0, 255, 255}}, Probe{284, 184, {0, 0, 0}},
                Probe{280, 40, {188, 0, 188}}, Probe{280, 20, {0, 0, 188}}, Probe{264, 40, {255, 0, 0}},
                // Scene depth: the mesh hides the red batch triangle behind it, the
                // blue 3D line in front of it shows, and the triangle shows beside it.
                Probe{180, 215, {0, 255, 0}}, Probe{155, 215, {255, 0, 0}}, Probe{185, 222, {0, 0, 255}},
                Probe{210, 215, {0, 0, 0}},
                // A second camera in the same pass moves only the mesh drawn after it.
                Probe{285, 215, {0, 255, 0}}, Probe{265, 215, {0, 0, 0}}};
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
            renderer.setLights(std::array{gfx::Light{.type = gfx::LightType::Ambient}});
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
            //! Built once and drawn every frame; each is one draw call.
            gfx::Canvas behind;
            behind.triangle(glm::vec3{150, 200, -0.5f}, glm::vec3{230, 200, -0.5f}, glm::vec3{150, 236, -0.5f},
                            {1, 0, 0, 1});
            gfx::Canvas shapes;
            shapes.line({16, 24}, {96, 24}, {1, 0, 0, 1}, 8);
            for (const Segment &segment : segments)
                shapes.line(segment.from, segment.to, segment.color, 8);
            shapes.rect({16, 144}, {80, 48}, {1, 1, 0, 1});
            shapes.triangle(glm::vec2{128, 144}, glm::vec2{176, 144}, glm::vec2{128, 192}, {1, 0, 1, 1});
            shapes.triangle(glm::vec2{240, 192}, glm::vec2{288, 144}, glm::vec2{240, 144}, {0, 1, 1, 1});
            shapes.rect({256, 24}, {48, 48}, {1, 0, 0, 1});
            shapes.line({280, 16}, {280, 80}, {0, 0, 1, .5f}, 16);
            gfx::Canvas inFront;
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
                renderer.drawBatch(behind, {}, gfx::BatchSpace::World);
                //! Projected with this frame's camera, so rebuilt each frame.
                inFront.clear();
                inFront.line(renderer.canvasView(), glm::vec3{172, 222, 0.9f}, glm::vec3{198, 222, 0.9f}, {0, 0, 1, 1},
                             2);
                renderer.drawBatch(inFront);
                // Primitives must select the white fallback after a coverage draw.
                renderer.drawBatch(textured, texturedIndices, emptyCoverage);
                renderer.drawBatch(shapes);
                glm::mat4 shifted{1.0f};
                shifted[3].x = 100.0f;
                renderer.setTransform({glm::mat4{1.0f}, shifted, pixels});
                renderer.drawMesh(occluderMesh, white);
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
                            std::fprintf(stderr, "PRIMITIVES frame %u pixel (%d,%d): got %u,%u,%u expected %u,%u,%u\n",
                                         frame, probe.x, probe.y, actual[0], actual[1], actual[2], probe.rgb[0],
                                         probe.rgb[1], probe.rgb[2]);
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
            std::printf("PRIMITIVES backend=%s rendered_frames=%u checked_pixels=%u\n",
                        RendererChoiceToString(renderer.getBackendType()), rendered, checkedPixels);
            return rendered == 12 ? 0 : 1;
        }
        if (argc > 2 && std::string_view(argv[2]) == "normalmap")
        {
            //! A camera-facing plane lit from the side: its normal-mapped half ripples, its plain half is even.
            if (!INK_FLAG_CHECK(renderer.caps(), RendererCaps::Pbr))
            {
                std::printf("NORMALMAP backend=%s skipped (no PBR)\n",
                            RendererChoiceToString(renderer.getBackendType()));
                return 0;
            }
            const RenderTargetHandle target = renderer.createRenderTarget({.width = 64, .height = 32});
            std::vector<u8> ridges(usize{32} * 32 * 4);
            for (u32 i = 0; i < 32 * 32; ++i)
            {
                const f32 tilt = std::sin(static_cast<f32>(i % 32) / 32.0f * 6.2831853f * 4.0f) * 0.8f;
                const glm::vec3 n = glm::normalize(glm::vec3{tilt, 0.0f, 1.0f});
                ridges[i * 4 + 0] = static_cast<u8>(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
                ridges[i * 4 + 1] = 128;
                ridges[i * 4 + 2] = static_cast<u8>(std::lround((n.z * 0.5f + 0.5f) * 255.0f));
                ridges[i * 4 + 3] = 255;
            }
            const TextureHandle white = renderer.createSolidColorTexture(255, 255, 255);
            const TextureHandle normals = renderer.createTexture(
                {.width = 32, .height = 32, .format = TextureFormat::Rgba8Unorm}, std::as_bytes(std::span(ridges)));
            const MaterialHandle mapped = renderer.createMaterial({.albedo = white, .normalMap = normals});
            const MaterialHandle plain = renderer.createMaterial({.albedo = white});
            gfx::Mesh3D quad;
            for (const glm::vec2 corner : {glm::vec2{0, 0}, glm::vec2{1, 0}, glm::vec2{1, 1}, glm::vec2{0, 1}})
                quad.vertices.push_back({{corner, 0.0f}, corner, {1, 1, 1, 1}, {0, 0, 1}});
            quad.indices = {0, 1, 2, 2, 3, 0};
            const MeshHandle mesh = renderer.createMesh(std::move(quad));
            const bool zeroToOne = renderer.getBackendType() == RendererChoice::VULKAN ||
                                   renderer.getBackendType() == RendererChoice::METAL;
            const glm::mat4 proj = zeroToOne ? glm::orthoRH_ZO(0.0f, 2.0f, 0.0f, 1.0f, 0.1f, 10.0f)
                                             : glm::orthoRH_NO(0.0f, 2.0f, 0.0f, 1.0f, 0.1f, 10.0f);
            const glm::mat4 view = glm::lookAt(glm::vec3{0, 0, 1}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
            renderer.setLights(std::array{gfx::Light{.direction = glm::normalize(glm::vec3{1, 0, -0.6f})}});
            for (u32 frame = 0; frame < 2; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                renderer.beginRenderPass(target);
                renderer.setTransform({glm::mat4{1.0f}, view, proj});
                const std::array<IRenderer::DrawItem, 2> halves{
                    IRenderer::DrawItem{mesh, mapped, glm::mat4{1.0f}},
                    IRenderer::DrawItem{mesh, plain, glm::translate(glm::mat4{1.0f}, {1, 0, 0})}};
                renderer.drawMeshes(halves);
                renderer.endRenderPass();
                renderer.endFrame();
            }
            std::vector<u8> pixels(usize{64} * 32 * 4);
            if (!renderer.readPixels(target, pixels))
                return 1;
            const auto range = [&](int first, int last)
            {
                int low = 255, high = 0;
                for (int x = first; x < last; ++x)
                {
                    const int value = pixels[(usize{16} * 64 + static_cast<usize>(x)) * 4];
                    low = std::min(low, value);
                    high = std::max(high, value);
                }
                return high - low;
            };
            const int rippled = range(2, 30), even = range(34, 62);
            std::printf("NORMALMAP backend=%s mapped_range=%d plain_range=%d %s\n",
                        RendererChoiceToString(renderer.getBackendType()), rippled, even,
                        rippled > 40 && even < 4 ? "ok" : "MISMATCH");
            return rippled > 40 && even < 4 ? 0 : 2;
        }
        if (argc > 2 && std::string_view(argv[2]) == "pipeline")
        {
            //! tests/shaders/*: your own shaders through createPipeline(). Constants, a texture and a uniform buffer
            //! land in separate channels; a clockwise triangle is culled; a render target is read back by a
            //! vertex-less pass; and a depth target is sampled like a shadow map.
            if (!INK_FLAG_CHECK(renderer.caps(), RendererCaps::CustomPipelines))
            {
                std::printf("PIPELINE backend=%s skipped (no custom pipelines)\n",
                            RendererChoiceToString(renderer.getBackendType()));
                return 0;
            }
            const auto make = [&](const char *vert, const char *frag, PipelineDesc desc)
            {
                const std::filesystem::path spirvDir = AURA_TEST_SPIRV_DIR, sourceDir = AURA_TEST_SHADER_DIR;
                const auto vertexSpirv = readFileBytes(spirvDir / (std::string(vert) + ".spv"));
                const auto fragmentSpirv = readFileBytes(spirvDir / (std::string(frag) + ".spv"));
                const auto glsl = [&](const char *name)
                {
                    const auto source = readFileBytes(sourceDir / name);
                    std::string text =
                        source ? std::string(reinterpret_cast<const char *>(source->data()), source->size()) : "";
                    text.replace(0, text.find('\n'), "#version 330 core");
                    return text;
                };
                if (vertexSpirv && fragmentSpirv)
                {
                    desc.vertexSpirv = *vertexSpirv;
                    desc.fragmentSpirv = *fragmentSpirv;
                }
                desc.vertexGlsl = glsl(vert);
                desc.fragmentGlsl = glsl(frag);
                const auto msl = [&](const char *name)
                {
                    const auto source = readFileBytes(sourceDir / (std::string(name) + ".metal"));
                    return source ? std::string(reinterpret_cast<const char *>(source->data()), source->size())
                                  : std::string{};
                };
                desc.vertexMsl = msl(vert);
                desc.fragmentMsl = msl(frag);
                return renderer.createPipeline(desc);
            };
            struct Constants
            {
                glm::mat4 viewProj;
                glm::vec4 tint;
            };
            constexpr PipelineState kSolid = PipelineState::DepthTest | PipelineState::DepthWrite;
            const PipelineHandle lit = make("pipeline.vert", "pipeline.frag",
                                            {.state = kSolid, .textures = 1, .buffers = 1, .constantBytes = 80});
            const PipelineHandle culling = make(
                "pipeline.vert", "pipeline.frag",
                {.state = kSolid | PipelineState::CullBackFaces, .textures = 1, .buffers = 1, .constantBytes = 80});
            const PipelineHandle depthOnly =
                make("pipeline.vert", "depth.frag", {.state = kSolid, .constantBytes = 80});
            const PipelineHandle shade =
                make("pipeline.vert", "shade.frag", {.state = kSolid, .textures = 1, .constantBytes = 80});
            const PipelineHandle invert =
                make("fullscreen.vert", "invert.frag",
                     {.state = PipelineState::None, .input = VertexInput::None, .textures = 1, .constantBytes = 16});
            if (!isValidHandle(lit) || !isValidHandle(culling) || !isValidHandle(depthOnly) || !isValidHandle(shade) ||
                !isValidHandle(invert))
            {
                std::printf("PIPELINE backend=%s a pipeline was rejected\n",
                            RendererChoiceToString(renderer.getBackendType()));
                return 1;
            }

            const bool zeroToOne = renderer.getBackendType() == RendererChoice::VULKAN ||
                                   renderer.getBackendType() == RendererChoice::METAL;
            const f32 z = zeroToOne ? 0.5f : 0.0f; //! Stores 0.5 as depth on either clip convention.
            const auto quad = [](glm::vec2 min, glm::vec2 max, f32 depth)
            {
                //! Top-left first; uv as a render target's texture is read: top row first.
                const auto uv = [](glm::vec2 p)
                {
                    return glm::vec2{p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f};
                };
                std::vector<gfx::Vertex3D> vertices;
                for (const glm::vec2 corner : {glm::vec2{min.x, max.y}, glm::vec2{max.x, max.y},
                                               glm::vec2{max.x, min.y}, glm::vec2{min.x, min.y}})
                    vertices.push_back({{corner, depth}, uv(corner), {1, 1, 1, 1}, {0, 0, 1}});
                return gfx::Mesh3D{std::move(vertices), {0, 1, 2, 0, 2, 3}};
            };
            const MeshHandle full = renderer.createMesh(quad({-1, -1}, {1, 1}, z));
            const MeshHandle topLeft = renderer.createMesh(quad({-1, 0}, {0, 1}, z));
            //! Counter-clockwise, which the built-in shaders keep: they cull back faces.
            gfx::Mesh3D facing = quad({-1, -1}, {1, 1}, z);
            facing.indices = {0, 2, 1, 0, 3, 2};
            const MeshHandle builtinQuad = renderer.createMesh(std::move(facing));
            //! One counter-clockwise triangle on the left, one clockwise on the right.
            const auto corner = [](f32 x, f32 y)
            {
                return gfx::Vertex3D{{x, y, 0.0f}, {0, 0}, {1, 1, 1, 1}, {0, 0, 1}};
            };
            const MeshHandle triangles =
                renderer.createMesh(gfx::Mesh3D{{corner(-0.9f, -0.5f), corner(-0.1f, -0.5f), corner(-0.5f, 0.5f),
                                                 corner(0.1f, -0.5f), corner(0.5f, 0.5f), corner(0.9f, -0.5f)},
                                                {0, 1, 2, 3, 4, 5}});

            const std::array<u8, 4> pixel{0, 0, 128, 255};
            const TextureHandle texture =
                renderer.createTexture({.width = 1, .height = 1, .format = TextureFormat::Rgba8Unorm, .mipmaps = false},
                                       std::as_bytes(std::span(pixel)));
            const std::array<glm::vec4, 4> palette{{{0, 0, 0, 0}, {0, 0, 0.5f, 0}, {0, 0, 0.25f, 0}, {0, 0, 0, 0}}};
            const BufferHandle buffer = renderer.createBuffer(sizeof(palette), std::as_bytes(std::span(palette)));

            constexpr u32 kSize = 64;
            const RenderTargetHandle depthMap =
                renderer.createRenderTarget({.width = kSize, .height = kSize, .format = TextureFormat::Depth});
            const auto colourTarget = [&]
            {
                return renderer.createRenderTarget(
                    {.width = kSize, .height = kSize, .format = TextureFormat::Rgba8Unorm});
            };
            const RenderTargetHandle builtinMap =
                renderer.createRenderTarget({.width = kSize, .height = kSize, .format = TextureFormat::Depth});
            const RenderTargetHandle gradient = colourTarget(), culled = colourTarget(), shaded = colourTarget(),
                                     inverted = colourTarget(), shadedBuiltin = colourTarget();
            if (!isValidHandle(depthMap) && renderer.getBackendType() == RendererChoice::METAL)
            {
                std::printf("PIPELINE backend=METAL skipped (no depth targets yet)\n");
                return 0;
            }
            if (!isValidHandle(buffer) || !isValidHandle(depthMap) || !isValidHandle(builtinMap) ||
                !isValidHandle(gradient) || !isValidHandle(culled) || !isValidHandle(shaded) ||
                !isValidHandle(inverted) || !isValidHandle(shadedBuiltin))
                return 1;

            const auto bytes = [](const auto &value)
            {
                return std::as_bytes(std::span(&value, 1));
            };
            //! Every pass but the window's draws into a target, where OpenGL wants its y flipped.
            const std::array litTextures{texture};
            const std::array litBuffers{buffer};
            const glm::mat4 correction = renderer.clipCorrection(gradient);
            const Constants constants{correction, {1.0f, 0.5f, 0.0f, 1.0f}};
            const Constants windowConstants{renderer.clipCorrection(), {1.0f, 0.5f, 0.0f, 1.0f}};
            const glm::vec4 flip{correction[1][1], 0, 0, 0};
            for (u32 frame = 0; frame < 3; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    return 1;
                renderer.setClearColor(0, 0, 0, 1);

                renderer.beginRenderPass(depthMap);
                renderer.drawPipeline({.pipeline = depthOnly, .mesh = topLeft, .constants = bytes(constants)});
                renderer.endRenderPass();

                renderer.beginRenderPass(gradient);
                renderer.drawPipeline({.pipeline = lit,
                                       .mesh = full,
                                       .constants = bytes(constants),
                                       .textures = litTextures,
                                       .buffers = litBuffers});
                renderer.endRenderPass();

                renderer.beginRenderPass(culled);
                renderer.drawPipeline({.pipeline = culling,
                                       .mesh = triangles,
                                       .constants = bytes(constants),
                                       .textures = litTextures,
                                       .buffers = litBuffers});
                renderer.endRenderPass();

                renderer.beginRenderPass(shaded);
                renderer.drawPipeline({.pipeline = shade,
                                       .mesh = full,
                                       .constants = bytes(constants),
                                       .textures = std::array{renderer.renderTargetTexture(depthMap)}});
                renderer.endRenderPass();

                //! The built-in shaders draw into a depth map too, writing depth alone.
                renderer.beginRenderPass(builtinMap);
                renderer.setTransform({glm::mat4{1.0f}, glm::mat4{1.0f}, glm::mat4{1.0f}});
                renderer.drawMesh(builtinQuad);
                renderer.endRenderPass();

                renderer.beginRenderPass(shadedBuiltin);
                renderer.drawPipeline({.pipeline = shade,
                                       .mesh = full,
                                       .constants = bytes(constants),
                                       .textures = std::array{renderer.renderTargetTexture(builtinMap)}});
                renderer.endRenderPass();

                renderer.beginRenderPass(inverted);
                renderer.drawPipeline({.pipeline = invert,
                                       .vertexCount = 3,
                                       .constants = bytes(flip),
                                       .textures = std::array{renderer.renderTargetTexture(gradient)}});
                renderer.endRenderPass();

                //! The window's pass takes the same pipelines, whatever its sample count.
                renderer.beginRenderPass();
                renderer.drawPipeline({.pipeline = lit,
                                       .mesh = full,
                                       .constants = bytes(windowConstants),
                                       .textures = litTextures,
                                       .buffers = litBuffers});
                renderer.endRenderPass();
                renderer.endFrame();
            }

            const auto read = [&](RenderTargetHandle target)
            {
                std::vector<u8> pixels(usize{kSize} * kSize * 4);
                return renderer.readPixels(target, pixels) ? pixels : std::vector<u8>{};
            };
            const std::vector<u8> grad = read(gradient), cull = read(culled), shadow = read(shaded),
                                  inv = read(inverted), builtin = read(shadedBuiltin);
            if (grad.empty() || cull.empty() || shadow.empty() || inv.empty() || builtin.empty())
                return 1;
            const auto at = [](const std::vector<u8> &pixels, u32 x, u32 y, u32 channel)
            {
                return static_cast<int>(pixels[(usize{y} * kSize + x) * 4 + channel]);
            };
            const auto near = [](int a, int b, int tolerance)
            {
                return std::abs(a - b) <= tolerance;
            };
            //! The uv gradient reads top-left to bottom-right, scaled by the constants; blue is the texture and buffer.
            const bool gradientOk = near(at(grad, 8, 8, 0), 34, 5) && near(at(grad, 8, 8, 1), 17, 5) &&
                                    near(at(grad, 56, 56, 0), 221, 5) && near(at(grad, 56, 56, 1), 110, 5) &&
                                    near(at(grad, 8, 8, 2), 128, 3) && near(at(grad, 56, 56, 2), 128, 3);
            //! Culling: the counter-clockwise triangle is drawn, the clockwise one is not.
            const bool cullOk = at(cull, 16, 32, 2) > 100 && at(cull, 48, 32, 2) == 0;
            //! The depth map's top-left quadrant holds 0.5; the rest is cleared to 1.
            const bool shadowOk = near(at(shadow, 16, 16, 0), 128, 3) && at(shadow, 16, 16, 1) == 255 &&
                                  at(shadow, 16, 48, 0) == 255 && at(shadow, 16, 48, 1) == 0 &&
                                  at(shadow, 48, 16, 0) == 255 && at(shadow, 48, 16, 1) == 0;
            bool invertOk = true;
            for (const u32 x : {8u, 56u})
                for (const u32 y : {8u, 56u})
                    for (u32 channel = 0; channel < 3; ++channel)
                        invertOk = invertOk && near(at(inv, x, y, channel) + at(grad, x, y, channel), 255, 4);
            //! The built-in quad filled the whole depth map at 0.5.
            const bool builtinOk = near(at(builtin, 32, 32, 0), 128, 3) && at(builtin, 32, 32, 1) == 255;

            //! More bindings than any fixed cap would have allowed, as far as the device goes, and one past it refused.
            const PipelineLimits limits = renderer.pipelineLimits();
            constexpr u32 kManyTextures = 10, kManyBuffers = 6;
            bool manyOk = limits.textures >= kManyTextures && limits.buffers >= kManyBuffers;
            if (manyOk)
            {
                const PipelineHandle many =
                    make("pipeline.vert", "many.frag",
                         {.state = kSolid, .textures = kManyTextures, .buffers = kManyBuffers, .constantBytes = 80});
                std::vector<TextureHandle> manyTextures;
                std::vector<BufferHandle> manyBuffers;
                for (u32 i = 0; i < kManyTextures; ++i)
                {
                    const std::array<u8, 4> red{static_cast<u8>(10 + 10 * i), 0, 0, 255};
                    manyTextures.push_back(renderer.createTexture(
                        {.width = 1, .height = 1, .format = TextureFormat::Rgba8Unorm, .mipmaps = false},
                        std::as_bytes(std::span(red))));
                }
                for (u32 i = 0; i < kManyBuffers; ++i)
                {
                    const glm::vec4 value{0.02f * static_cast<f32>(i + 1), 0, 0, 0};
                    manyBuffers.push_back(renderer.createBuffer(sizeof(value), std::as_bytes(std::span(&value, 1))));
                }
                const RenderTargetHandle target = colourTarget();
                for (u32 frame = 0; frame < 2 && isValidHandle(many) && isValidHandle(target); ++frame)
                {
                    renderer.beginFrame();
                    if (!renderer.frameBegun())
                        return 1;
                    renderer.setClearColor(0, 0, 0, 1);
                    renderer.beginRenderPass(target);
                    renderer.drawPipeline({.pipeline = many,
                                           .mesh = full,
                                           .constants = bytes(constants),
                                           .textures = manyTextures,
                                           .buffers = manyBuffers});
                    renderer.endRenderPass();
                    renderer.endFrame();
                }
                //! The textures' mean is 55/255 and the blocks' sum 0.42.
                const std::vector<u8> sums =
                    isValidHandle(many) && isValidHandle(target) ? read(target) : std::vector<u8>{};
                manyOk = !sums.empty() && near(at(sums, 32, 32, 0), 55, 2) && near(at(sums, 32, 32, 1), 107, 2);
            }
            const bool refusedOk = !isValidHandle(
                make("pipeline.vert", "pipeline.frag", {.state = kSolid, .textures = limits.textures + 1}));
            const bool ok = gradientOk && cullOk && shadowOk && invertOk && builtinOk && manyOk && refusedOk;
            std::printf("PIPELINE backend=%s gradient=%s cull=%s depth=%s vertexless=%s builtin_depth=%s many=%s "
                        "refused=%s limits=%u,%u,%u,%zu %s\n",
                        RendererChoiceToString(renderer.getBackendType()), gradientOk ? "ok" : "BAD",
                        cullOk ? "ok" : "BAD", shadowOk ? "ok" : "BAD", invertOk ? "ok" : "BAD",
                        builtinOk ? "ok" : "BAD", manyOk ? "ok" : "BAD", refusedOk ? "ok" : "BAD", limits.textures,
                        limits.buffers, limits.constantBytes, limits.bufferBytes, ok ? "ok" : "MISMATCH");
            if (!ok)
                std::printf("  gradient (8,8)=%d,%d,%d (56,56)=%d,%d,%d | cull left=%d,%d,%d right=%d,%d,%d | shade "
                            "(16,16)=%d,%d (16,48)=%d,%d (48,16)=%d,%d\n",
                            at(grad, 8, 8, 0), at(grad, 8, 8, 1), at(grad, 8, 8, 2), at(grad, 56, 56, 0),
                            at(grad, 56, 56, 1), at(grad, 56, 56, 2), at(cull, 16, 32, 0), at(cull, 16, 32, 1),
                            at(cull, 16, 32, 2), at(cull, 48, 32, 0), at(cull, 48, 32, 1), at(cull, 48, 32, 2),
                            at(shadow, 16, 16, 0), at(shadow, 16, 16, 1), at(shadow, 16, 48, 0), at(shadow, 16, 48, 1),
                            at(shadow, 48, 16, 0), at(shadow, 48, 16, 1));
            return ok ? 0 : 2;
        }
        if (argc > 2 && std::string_view(argv[2]) == "compute")
        {
            //! tests/shaders/grid.comp fills instances and the draw's count on the GPU; one indirect draw shows them.
            if (!INK_FLAG_CHECK(renderer.caps(), RendererCaps::Compute))
            {
                std::printf("COMPUTE backend=%s skipped (no compute)\n",
                            RendererChoiceToString(renderer.getBackendType()));
                return 0;
            }
            struct Constants
            {
                u32 count;
                u32 columns;
                f32 spacing;
                f32 scale;
            };
            const Constants constants{16, 4, 0.5f, 0.15f};
            const auto spirv = readFileBytes(std::filesystem::path(AURA_TEST_SPIRV_DIR) / "grid.comp.spv");
            const auto source = readFileBytes(std::filesystem::path(AURA_TEST_SHADER_DIR) / "grid.comp");
            std::string glsl =
                source ? std::string(reinterpret_cast<const char *>(source->data()), source->size()) : "";
            glsl.replace(0, glsl.find('\n'), "#version 430");
            ComputeDesc desc{.buffers = 2, .glsl = glsl};
            if (spirv)
                desc.spirv = *spirv;
            desc.msl = R"(#include <metal_stdlib>
using namespace metal;
struct Instance { float4x4 model; float4x4 normal; };
struct DrawArgs { uint indexCount; atomic_uint instanceCount; uint firstIndex; int vertexOffset; uint firstInstance; };
struct Constants { uint count; uint columns; float spacing; float scale; };
kernel void grid(device Instance *instances [[buffer(0)]], device DrawArgs &args [[buffer(1)]],
                 constant Constants &constants [[buffer(2)]], uint i [[thread_position_in_grid]])
{
    if (i >= constants.count || (i & 1u) != 0u)
        return;
    const uint slot = atomic_fetch_add_explicit(&args.instanceCount, 1u, memory_order_relaxed);
    const float middle = float(constants.columns - 1u) * 0.5f;
    const float2 at = (float2(float(i % constants.columns), float(i / constants.columns)) - middle) * constants.spacing;
    float4x4 model = float4x4(constants.scale);
    model[3] = float4(at, 0.0f, 1.0f);
    instances[slot].model = model;
    instances[slot].normal = float4x4(1.0f);
})";
            desc.mslEntry = "grid";
            desc.cpu = [](const ComputeInvocation &invocation)
            {
                Constants c{};
                std::memcpy(&c, invocation.constants.data(), sizeof(c));
                auto *instances = reinterpret_cast<InstanceTransform *>(invocation.buffers[0].data());
                auto *args = reinterpret_cast<DrawIndexedIndirect *>(invocation.buffers[1].data());
                for (u32 i = invocation.group.x * 64; i < (invocation.group.x + 1) * 64 && i < c.count; ++i)
                {
                    if ((i & 1u) != 0u)
                        continue;
                    const u32 slot = std::atomic_ref<u32>(args->instanceCount).fetch_add(1);
                    const f32 half = static_cast<f32>(c.columns - 1) * 0.5f;
                    const u32 column = i % c.columns, row = i / c.columns;
                    const glm::vec2 at =
                        (glm::vec2(static_cast<f32>(column), static_cast<f32>(row)) - half) * c.spacing;
                    glm::mat4 model{c.scale};
                    model[3] = glm::vec4(at, 0.0f, 1.0f);
                    instances[slot] = {model, glm::mat4{1.0f}};
                }
            };
            const ComputeHandle kernel = renderer.createCompute(desc);
            const MeshHandle cube = renderer.createMesh(MeshLoader::createCube());
            const DrawIndexedIndirect empty{.indexCount = renderer.meshIndexCount(cube)};
            const BufferHandle instances = renderer.createBuffer(constants.count * sizeof(InstanceTransform));
            const BufferHandle arguments = renderer.createBuffer(sizeof(empty), std::as_bytes(std::span(&empty, 1)));
            const RenderTargetHandle target = renderer.createRenderTarget({.width = 64, .height = 64});
            const MaterialHandle white =
                renderer.createMaterial({.albedo = renderer.createSolidColorTexture(255, 255, 255)});
            const glm::mat4 view = glm::lookAt(glm::vec3{0, 0, 2}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
            const bool zeroToOne = renderer.getBackendType() == RendererChoice::VULKAN ||
                                   renderer.getBackendType() == RendererChoice::METAL;
            const glm::mat4 proj = zeroToOne ? glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f)
                                             : glm::orthoRH_NO(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
            renderer.setLights(std::array{gfx::Light{.direction = {0, 0, -1}}});
            for (u32 frame = 0; frame < 3; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                //! The count starts from zero every frame; the kernel adds to it.
                renderer.updateBuffer(arguments, 0, std::as_bytes(std::span(&empty, 1)));
                const std::array<BufferHandle, 2> bound{instances, arguments};
                renderer.dispatch(kernel, bound, {(constants.count + 63) / 64, 1, 1},
                                  std::as_bytes(std::span(&constants, 1)));
                renderer.setClearColor(0, 0, 0, 1);
                renderer.beginRenderPass(target);
                renderer.setTransform({glm::mat4{1.0f}, view, proj});
                renderer.drawMeshInstancedIndirect(cube, white, instances, arguments);
                renderer.endRenderPass();
                renderer.endFrame();
            }
            DrawIndexedIndirect written{};
            renderer.readBuffer(arguments, 0, std::as_writable_bytes(std::span(&written, 1)));
            std::vector<u8> pixels(usize{64} * 64 * 4);
            renderer.readPixels(target, pixels);
            //! Cell (0, 0) at pixel (8, 56) is even and drawn; cell (1, 0) at (24, 56) is odd and empty.
            const int even = pixels[(usize{56} * 64 + 8) * 4], odd = pixels[(usize{56} * 64 + 24) * 4];
            const bool ok =
                isValidHandle(kernel) && written.instanceCount == constants.count / 2 && even > 100 && odd < 16;
            std::printf("COMPUTE backend=%s instances=%u even=%d odd=%d %s\n",
                        RendererChoiceToString(renderer.getBackendType()), written.instanceCount, even, odd,
                        ok ? "ok" : "MISMATCH");
            return ok ? 0 : 2;
        }
        if (argc > 3 && std::string_view(argv[2]) == "reload")
        {
            //! benchmark_runtime <backend> reload <shaders dir>: a lit quad before and after reloadShaders().
            const RenderTargetHandle target = renderer.createRenderTarget({.width = 16, .height = 16});
            const MaterialHandle white =
                renderer.createMaterial({.albedo = renderer.createSolidColorTexture(255, 255, 255)});
            gfx::Mesh3D quad;
            for (const glm::vec2 corner : {glm::vec2{-1, -1}, glm::vec2{1, -1}, glm::vec2{1, 1}, glm::vec2{-1, 1}})
                quad.vertices.push_back({{corner, 0.0f}, corner, {1, 1, 1, 1}, {0, 0, 1}});
            quad.indices = {0, 1, 2, 2, 3, 0};
            const MeshHandle mesh = renderer.createMesh(std::move(quad));
            const glm::mat4 view = glm::lookAt(glm::vec3{0, 0, 1}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
            const bool zeroToOne = renderer.getBackendType() == RendererChoice::VULKAN ||
                                   renderer.getBackendType() == RendererChoice::METAL;
            const glm::mat4 proj = zeroToOne ? glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f)
                                             : glm::orthoRH_NO(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
            renderer.setLights(std::array{gfx::Light{.direction = {0, 0, -1}, .intensity = 0.5f}});
            const auto centre = [&]
            {
                renderer.beginFrame();
                if (renderer.frameBegun())
                {
                    renderer.beginRenderPass(target);
                    renderer.setTransform({glm::mat4{1.0f}, view, proj});
                    const std::array<IRenderer::DrawItem, 1> items{IRenderer::DrawItem{mesh, white, glm::mat4{1.0f}}};
                    renderer.drawMeshes(items);
                    renderer.endRenderPass();
                    renderer.endFrame();
                }
                std::vector<u8> pixels(usize{16} * 16 * 4);
                renderer.readPixels(target, pixels);
                const usize at = (usize{8} * 16 + 8) * 4;
                return glm::ivec3{pixels[at], pixels[at + 1], pixels[at + 2]};
            };
            const glm::ivec3 before = centre();
            const bool reloaded = renderer.reloadShaders(argv[3]);
            const glm::ivec3 after = centre();
            std::printf("RELOAD backend=%s reloaded=%d before=%d,%d,%d after=%d,%d,%d\n",
                        RendererChoiceToString(renderer.getBackendType()), reloaded ? 1 : 0, before.x, before.y,
                        before.z, after.x, after.y, after.z);
            return 0;
        }
        if (argc > 2 && std::string_view(argv[2]) == "update")
        {
            //! A quad created on the left half, moved by updateMesh() to the right half every frame after the first.
            const RenderTargetHandle target = renderer.createRenderTarget({.width = 32, .height = 16});
            const MaterialHandle white =
                renderer.createMaterial({.albedo = renderer.createSolidColorTexture(255, 255, 255)});
            const auto quad = [](f32 left)
            {
                std::vector<gfx::Vertex3D> vertices;
                for (const glm::vec2 corner : {glm::vec2{0, 0}, glm::vec2{1, 0}, glm::vec2{1, 1}, glm::vec2{0, 1}})
                    vertices.push_back({{left + corner.x, corner.y, 0.0f}, corner, {1, 1, 1, 1}, {0, 0, 1}});
                return vertices;
            };
            const MeshHandle mesh = renderer.createMesh(gfx::Mesh3D{quad(0.0f), {0, 1, 2, 2, 3, 0}});
            const bool zeroToOne = renderer.getBackendType() == RendererChoice::VULKAN ||
                                   renderer.getBackendType() == RendererChoice::METAL;
            const glm::mat4 proj = zeroToOne ? glm::orthoRH_ZO(0.0f, 2.0f, 0.0f, 1.0f, 0.1f, 10.0f)
                                             : glm::orthoRH_NO(0.0f, 2.0f, 0.0f, 1.0f, 0.1f, 10.0f);
            const glm::mat4 view = glm::lookAt(glm::vec3{0, 0, 1}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
            renderer.setLights(std::array{gfx::Light{.direction = {0, 0, -1}}});
            bool updated = true;
            for (u32 frame = 0; frame < 3; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                if (frame > 0)
                    updated = renderer.updateMesh(mesh, quad(1.0f)) && updated;
                renderer.setClearColor(0, 0, 0, 1);
                renderer.beginRenderPass(target);
                renderer.setTransform({glm::mat4{1.0f}, view, proj});
                const std::array<IRenderer::DrawItem, 1> items{IRenderer::DrawItem{mesh, white, glm::mat4{1.0f}}};
                renderer.drawMeshes(items);
                renderer.endRenderPass();
                renderer.endFrame();
            }
            std::vector<u8> pixels(usize{32} * 16 * 4);
            if (!renderer.readPixels(target, pixels))
                return 1;
            const int left = pixels[(usize{8} * 32 + 8) * 4], right = pixels[(usize{8} * 32 + 24) * 4];
            const bool ok = updated && left < 16 && right > 128;
            std::printf("UPDATE backend=%s left=%d right=%d %s\n", RendererChoiceToString(renderer.getBackendType()),
                        left, right, ok ? "ok" : "MISMATCH");
            return ok ? 0 : 2;
        }
        if (argc > 2 && std::string_view(argv[2]) == "golden")
        {
            //! benchmark_runtime <backend> golden [reference.ppm]; AURA_BENCH_DUMP=<file.ppm> saves the readback.
            const test::GoldenScene scene = test::buildGoldenScene(renderer);
            if (!isValidHandle(scene.frame))
                return 1;
            u32 rendered = 0;
            for (u32 frame = 0; frame < 3; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                test::drawGoldenScene(renderer, scene, std::getenv("AURA_BENCH_INSTANCED") != nullptr);
                renderer.endFrame();
                ++rendered;
            }
            std::vector<u8> rgba(static_cast<usize>(test::kGoldenSize) * test::kGoldenSize * 4);
            if (rendered == 0 || !renderer.readPixels(scene.frame, rgba))
                return 1;
            if (const char *dump = std::getenv("AURA_BENCH_DUMP"))
                test::writeGolden(dump, rgba, test::kGoldenSize, test::kGoldenSize);
            if (argc > 3)
            {
                const std::vector<u8> golden = test::readGolden(argv[3], test::kGoldenSize, test::kGoldenSize);
                if (golden.empty())
                    return 1;
                //! The reference is the software backend's: GPUs blend between mip levels where it picks one per
                //! triangle, and rasterise line edges a little differently. Measured: 1% of pixels past 32.
                const test::GoldenDiff diff = test::compareGolden(rgba, golden, 32);
                const usize allowed = test::kGoldenSize * test::kGoldenSize / 50;
                std::printf("GOLDEN backend=%s differing=%zu allowed=%zu worst=%d %s\n",
                            RendererChoiceToString(renderer.getBackendType()), diff.differing, allowed, diff.worst,
                            diff.differing <= allowed ? "ok" : "MISMATCH");
                return diff.differing <= allowed ? 0 : 2;
            }
            std::printf("GOLDEN backend=%s rendered\n", RendererChoiceToString(renderer.getBackendType()));
            return 0;
        }
        if (argc > 2 && std::string_view(argv[2]) == "targets")
        {
            //! Offscreen targets: drawn at their own size, sampled by the window pass, read back.
            const RenderTargetHandle colour = renderer.createRenderTarget({.width = 64, .height = 64});
            const RenderTargetHandle hdr = renderer.createRenderTarget(
                {.width = 32, .height = 32, .format = TextureFormat::Rgba16Float, .depth = false});
            if (!isValidHandle(colour) || !isValidHandle(hdr) || !isValidHandle(renderer.renderTargetTexture(colour)))
                return 1;
            const auto quad = [&](glm::vec2 origin, glm::vec2 size, const glm::vec4 &tint, TextureHandle texture)
            {
                const std::array<gfx::BatchVertex, 4> corners{{{{origin, 0}, {0, 0}, tint},
                                                               {{origin.x + size.x, origin.y, 0}, {1, 0}, tint},
                                                               {{origin + size, 0}, {1, 1}, tint},
                                                               {{origin.x, origin.y + size.y, 0}, {0, 1}, tint}}};
                constexpr std::array<u32, 6> indices{0, 1, 2, 0, 2, 3};
                renderer.drawBatch(corners, indices, texture);
            };
            renderer.setClearColor(0, 0, 0, 1);
            u32 rendered = 0, checked = 0;
            for (u32 frame = 0; frame < 4; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                renderer.beginRenderPass(colour);
                //! A screen batch spans the bound target: these two fill it exactly, half each.
                if (renderer.renderTargetSize() != glm::uvec2{64, 64})
                    return 3;
                quad({0, 0}, {32, 64}, {1, 0, 0, 1}, {});
                quad({32, 0}, {32, 64}, {0, 1, 0, 1}, {});
                renderer.endRenderPass();
                renderer.beginRenderPass(hdr);
                quad({0, 0}, {32, 32}, {0.5f, 0.5f, 0.5f, 1}, {});
                renderer.endRenderPass();
                renderer.beginRenderPass();
                quad({100, 100}, {64, 64}, {1, 1, 1, 1}, renderer.renderTargetTexture(colour));
                renderer.endRenderPass();
#ifdef AURA_HAS_OPENGL
                if (renderer.getBackendType() == RendererChoice::OPENGL)
                {
                    std::array<u8, 4> left{}, right{};
                    glReadPixels(116, 239 - 132, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left.data());
                    glReadPixels(148, 239 - 132, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right.data());
                    if (left[0] < 250 || left[1] > 5 || right[1] < 250 || right[0] > 5)
                        return 4;
                    checked += 2;
                }
#endif
#ifdef AURA_HAS_CPU
                if (renderer.getBackendType() == RendererChoice::SOFTWARE)
                {
                    auto &frameBuffer = *static_cast<cpu::CPURenderer &>(renderer).getFrameBufferManager();
                    const u32 left = frameBuffer.getPixel({116, 132}).rgb, right = frameBuffer.getPixel({148, 132}).rgb;
                    if (((left >> 16u) & 255u) < 250 || ((left >> 8u) & 255u) > 5 || ((right >> 8u) & 255u) < 250 ||
                        ((right >> 16u) & 255u) > 5)
                        return 4;
                    checked += 2;
                }
#endif
                renderer.endFrame();
                ++rendered;
            }
            std::vector<u8> pixels(usize{64} * 64 * 4), hdrPixels(usize{32} * 32 * 4);
            if (!renderer.readPixels(colour, pixels) || !renderer.readPixels(hdr, hdrPixels))
                return 5;
            const auto at = [&](i32 x, i32 y)
            {
                return pixels.data() + (static_cast<usize>(y) * 64 + static_cast<usize>(x)) * 4;
            };
            //! sRGB stores 255 and 0 exactly; the half-float target holds linear 0.5.
            if (at(10, 10)[0] != 255 || at(10, 10)[1] != 0 || at(50, 50)[1] != 255 || at(50, 50)[0] != 0 ||
                std::abs(int(hdrPixels[(usize{16} * 32 + 16) * 4]) - 128) > 1)
            {
                std::fprintf(stderr, "TARGETS readback: left %u,%u right %u,%u hdr %u\n", at(10, 10)[0], at(10, 10)[1],
                             at(50, 50)[0], at(50, 50)[1], hdrPixels[(usize{16} * 32 + 16) * 4]);
                return 6;
            }
            std::printf("TARGETS backend=%s rendered_frames=%u checked_pixels=%u readback=ok\n",
                        RendererChoiceToString(renderer.getBackendType()), rendered, checked);
            return rendered == 4 ? 0 : 1;
        }
        if (argc > 2 && std::string_view(argv[2]) == "textures")
        {
            //! TextureDesc paths: sRGB decode, a box-filtered mip chain, half floats, nearest + repeat.
            const auto srgbEncode = [](f32 linear)
            {
                const f32 c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
                return static_cast<int>(std::lround(c * 255.0f));
            };
            std::vector<u8> checker(usize{64} * 64 * 4);
            for (u32 i = 0; i < 64 * 64; ++i)
            {
                const u8 v = ((i % 64) + (i / 64)) % 2 ? 255 : 0;
                const usize at = usize{i} * 4;
                checker[at] = checker[at + 1] = checker[at + 2] = v;
                checker[at + 3] = 255;
            }
            std::vector<u8> grey(usize{4} * 4 * 4, 188);
            std::vector<u16> half(usize{4} * 4 * 4, 0x3800u); // 0.5
            //! Opaque, or the probes would read the blend over black.
            for (usize i = 3; i < grey.size(); i += 4)
            {
                grey[i] = 255;
                half[i] = 0x3C00u; // 1.0
            }
            const std::array<u8, 8> stripes{0, 0, 0, 255, 255, 255, 255, 255};
            const TextureHandle srgb =
                renderer.createTexture({.width = 4, .height = 4, .format = TextureFormat::Rgba8Srgb, .mipmaps = false},
                                       std::as_bytes(std::span(grey)));
            const TextureHandle mipped = renderer.createTexture(
                {.width = 64, .height = 64, .format = TextureFormat::Rgba8Unorm, .sampler = {.maxAnisotropy = 16}},
                std::as_bytes(std::span(checker)));
            const TextureHandle hdr = renderer.createTexture(
                {.width = 4, .height = 4, .format = TextureFormat::Rgba16Float}, std::as_bytes(std::span(half)));
            const TextureHandle repeat = renderer.createTexture(
                {.width = 2,
                 .height = 1,
                 .format = TextureFormat::Rgba8Unorm,
                 .mipmaps = false,
                 .sampler = {.magFilter = TextureFilter::Nearest, .minFilter = TextureFilter::Nearest}},
                std::as_bytes(std::span(stripes)));
            if (!isValidHandle(srgb) || !isValidHandle(mipped) || !isValidHandle(hdr) || !isValidHandle(repeat))
                return 1;

            const auto quad = [&](glm::vec2 origin, f32 size, f32 uvSpan, TextureHandle texture)
            {
                const std::array<gfx::BatchVertex, 4> corners{
                    {{{origin, 0}, {0, 0}, {1, 1, 1, 1}},
                     {{origin.x + size, origin.y, 0}, {uvSpan, 0}, {1, 1, 1, 1}},
                     {{origin + size, 0}, {uvSpan, uvSpan}, {1, 1, 1, 1}},
                     {{origin.x, origin.y + size, 0}, {0, uvSpan}, {1, 1, 1, 1}}}};
                constexpr std::array<u32, 6> indices{0, 1, 2, 0, 2, 3};
                renderer.drawBatch(corners, indices, texture);
            };
            struct Probe
            {
                i32 x;
                i32 y;
                int value;
                const char *what;
            };
            const std::array probes{Probe{18, 18, 188, "sRGB 188 decodes to linear 0.5"},
                                    Probe{42, 42, srgbEncode(0.5f), "a 16x minified checker averages to grey"},
                                    Probe{66, 18, srgbEncode(0.5f), "half float 0.5"},
                                    //! Four periods over 16 px: each texel covers 2 px.
                                    Probe{97, 18, 0, "nearest + repeat: first period dark"},
                                    Probe{99, 18, 255, "nearest + repeat: first period light"},
                                    Probe{101, 18, 0, "nearest + repeat: second period dark"},
                                    Probe{103, 18, 255, "nearest + repeat: second period light"}};
            renderer.setClearColor(0, 0, 0, 1);
            u32 rendered = 0, checked = 0;
            for (u32 frame = 0; frame < 6; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                renderer.beginRenderPass();
                quad({16, 16}, 4, 1, srgb);
                quad({40, 40}, 4, 1, mipped);
                quad({64, 16}, 4, 1, hdr);
                quad({96, 16}, 16, 4, repeat);
                renderer.endRenderPass();
                for (const Probe &probe : probes)
                {
                    int actual = -1;
#ifdef AURA_HAS_OPENGL
                    if (renderer.getBackendType() == RendererChoice::OPENGL)
                    {
                        std::array<u8, 4> pixel{};
                        glReadPixels(probe.x, 239 - probe.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
                        actual = pixel[0];
                    }
#endif
#ifdef AURA_HAS_CPU
                    if (renderer.getBackendType() == RendererChoice::SOFTWARE)
                        actual = static_cast<int>((static_cast<cpu::CPURenderer &>(renderer)
                                                       .getFrameBufferManager()
                                                       ->getPixel({probe.x, probe.y})
                                                       .rgb >>
                                                   16u) &
                                                  0xFFu);
#endif
                    if (actual < 0)
                        continue;
                    if (std::abs(actual - probe.value) > 2)
                    {
                        std::fprintf(stderr, "TEXTURES %s at (%d,%d): got %d expected %d\n", probe.what, probe.x,
                                     probe.y, actual, probe.value);
                        return 2;
                    }
                    ++checked;
                }
                renderer.endFrame();
                ++rendered;
            }
            std::printf("TEXTURES backend=%s rendered_frames=%u checked_pixels=%u\n",
                        RendererChoiceToString(renderer.getBackendType()), rendered, checked);
            return rendered == 6 ? 0 : 1;
        }
        if (argc > 2 && std::string_view(argv[2]) == "canvas")
        {
            //! A particle field moved and rebuilt every frame: the Canvas path end to end.
            constexpr u32 kRects = 20000;
            constexpr u32 kLines = 5000;
            constexpr glm::vec2 kArea{318.0f, 238.0f};
            struct Particle
            {
                glm::vec2 position;
                glm::vec2 velocity;
                glm::vec4 color;
            };
            std::vector<Particle> particles(kRects + kLines);
            for (u32 i = 0; i < particles.size(); ++i)
            {
                const auto n = static_cast<f32>(i);
                particles[i] = {{std::fmod(n * 7.3f, kArea.x), std::fmod(n * 3.1f, kArea.y)},
                                {20.0f + static_cast<f32>(i % 50), 13.0f - static_cast<f32>(i % 37)},
                                {static_cast<f32>(i % 255) / 255.0f, .5f, 1.0f, i < kRects ? 1.0f : .6f}};
            }
            const auto update = [&]
            {
                for (Particle &particle : particles)
                {
                    particle.position += particle.velocity * (1.0f / 60.0f);
                    for (int axis = 0; axis < 2; ++axis)
                    {
                        if (particle.position[axis] < 0.0f)
                            particle.position[axis] += kArea[axis];
                        else if (particle.position[axis] >= kArea[axis])
                            particle.position[axis] -= kArea[axis];
                    }
                }
            };
            gfx::Canvas canvas;
            const auto build = [&]
            {
                update();
                canvas.clear();
                for (u32 i = 0; i < kRects; ++i)
                    canvas.rect(particles[i].position, {2, 2}, particles[i].color);
                for (u32 i = kRects; i < particles.size(); ++i)
                    canvas.line(particles[i].position, particles[i].position + particles[i].velocity * 0.25f,
                                particles[i].color, 1.5f);
            };
            measure("canvas_update", 120, update);
            measure("canvas_build", 120, build);
            const auto frame = [&](bool draw)
            {
                renderer.beginFrame();
                renderer.beginRenderPass();
                if (draw)
                {
                    build();
                    renderer.drawBatch(canvas);
                }
                renderer.endRenderPass();
                renderer.endFrame();
            };
            measure("canvas_empty_frame", 120,
                    [&]
                    {
                        frame(false);
                    });
            measure("canvas_frame", 120,
                    [&]
                    {
                        frame(true);
                    });
            return 0;
        }
        if (argc > 2 && std::string_view(argv[2]) == "lighting")
        {
            // A lit quad under a rotation and then a non-uniform scale: the normal is the inverse
            // transpose of the model, not the model, so a wrong normal matrix shows in the colour.
            const glm::mat4 model = glm::scale(glm::mat4{1.0f}, {2.0f, 1.0f, 1.0f}) *
                                    glm::rotate(glm::mat4{1.0f}, glm::radians(60.0f), glm::vec3{0, 1, 0});
            const glm::vec3 tint{1.0f, 0.5f, 0.25f};
            const std::array lights{gfx::Light{.direction = glm::normalize(glm::vec3{-1.0f, 0.2f, -1.0f}),
                                               .color = tint,
                                               .intensity = 0.6f},
                                    gfx::Light{.type = gfx::LightType::Ambient, .color = tint, .intensity = 0.1f}};
            const glm::vec3 normal =
                glm::normalize(glm::transpose(glm::inverse(glm::mat3(model))) * glm::vec3{0, 0, 1});
            const f32 lighting =
                lights[1].intensity + std::max(glm::dot(normal, -lights[0].direction), 0.0f) * lights[0].intensity;
            const auto encode = [](f32 linear)
            {
                linear = std::min(linear, 1.0f);
                const f32 encoded =
                    linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
                return static_cast<int>(std::lround(encoded * 255.0f));
            };
            //! The quad's centre after the model, in pixels of the 320x240 window.
            const glm::vec4 centre = model * glm::vec4{0, 0, 0.5f, 1};
            const i32 probeX = static_cast<i32>((centre.x + 1.0f) * 0.5f * 320.0f);
            const i32 probeY = static_cast<i32>((1.0f - centre.y) * 0.5f * 240.0f);
            const std::array<int, 3> expected{encode(tint.r * lighting), encode(tint.g * lighting),
                                              encode(tint.b * lighting)};

            gfx::Mesh3D quad;
            for (const glm::vec2 corner : {glm::vec2{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}})
                quad.vertices.push_back({{corner, 0.5f}, {}, {1, 1, 1, 1}, {0, 0, 1}});
            quad.indices = {0, 1, 2, 2, 3, 0, 0, 2, 1, 2, 0, 3};
            const MeshHandle mesh = renderer.createMesh(std::move(quad));
            const auto white = renderer.createSolidColorTexture(255, 255, 255, 255);
            renderer.setLights(lights);
            renderer.setClearColor(0, 0, 0, 1);
            std::array<u8, 4> actual{};
            bool readable = false;
            for (u32 frame = 0; frame < 3; ++frame)
            {
                renderer.beginFrame();
                if (!renderer.frameBegun())
                    continue;
                renderer.beginRenderPass();
                renderer.setTransform({model, glm::mat4{1.0f}, glm::mat4{1.0f}});
                renderer.drawMesh(mesh, white);
                renderer.endRenderPass();
#ifdef AURA_HAS_OPENGL
                if (renderer.getBackendType() == RendererChoice::OPENGL)
                {
                    glReadPixels(probeX, 239 - probeY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
                    readable = true;
                }
#endif
#ifdef AURA_HAS_CPU
                if (renderer.getBackendType() == RendererChoice::SOFTWARE)
                {
                    const u32 pixel = static_cast<cpu::CPURenderer &>(renderer)
                                          .getFrameBufferManager()
                                          ->getPixel({probeX, probeY})
                                          .rgb;
                    actual = {static_cast<u8>(pixel >> 16), static_cast<u8>(pixel >> 8), static_cast<u8>(pixel), 255};
                    readable = true;
                }
#endif
                renderer.endFrame();
            }
            // Other backends are read from a screenshot at the probe pixel.
            std::printf("LIGHTING backend=%s probe=%d,%d expected=%d,%d,%d",
                        RendererChoiceToString(renderer.getBackendType()), probeX, probeY, expected[0], expected[1],
                        expected[2]);
            if (!readable)
                return std::printf("\n"), 0;
            std::printf(" got=%u,%u,%u\n", actual[0], actual[1], actual[2]);
            for (usize c = 0; c < 3; ++c)
                if (std::abs(int(actual[c]) - expected[c]) > 1)
                    return 2;
            return 0;
        }
        if (argc > 2 && std::string_view(argv[2]) == "coverage")
        {
            // Screenshot swatches: black, black, half-green (188: linear blend, sRGB storage), green, magenta;
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
                renderer.setLights(std::array{gfx::Light{.type = gfx::LightType::Ambient}});
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
                        !checkPixel(152, 56, {0, 188, 0}) || !checkPixel(208, 56, {0, 255, 0}) ||
                        !checkPixel(264, 56, {255, 0, 255}) || !checkPixel(40, 136, {0, 255, 0}) ||
                        !checkPixel(112, 136, {255, 0, 255}) || glGetError() != GL_NO_ERROR)
                        return 2;
                }
#endif
                renderer.endRenderPass();
                renderer.endFrame();
                ++rendered;
            }
            std::printf("COVERAGE backend=%s rendered_frames=%u\n", RendererChoiceToString(renderer.getBackendType()),
                        rendered);
            return rendered == 12 ? 0 : 1;
        }
        if (raster)
        {
            // The Sandbox's load at 1080p: a lit floor, textured cubes and a sphere, then a
            // blended UI panel and a coverage-textured text strip.
            // Run with: SDL_VIDEODRIVER=dummy benchmark_runtime software raster
            // AURA_BENCH_DUMP=<file.ppm> writes the software backend's last frame for A/B comparison.
            // AURA_BENCH_OBJECTS=<n> adds a grid of n spheres; GPU backends also report GPU time.
            const auto checker = [&](u32 size, std::array<u8, 4> a, std::array<u8, 4> b)
            {
                std::vector<u8> pixels(usize{size} * size * 4);
                for (u32 y = 0; y < size; ++y)
                    for (u32 x = 0; x < size; ++x)
                        std::copy_n(((x / 16 + y / 16) % 2 ? a : b).data(), 4, &pixels[(usize{y} * size + x) * 4]);
                return renderer.createTextureFromPixels(pixels.data(), size, size);
            };
            const auto floor =
                renderer.createMaterial(Material{.albedo = renderer.createSolidColorTexture(255, 255, 255)});
            const auto crate =
                renderer.createMaterial(Material{.albedo = checker(256, {200, 140, 60, 255}, {90, 60, 30, 255})});
            const auto orb =
                renderer.createMaterial(Material{.albedo = checker(128, {60, 120, 220, 255}, {220, 230, 255, 255})});
            const auto cube = renderer.createMesh(MeshLoader::createCube());
            const auto sphere = renderer.createMesh(MeshLoader::createSphere(3));
            const auto plane = renderer.createMesh(MeshLoader::createPlane());
            const glm::vec3 spin{0.3f, 1, 0.1f};
            std::vector<IRenderer::DrawItem> items{
                {plane, floor, glm::scale(glm::translate(glm::mat4{1}, {0, -0.75f, 0}), {8, 1, 8})},
                {cube, crate, glm::rotate(glm::translate(glm::mat4{1}, {-1.5f, 0, 0}), 0.6f, spin)},
                {sphere, orb, glm::scale(glm::mat4{1}, glm::vec3{1.2f})},
                {cube, crate, glm::rotate(glm::translate(glm::mat4{1}, {1.5f, 0, 0}), -0.6f, spin)},
            };
            const char *objects = std::getenv("AURA_BENCH_OBJECTS");
            const int extra =
                objects ? static_cast<int>(std::clamp(std::strtol(objects, nullptr, 10), 0L, 1L << 20)) : 0;
            const int perRow = std::max(1, static_cast<int>(std::sqrt(static_cast<f32>(extra))));
            for (int i = 0; i < extra; ++i)
            {
                const int column = i % perRow - perRow / 2, row = i / perRow;
                const glm::vec3 at{static_cast<f32>(column) * 0.6f, -0.4f, -2.0f - static_cast<f32>(row) * 0.6f};
                items.push_back({sphere, orb, glm::scale(glm::translate(glm::mat4{1}, at), glm::vec3{0.25f})});
            }
            renderer.setGpuTimingEnabled(true);
            Camera camera =
                Camera::perspective({.fovDeg = 60, .aspect = 1920.0f / 1080.0f, .nearZ = 0.1f, .farZ = 100});
            camera.setPosition({0, 0.8f, 4.5f});
            camera.setRotation(-90, -8);
            renderer.setLights(
                std::array{gfx::Light{}, gfx::Light{.type = gfx::LightType::Ambient, .intensity = 0.15f}});
            renderer.setClearColor(0.1f, 0.1f, 0.3f);

            constexpr u32 kAtlasWidth = 512, kAtlasHeight = 64;
            const auto atlas = renderer.createCoverageTexture(kAtlasWidth, kAtlasHeight);
            std::vector<u8> coverage(usize{kAtlasWidth} * kAtlasHeight);
            for (usize i = 0; i < coverage.size(); ++i)
                coverage[i] = static_cast<u8>((i * 2654435761u) >> 24);
            renderer.updateCoverageTextureRegion(atlas, 0, 0, kAtlasWidth, kAtlasHeight, coverage.data());

            std::vector<gfx::BatchVertex> ui, text;
            std::vector<u32> uiIndices, textIndices;
            const auto quad = [](std::vector<gfx::BatchVertex> &vertices, std::vector<u32> &indices, glm::vec2 min,
                                 glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax, glm::vec4 color)
            {
                const u32 base = static_cast<u32>(vertices.size());
                vertices.push_back({{min.x, min.y, 0}, uvMin, color});
                vertices.push_back({{max.x, min.y, 0}, {uvMax.x, uvMin.y}, color});
                vertices.push_back({{max.x, max.y, 0}, uvMax, color});
                vertices.push_back({{min.x, max.y, 0}, {uvMin.x, uvMax.y}, color});
                indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
            };
            quad(ui, uiIndices, {0, 0}, {360, 1080}, {}, {}, {0.05f, 0.05f, 0.08f, 0.85f});
            for (int i = 0; i < 30; ++i)
            {
                const f32 y = 24.0f + static_cast<f32>(i) * 34.0f;
                quad(ui, uiIndices, {16, y}, {344, y + 28}, {}, {}, {0.2f, 0.25f, 0.4f, i % 3 ? 1.0f : 0.6f});
            }
            for (int line = 0; line < 30; ++line)
                for (int glyph = 0; glyph < 24; ++glyph)
                {
                    const glm::vec2 at{24.0f + static_cast<f32>(glyph) * 12.0f, 29.0f + static_cast<f32>(line) * 34.0f};
                    const glm::vec2 uv{static_cast<f32>(glyph % 32) / 32.0f, 0};
                    quad(text, textIndices, at, at + glm::vec2{11, 18}, uv, uv + glm::vec2{1.0f / 32, 1}, glm::vec4{1});
                }
            const TextureHandle white = renderer.createSolidColorTexture(255, 255, 255);

            const auto frame = [&]
            {
                renderer.beginFrame();
                renderer.setTransform(camera.buildUBO());
                renderer.beginRenderPass();
                renderer.drawMeshes(items);
                renderer.drawBatch(ui, uiIndices, white);
                renderer.drawBatch(text, textIndices, atlas);
                renderer.endRenderPass();
                renderer.endFrame();
            };
            measure(argv[1], 300, frame);
            std::vector<f64> gpu;
            gpu.reserve(300);
            for (int i = 0; i < 300; ++i)
            {
                frame();
                if (const GpuTimingStats timing = renderer.gpuTiming(); timing.available)
                    gpu.push_back(timing.frameMillis);
            }
            if (!gpu.empty())
            {
                std::sort(gpu.begin(), gpu.end());
                std::printf("GPU %s objects=%zu n=%zu p50_us=%.3f p95_us=%.3f dropped=%llu\n", argv[1], items.size(),
                            gpu.size(), gpu[gpu.size() / 2] * 1000.0, gpu[gpu.size() * 95 / 100] * 1000.0,
                            static_cast<unsigned long long>(renderer.gpuTiming().droppedSamples));
            }
#ifdef AURA_HAS_CPU
            if (const char *dump = std::getenv("AURA_BENCH_DUMP"))
                if (auto *cpu = dynamic_cast<cpu::CPURenderer *>(&renderer))
                {
                    const cpu::CpuFrameBufferManager &frameBuffer = *cpu->getFrameBufferManager();
                    std::ofstream out(dump, std::ios::binary);
                    out << "P6\n" << frameBuffer.getWidth() << ' ' << frameBuffer.getHeight() << "\n255\n";
                    for (i32 y = 0; y < frameBuffer.getHeight(); ++y)
                        for (i32 x = 0; x < frameBuffer.getWidth(); ++x)
                        {
                            const u32 rgb = frameBuffer.getPixel({x, y}).rgb;
                            const std::array<char, 3> bytes{static_cast<char>(rgb >> 16), static_cast<char>(rgb >> 8),
                                                            static_cast<char>(rgb)};
                            out.write(bytes.data(), bytes.size());
                        }
                }
#endif
            return 0;
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
        renderer.setLights(std::array{gfx::Light{.type = gfx::LightType::Ambient}});
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
