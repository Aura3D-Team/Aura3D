#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <thread>
#include "aura/Core/AudioEngine/AudioEngine.h"
#include "RuntimeAudioDevice.h"
#include "TestUtils.h"

#ifndef AURA_ENABLE_DEBUG_MODE
namespace {
thread_local bool inCallback = false;
thread_local unsigned callbackAllocations = 0, callbackFrees = 0;
}
void* operator new(std::size_t n)
{
    if (inCallback) ++callbackAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p && inCallback) ++callbackFrees; std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#endif

int main()
{
    using namespace aura3d;
    auto owned = std::make_unique<RuntimeAudioDevice>();
    auto* device = owned.get();
    AudioEngine engine(std::move(owned), 8);
    AudioClipData data;
    data.sampleRate = engine.sampleRate();
    data.channelCount = 1;
    data.samples.assign(8192, .25f);
    const auto clip = engine.createClip(data);
    const auto source = engine.play(AudioSourceDesc{.clip = clip, .loop = true});
    for (int i = 0; i < 100000; ++i) engine.setSourceGain(source, (i & 1) ? .5f : .75f);
    std::array<f32, 512> output{};
    device->callback(output);
    AURA_CHECK(std::abs(output[0] - .125f) < .0001f, "saturated controls apply the latest gain");
    engine.stop(source);
    device->callback(output);
    AudioSourceHandle recycled;
    for (int i = 0; i < 65535; ++i)
    {
        recycled = engine.play(clip);
        if (i != 65534) engine.stop(recycled);
    }
    AURA_CHECK(engine.isPlaying(recycled), "completion does not alias a wrapped handle generation");
    device->callback(output);
    AURA_CHECK(output[0] == .25f, "coalesced play restarts after handle generation wrap");
    std::atomic<bool> stop{false};
    std::atomic<bool> valid{true};
    std::atomic<unsigned> allocations{0}, frees{0};
    std::thread audio([&] {
        std::array<f32, 512> block{};
        while (!stop.load(std::memory_order_acquire))
        {
#ifndef AURA_ENABLE_DEBUG_MODE
            inCallback = true;
#endif
            device->callback(block);
#ifndef AURA_ENABLE_DEBUG_MODE
            inCallback = false;
#endif
            for (f32 sample : block) if (!std::isfinite(sample)) valid = false;
        }
#ifndef AURA_ENABLE_DEBUG_MODE
        allocations = callbackAllocations;
        frees = callbackFrees;
#endif
    });
    for (int i = 0; i < 4000; ++i)
    {
        auto temporary = engine.createClip(data);
        auto voice = engine.play(temporary);
        engine.pause(voice);
        engine.resume(voice);
        engine.setSourcePosition(source, glm::vec3{static_cast<f32>(i), 0, 0});
        engine.setListener(AudioListener3D{});
        engine.unloadClip(temporary);
        engine.update(0);
    }
    engine.unloadAllClips();
    stop.store(true, std::memory_order_release);
    audio.join();
    device->callback(output);
    device->callback(output);
    engine.update(0);
    AURA_CHECK(valid, "concurrent control/loading/unloading keeps output finite");
    AURA_CHECK(engine.activeVoiceCount() == 0, "unload stops all voices despite saturated controls");
#ifndef AURA_ENABLE_DEBUG_MODE
    AURA_CHECK(allocations == 0 && frees == 0, "audio callback allocates and frees zero heap objects");
#else
    std::cout << "[SKIP] local allocation hooks: engine debug hooks own operator new/delete\n";
#endif
    AURA_TEST_MAIN_RETURN();
}
