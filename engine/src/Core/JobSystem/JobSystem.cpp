#include "aura/Core/JobSystem/JobSystem.h"

#include <algorithm>
#include <thread>

#include <ink/ParallelProcessor.h>

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
        _pool = std::make_unique<ink::ParallelProcessor>(static_cast<std::size_t>(_workerCount));
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

    struct Dispatch { const BandBody& body; i32 items; i32 bands; } work{body, itemCount, bands};
    _pool->run(static_cast<std::size_t>(bands), [&work](std::size_t band) {
        work.body(bandEdge(static_cast<i64>(band), work.items, work.bands),
                  bandEdge(static_cast<i64>(band + 1), work.items, work.bands));
    });
}

} // namespace aura3d
