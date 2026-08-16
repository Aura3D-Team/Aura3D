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

/**
 * @brief Where band @p band starts, in a balanced partition of @p itemCount
 *        into @p bandCount pieces.
 *
 * Every band gets floor(n/b) or ceil(n/b) items and the edges land exactly on
 * 0 and n, with no remainder to distribute by hand. The obvious alternative --
 * a fixed step of ceil(n/b) -- silently wastes workers whenever b does not
 * divide n: 100 items across 24 bands gives a step of 5, which fills 20 bands
 * and leaves 4 threads with nothing to do.
 *
 * Computed in 64 bits because the product reaches n*b, which overflows i32 long
 * before either factor does.
 */
[[nodiscard]] constexpr i32 bandEdge(i64 band, i64 itemCount, i64 bandCount) noexcept
{
    return static_cast<i32>((band * itemCount) / bandCount);
}

/**
 * @brief Drains a dispatch's fences on the way out, however it leaves.
 *
 * The queued bands capture the caller's JobSystem::BandBody by reference, so
 * they must all be finished before dispatch()'s frame dies -- including when
 * the band running inline throws and unwinds past the join. A destructor cannot
 * propagate, so what a band threw is discarded here; JobSystem::dispatch()
 * documents that bodies must not throw.
 */
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
    //! Pool construction is deferred to _ensurePool() -- see the class comment
    //! on why an Engine-owned JobSystem should not cost threads until
    //! something actually dispatches through it.
    INK_VERBOSE << "JobSystem: " << _workerCount << " band(s)";
}

//! Out of line so the header can forward-declare ink::ThreadPool.
JobSystem::~JobSystem() = default;

void JobSystem::_ensurePool() const
{
    if (_workerCount <= 1)
        return; //! Nothing to build: dispatch() always takes the serial path.

    std::call_once(_poolOnce, [this] {
        _pool = std::make_unique<ink::ThreadPool>(static_cast<std::size_t>(_workerCount - 1));
    });
}

void JobSystem::dispatch(i32 itemCount, const BandBody& body) const
{
    if (itemCount <= 0 || !body)
        return;

    _ensurePool();

    //! Never more bands than items: an empty band is pure hand-off cost.
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

            /*
             * body is captured by reference, which the joiner above is what
             * makes safe: every task referencing it is drained before this
             * scope -- and with it the caller's callable -- goes away.
             */
            fences.push_back(_pool->submit([&body, begin, end] { body(begin, end); }));
        }

        //! The calling thread takes the first band rather than blocking on workers.
        body(0, bandEdge(1, itemCount, bands));
    }
}

} // namespace aura3d
