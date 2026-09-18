#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <thread>
#include "aura/Core/Engine.h"
#include "aura/Core/MeshLoader/MeshLoader.h"
#include "aura/UI/UI.hpp"
#include "RuntimeAudioDevice.h"

namespace {
thread_local bool counting = false;
thread_local std::size_t allocations = 0, bytes = 0;
}
#ifndef AURA_ENABLE_DEBUG_MODE
void* operator new(std::size_t n)
{
    if (counting) { ++allocations; bytes += n; }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#endif
using namespace aura3d;
using namespace aura3d::ui;
using Clock = std::chrono::steady_clock;

template<class Body>
void measure(const char* label, int samples, Body body)
{
    allocations = bytes = 0;
    counting = true;
    const auto cold = Clock::now();
    body();
    const double coldUs = std::chrono::duration<double, std::micro>(Clock::now() - cold).count();
    counting = false;
    const auto coldAllocations = allocations, coldBytes = bytes;
    for (int i = 0; i < 5; ++i) body();
    std::vector<double> times(samples);
    allocations = bytes = 0;
    counting = true;
    for (double& time : times)
    {
        const auto start = Clock::now();
        body();
        time = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    }
    counting = false;
    std::sort(times.begin(), times.end());
    std::printf("BENCH %s n=%d cold_us=%.3f cold_new_count=%zu cold_new_bytes=%zu p50_us=%.3f p95_us=%.3f p99_us=%.3f new_count=%zu new_bytes=%zu\n",
                label, samples, coldUs, coldAllocations, coldBytes, times[samples/2],
                times[samples*95/100], times[samples*99/100], allocations, bytes);
}

int main(int argc, char** argv)
{
    ink::LogManager::getInstance().setGlobalLevel(ink::LogLevel::ERROR);
    if (argc > 1)
    {
        AuraConfig config;
        config.renderer.backend = argv[1];
        config.renderer.validationLayers = std::getenv("AURA_BENCH_VALIDATION") != nullptr;
        config.window.width = 320; config.window.height = 240;
        config.window.fpsLimit = 0; config.window.vsync = false;
        config.graphics.cpuThreads = 4;
        config.graphics.gpuPreference = "any";
        config.audio.backend = wma::AudioBackend::Null;
        config.logging.level = ink::LogLevel::ERROR;
        Engine engine(config);
        auto& renderer = *engine.getRenderer();
        gfx::Mesh3D mesh;
        mesh.vertices = {{{-.8f,-.8f,.5f},{0,0},{1,1,1,1},{0,0,1}},
                         {{.8f,-.8f,.5f},{1,0},{1,1,1,1},{0,0,1}},
                         {{0,.8f,.5f},{.5f,1},{1,1,1,1},{0,0,1}}};
        mesh.indices = {0,1,2};
        const bool indexed = argc > 2 && std::string_view(argv[2]) == "indexed";
        if (indexed)
        {
            mesh.vertices.clear(); mesh.indices.clear();
            constexpr u32 side = 100;
            for (u32 y = 0; y < side; ++y) for (u32 x = 0; x < side; ++x)
                mesh.vertices.push_back({{-0.9f + 1.8f*x/(side-1), -0.9f + 1.8f*y/(side-1), .5f},
                                         {f32(x)/(side-1),f32(y)/(side-1)}, {1,1,1,1}, {0,0,1}});
            for (u32 y = 0; y + 1 < side; ++y) for (u32 x = 0; x + 1 < side; ++x)
            {
                const u32 i = y*side+x;
                mesh.indices.insert(mesh.indices.end(), {i,i+1,i+side,i+1,i+side+1,i+side});
            }
        }
        const auto handle = renderer.createMesh(std::move(mesh));
        const auto red = renderer.createSolidColorTexture(255,0,0);
        const auto green = renderer.createSolidColorTexture(0,255,0);
        const auto redMaterial = renderer.createMaterial(Material{.albedo = red});
        std::vector<IRenderer::DrawItem> draws(indexed ? 1 : 512);
        for (auto& draw : draws) { draw.mesh = handle; draw.material = redMaterial; draw.model = glm::mat4{1}; }
        gfx::LightUBO light; light.ambient = 1; light.intensity = 0;
        renderer.setLight(light);
        renderer.setTransform({glm::mat4{1},glm::mat4{1},glm::mat4{1}});
        std::array<TextureHandle, 8> textures{};
        for (auto& texture : textures) texture = renderer.createDynamicTexture(64,64);
        std::vector<u8> pixels(64 * 64 * 4, 255);
        const std::array<u32,6> indices{0,1,2,2,3,0};
        const bool check = std::getenv("AURA_BENCH_VALIDATE_IMAGE") != nullptr;
        TextureHandle large;
        std::vector<u8> largePixels;
        if (check)
        {
            large = renderer.createDynamicTexture(1024,1024);
            largePixels.assign(1024*1024*4,255);
        }
        measure(indexed ? "cpu_indexed" : argv[1], check ? 4 : 120, [&] {
            renderer.beginFrame();
            if (check)
                for (int i = 0; i < 4; ++i)
                    renderer.updateTextureRegion(large,0,0,1024,1024,largePixels.data());
            for (auto texture : textures) renderer.updateTextureRegion(texture,0,0,64,64,pixels.data());
            if (check)
            {
                std::array<u8,32*32*4> patch{};
                for (usize i = 0; i < patch.size(); i += 4)
                { patch[i] = patch[i+1] = patch[i+3] = 255; }
                renderer.updateTextureRegion(textures[0],32,32,32,32,patch.data());
            }
            if (check && std::getenv("AURA_BENCH_INVALID_REGIONS"))
            {
                const std::array<u8,4> invalid{255,0,0,255};
                renderer.updateTextureRegion(textures[0],UINT32_MAX,0,2,1,invalid.data());
                renderer.updateTextureRegion(textures[0],1,0,UINT32_MAX,1,invalid.data());
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
                const f32 y = static_cast<f32>(i / 80) * 4;
                std::array<gfx::Vertex2D,4> quad{{{{x,y},{0,0},{0,0,1,1}},
                    {{x+3,y},{1,0},{0,0,1,1}},{{x+3,y+3},{1,1},{0,0,1,1}},{{x,y+3},{0,1},{0,0,1,1}}}};
                if (check && (i == 0 || i == 8))
                    for (auto& vertex : quad)
                    { vertex.texCoord = glm::vec2(i == 0 ? .75f : .25f); vertex.color = glm::vec4{1}; }
                renderer.drawBatch2D(quad,indices,textures[i % 8]);
            }
            renderer.endRenderPass();
            renderer.endFrame();
        });
        return 0;
    }
    JobSystem jobs(4);
    std::array<int,4096> values{};
    measure("dispatch", 2000, [&] { jobs.dispatch(values.size(), [&](i32 first, i32 end) {
        for (i32 i=first;i<end;++i) ++values[i];
    }); });
    const char* path = "/tmp/aura-runtime-grid.obj";
    {
        std::ofstream file(path);
        constexpr int side = 120;
        for (int y=0;y<side;++y) for(int x=0;x<side;++x) file << "v " << x << ' ' << y << " 0\n";
        for (int y=0;y<side-1;++y) for(int x=0;x<side-1;++x)
        { const int i=y*side+x+1; file << "f "<< i<<' '<<i+1<<' '<<i+side<<"\nf "<<i+1<<' '<<i+side+1<<' '<<i+side<<'\n'; }
    }
    measure("obj", 30, [&] { auto mesh=MeshLoader::loadOBJ(path); if(mesh.empty()) std::abort(); });
    std::remove(path);
    AtlasTextShaper shaper;
    UIRoot root(shaper);
    auto& column = root.setContent<Column>();
    auto& first = column.add<Button>("first");
    auto& second = column.add<Button>("second");
    root.resize({320,240}); root.update(0);
    measure("hover", 10000, [&] { root.pointerMoved(first.bounds().center()); root.pointerMoved(second.bounds().center()); });
    auto device = std::make_unique<RuntimeAudioDevice>();
    auto* pump = device.get();
    AudioEngine audio(std::move(device),32);
    AudioClipData pcm; pcm.sampleRate=audio.sampleRate(); pcm.channelCount=1; pcm.samples.assign(8192,.01f);
    auto clip = audio.createClip(pcm);
    for(int i=0;i<32;++i) (void)audio.play(AudioSourceDesc{.clip=clip,.loop=true});
    std::array<f32,512> output{};
    measure("audio", 2000, [&] { pump->callback(output); });
    std::atomic<bool> stop{false};
    std::thread control([&] { while(!stop.load()) { auto c=audio.createClip(pcm); audio.unloadClip(c); audio.update(0); } });
    measure("audio_contention", 2000, [&] { pump->callback(output); });
    stop = true; control.join();
}
