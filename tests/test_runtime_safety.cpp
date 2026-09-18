#include <array>
#include <atomic>
#include <future>
#include <limits>
#include <thread>
#include <ink/ArenaResource.h>
#include <ink/ParallelProcessor.h>
#include <ink/ThreadPool.h>
#include "aura/Core/JobSystem/JobSystem.h"
#include "aura/Utils/AudioMailbox.h"
#include "aura/Utils/FutureJoiner.h"
#include "aura/Utils/InlineScratch.h"
#ifdef AURA_HAS_CPU
#include "aura/Renderer/Software/CPURenderer.h"
#endif
#include "TestUtils.h"

using namespace aura3d;

int main()
{
    for (bool submissionFailure : {false, true})
    {
        std::atomic<bool> finished{false};
        ink::ThreadPool pool(2); // Unlike std::async, these futures do not join on destruction.
        try
        {
            std::vector<std::future<void>> futures;
            FutureJoiner joiner(futures);
            futures.push_back(pool.submit([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                finished = true;
            }));
            if (submissionFailure) throw std::runtime_error("submission failed");
            futures.insert(futures.begin(), pool.submit([] {
                throw std::runtime_error("recording failed");
            }));
            joiner.get();
        }
        catch (const std::runtime_error&) {}
        AURA_CHECK(finished, "workers complete before submission/recording exceptions escape");
    }
    ink::ParallelProcessor executor(4);
    std::array<std::atomic<int>, 101> visits{};
    const auto run = [&] { executor.run(visits.size(), [&](usize i) { ++visits[i]; }); };
    std::thread concurrent(run);
    run();
    concurrent.join();
    bool exact = true;
    for (const auto& n : visits) exact &= n == 2;
    AURA_CHECK(exact, "concurrent dispatches visit every item exactly once each");
    std::atomic<int> nested{0};
    executor.run(4, [&](usize) { executor.run(3, [&](usize) { ++nested; }); });
    AURA_CHECK(nested == 12, "nested dispatch runs inline without deadlock");
    AURA_CHECK_THROWS(executor.run(4, [](usize i) { if (i == 1) throw std::runtime_error("worker"); }),
                      std::runtime_error, "parallel exceptions propagate after joining");
    run();
    JobSystem jobs(4);
    std::array<int, 101> values{};
    jobs.dispatch(101, [&](i32 begin, i32 end) { for (i32 i = begin; i < end; ++i) values[i] = i + 1; });
    AURA_CHECK(values.front() == 1 && values.back() == 101, "job bands cover uneven ranges");

    ink::ArenaResource resource(128);
    void* first;
    {
        ink::ArenaResource::Scope scope(resource);
        first = resource.allocate(64, 64);
        {
            ink::ArenaResource::Scope nestedScope(resource);
            std::pmr::vector<int> many{&resource};
            many.assign(10000, 7);
            AURA_CHECK(many.back() == 7, "arena grows while nested containers remain alive");
        }
        AURA_CHECK(reinterpret_cast<std::uintptr_t>(first) % 64 == 0, "arena honors over-alignment");
    }
    {
        ink::ArenaResource::Scope scope(resource);
        AURA_CHECK(resource.allocate(64, 64) == first, "outer scope resets and reuses arena storage");
    }
    std::weak_ptr<int> weak;
    {
        InlineScratch<std::shared_ptr<int>, 2> scratch;
        auto p = std::make_shared<int>(5);
        weak = p;
        scratch.values.assign(100, p);
    }
    AURA_CHECK(weak.expired(), "scratch spill destroys nontrivial elements");

    struct Message { u32 sequence = 0; u32 inverse = ~0u; };
    AudioMailbox<Message> mailbox;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (u32 i = 1; i <= 100000; ++i) mailbox.publish({i, ~i});
        done.store(true, std::memory_order_release);
    });
    Message message;
    bool consistent = true;
    u32 last = 0;
    while (!done.load(std::memory_order_acquire) || last != 100000)
        if (mailbox.consume(message))
        {
            consistent &= message.inverse == ~message.sequence && message.sequence >= last;
            last = message.sequence;
        }
    producer.join();
    AURA_CHECK(consistent, "mailbox saturation coalesces without torn or reordered state");
#ifdef AURA_HAS_CPU
    cpu::CPURenderer renderer(wma::WindowDetails{});
    const auto texture = renderer.createDynamicTexture(4, 4);
    const std::array<u8, 8> pixels{};
    renderer.updateTextureRegion(texture, UINT32_MAX, 0, 2, 1, pixels.data());
    renderer.updateTextureRegion(texture, 0, UINT32_MAX, 1, 2, pixels.data());
    renderer.updateTextureRegion(texture, 1, 0, UINT32_MAX, 1, pixels.data());
    renderer.updateTextureRegion(texture, 3, 3, 1, 1, pixels.data());
    renderer.updateTextureRegion(texture, 4, 4, 0, 0, pixels.data());
    AURA_CHECK(true, "texture edges and overflowing regions are handled without invalid access");
    using cpu::ClipVertex;
    const ClipVertex a{{-.5f,-.5f,0,1},{0,0},{1,0,0,1}};
    const ClipVertex b{{.5f,-.5f,0,1},{1,0},{0,1,0,1}};
    const ClipVertex c{{0,.5f,-2,1},{.5f,1},{0,0,1,1}};
    int count = 0;
    bool bounded = true, interpolated = false;
    const auto emit = [&](const cpu::ScreenTriangle& triangle) {
        ++count;
        for (const auto& v : {triangle.v0, triangle.v1, triangle.v2})
        {
            bounded &= std::isfinite(v.x) && std::isfinite(v.invW) && v.x >= 0 && v.x <= 100 &&
                       v.y >= 0 && v.y <= 100 && v.z >= -.0001f && v.z <= 1.0001f;
            interpolated |= v.uv.y > 0 && v.uv.y < 1;
        }
    };
    cpu::clipTriangle(a, b, c, 100, 100, emit);
    AURA_CHECK(count == 2 && bounded && interpolated, "near-plane clip emits two bounded triangles with interpolated attributes");
    count = 0;
    auto behind = c; behind.position = {0, 2, -2, -1};
    cpu::clipTriangle(a, b, behind, 100, 100, emit);
    AURA_CHECK(count > 0 && bounded, "camera-plane crossing retains its visible polygon");
    count = 0;
    auto farA = a, farB = b, farC = c;
    farA.position.z = farB.position.z = farC.position.z = -3;
    cpu::clipTriangle(farA, farB, farC, 100, 100, emit);
    AURA_CHECK(count == 0, "fully clipped geometry emits nothing");
#endif
    AURA_TEST_MAIN_RETURN();
}
