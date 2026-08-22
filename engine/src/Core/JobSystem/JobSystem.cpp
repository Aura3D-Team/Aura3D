#include "aura/Core/JobSystem/JobSystem.h"

#include <algorithm>
#include <future>
#include <thread>
#include <vector>

#include <ink/ThreadPool.h>

namespace aura3d {
namespace {

[[nodiscard]] i32 resolveWorkerCount(i32 configured) noexcept
{
    if (configured > 0)
        return configured;

    const unsigned detected = std::thread::hardware_concurrency();
    return detected > 0 ? static_cast<i32>(detected) : 1;
}

[[nodiscard]] constexpr i32 bandEdge(i64 band, i64 itemCount, i64 bandCount) noexcept
{
    return static_cast<i32>((band * itemCount) / bandCount);
}

class FenceJoiner {
public:
    explicit FenceJoiner(std::vector<std::future<void>>& fences) noexcept : _fences(fences) {}

    ~FenceJoiner()
    {
        for (std::future<void>& fence : _fences)
        {
            try { fence.get(); }
            catch (...) { }
        }
    }

    FenceJoiner(const FenceJoiner&) = delete;
    FenceJoiner& operator=(const FenceJoiner&) = delete;

private:
    std::vector<std::future<void>>& _fences;
};

} // namespace

JobSystem::JobSystem(i32 workerCount)
    : _workerCount(resolveWorkerCount(workerCount))
{
    INK_VERBOSE << "JobSystem: " << _workerCount << " band(s)";
}

JobSystem::~JobSystem() = default;

void JobSystem::_ensurePool() const
{
    if (_workerCount <= 1)
        return;

    std::call_once(_poolOnce, [this] {
        _pool = std::make_unique<ink::ThreadPool>(static_cast<std::size_t>(_workerCount - 1));
    });
}

void JobSystem::dispatch(i32 itemCount, const BandBody& body) const
{
    if (itemCount <= 0 || !body)
        return;

    _ensurePool();

    const i32 bands = _pool ? std::min(_workerCount, itemCount) : 1;
    if (bands <= 1)
    {
        body(0, itemCount);
        return;
    }

    std::vector<std::future<void>> fences;
    fences.reserve(static_cast<std::size_t>(bands) - 1u);

    {
        const FenceJoiner joiner(fences);

        for (i32 band = 1; band < bands; ++band)
        {
            const i32 begin = bandEdge(band, itemCount, bands);
            const i32 end = bandEdge(band + 1, itemCount, bands);

            fences.push_back(_pool->submit([&body, begin, end] { body(begin, end); }));
        }

        body(0, bandEdge(1, itemCount, bands));
    }
}

} // namespace aura3d
