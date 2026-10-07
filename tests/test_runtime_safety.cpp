#include "aura/Core/JobSystem/JobSystem.h"
#include "aura/Utils/AudioMailbox.h"
#include "aura/Utils/FutureJoiner.h"
#include "aura/Utils/InlineScratch.h"
#include <array>
#include <atomic>
#include <future>
#include <ink/ArenaResource.h>
#include <ink/ParallelProcessor.h>
#include <ink/ThreadPool.h>
#include <limits>
#include <string_view>
#include <thread>
#ifdef AURA_HAS_VULKAN
#include "aura/Core/AuraException/AuraException.h"
#endif
#ifdef AURA_HAS_CPU
#include "aura/Renderer/Software/CPURenderer.h"
#endif
#include "TestUtils.h"

using namespace aura3d;

int main()
{
#ifdef AURA_HAS_VULKAN
    int resultCalls = 0;
    const auto failingVulkanCall = [&]() -> VkResult
    {
        ++resultCalls;
        return resultCalls == 1 ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
    };
    bool caughtVulkanError = false;
    try
    {
        VK_RESULT_CHECK(failingVulkanCall());
    }
    catch (const AuraException &error)
    {
        caughtVulkanError = std::string_view(error.what()) == AuraException(VK_ERROR_OUT_OF_HOST_MEMORY).what();
    }
    AURA_CHECK(caughtVulkanError && resultCalls == 1,
               "Vulkan failure is evaluated once and preserves the original error");
    resultCalls = 0;
    const auto successfulVulkanCall = [&]() -> VkResult
    {
        ++resultCalls;
        return VK_SUCCESS;
    };
    bool elseRan = false;
    bool threw = false;
    try
    {
        if (true)
            VK_RESULT_CHECK(successfulVulkanCall());
        else
            elseRan = true;
    }
    catch (const AuraException &)
    {
        threw = true;
    }
    AURA_CHECK(resultCalls == 1 && !elseRan && !threw, "Vulkan success is evaluated once and check is safe in if/else");
#endif
    for (bool submissionFailure : {false, true})
    {
        std::atomic<bool> finished{false};
        bool caught = false;
        ink::ThreadPool pool(2); // Unlike std::async, these futures do not join on destruction.
        try
        {
            std::vector<std::future<void>> futures;
            FutureJoiner joiner(futures);
            futures.push_back(pool.submit(
                [&]
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    finished = true;
                }));
            if (submissionFailure)
                throw std::runtime_error("submission failed");
            futures.insert(futures.begin(), pool.submit(
                                                []
                                                {
                                                    throw std::runtime_error("recording failed");
                                                }));
            joiner.get();
        }
        catch (const std::runtime_error &)
        {
            caught = true;
        }
        AURA_CHECK(caught, "submission and worker exceptions reach the caller");
        AURA_CHECK(finished, "workers complete before submission/recording exceptions escape");
    }
    ink::ParallelProcessor executor(4);
    std::array<std::atomic<int>, 101> visits{};
    const auto run = [&]
    {
        executor.run(visits.size(),
                     [&](usize i)
                     {
                         ++visits[i];
                     });
    };
    std::thread concurrent(run);
    run();
    concurrent.join();
    bool exact = true;
    for (const auto &n : visits)
        exact &= n == 2;
    AURA_CHECK(exact, "concurrent dispatches visit every item exactly once each");
    std::atomic<int> nested{0};
    executor.run(4,
                 [&](usize)
                 {
                     executor.run(3,
                                  [&](usize)
                                  {
                                      ++nested;
                                  });
                 });
    AURA_CHECK(nested == 12, "nested dispatch runs inline without deadlock");
    AURA_CHECK_THROWS(executor.run(4,
                                   [](usize i)
                                   {
                                       if (i == 1)
                                           throw std::runtime_error("worker");
                                   }),
                      std::runtime_error, "parallel exceptions propagate after joining");
    run();
    JobSystem jobs(4);
    std::array<int, 101> values{};
    jobs.dispatch(101,
                  [&](i32 begin, i32 end)
                  {
                      for (i32 i = begin; i < end; ++i)
                          values[i] = i + 1;
                  });
    AURA_CHECK(values.front() == 1 && values.back() == 101, "job bands cover uneven ranges");

    ink::ArenaResource resource(128);
    void *first;
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

    struct Message
    {
        u32 sequence = 0;
        u32 inverse = ~0u;
    };
    AudioMailbox<Message> mailbox;
    std::atomic<bool> done{false};
    std::thread producer(
        [&]
        {
            for (u32 i = 1; i <= 100000; ++i)
                mailbox.publish({i, ~i});
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
    const auto coverageTexture = renderer.createCoverageTexture(3, 2);
    AURA_CHECK(isValidHandle(coverageTexture), "software coverage textures allocate without a window");
    AURA_CHECK(!isValidHandle(renderer.createCoverageTexture(0, 2)) &&
                   !isValidHandle(renderer.createCoverageTexture(UINT32_MAX, 2)),
               "coverage texture dimensions reject zero and integer overflow");
    const std::array<u8, 3> coveragePixels{0, 128, 255};
    renderer.updateCoverageTextureRegion(coverageTexture, 0, 0, 3, 1, coveragePixels.data());
    renderer.updateCoverageTextureRegion(coverageTexture, 2, 1, 1, 1, coveragePixels.data());
    renderer.updateCoverageTextureRegion(coverageTexture, UINT32_MAX, 0, 2, 1, coveragePixels.data());
    renderer.updateCoverageTextureRegion(coverageTexture, 0, UINT32_MAX, 1, 2, coveragePixels.data());
    renderer.updateCoverageTextureRegion(coverageTexture, 1, 0, UINT32_MAX, 1, coveragePixels.data());
    renderer.updateCoverageTextureRegion(coverageTexture, 0, 1, 1, UINT32_MAX, coveragePixels.data());
    renderer.updateCoverageTextureRegion(coverageTexture, 3, 2, 0, 0, coveragePixels.data());
    renderer.updateCoverageTextureRegion(coverageTexture, 0, 0, 1, 1, nullptr);
    renderer.updateCoverageTextureRegion({}, 0, 0, 1, 1, coveragePixels.data());
    renderer.updateCoverageTextureRegion(texture, 0, 0, 1, 1, coveragePixels.data());
    renderer.updateTextureRegion(coverageTexture, 0, 0, 1, 1, pixels.data());
    AURA_CHECK(true, "coverage updates reject invalid handles, bounds and formats without invalid access");
    using cpu::ClipVertex;
    const ClipVertex a{{-.5f, -.5f, 0, 1}, {0, 0}, {1, 0, 0, 1}};
    const ClipVertex b{{.5f, -.5f, 0, 1}, {1, 0}, {0, 1, 0, 1}};
    const ClipVertex c{{0, .5f, -2, 1}, {.5f, 1}, {0, 0, 1, 1}};
    int count = 0;
    bool bounded = true, interpolated = false;
    const auto emit = [&](const cpu::ScreenTriangle &triangle)
    {
        ++count;
        for (const auto &v : {triangle.v0, triangle.v1, triangle.v2})
        {
            bounded &= std::isfinite(v.x) && std::isfinite(v.invW) && v.x >= 0 && v.x <= 100 && v.y >= 0 &&
                       v.y <= 100 && v.z >= -.0001f && v.z <= 1.0001f;
            interpolated |= v.uv.y > 0 && v.uv.y < 1;
        }
    };
    cpu::clipTriangle(a, b, c, 100, 100, emit);
    AURA_CHECK(count == 2 && bounded && interpolated,
               "near-plane clip emits two bounded triangles with interpolated attributes");
    count = 0;
    auto behind = c;
    behind.position = {0, 2, -2, -1};
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
