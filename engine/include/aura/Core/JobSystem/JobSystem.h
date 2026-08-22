#ifndef AURA_JOB_SYSTEM_H
#define AURA_JOB_SYSTEM_H

#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include "aura/aura.h"

//! Forward-declared, not included: only the private member's type needs the
//! name, and pulling ink/ThreadPool.h (<thread>, <mutex>, <queue>, <future>)
//! into a header this widely included would cost every translation unit.
namespace ink { class ThreadPool; }

namespace aura3d {

/**
 * @class JobSystem
 * @brief Engine-wide parallel-for over an index range.
 *
 * For non-rendering work that needs threads (procedural generation, mesh
 * transforms, field evaluation) without opening its own pool. Only exposes a
 * way to *run work*, never the threads themselves.
 *
 * The pool is built lazily on the first dispatch(), not in the constructor:
 * Engine owns one of these unconditionally, but many runs never call
 * dispatch() directly and would otherwise pay for idle threads.
 *
 * @par Threading
 * dispatch() is synchronous and blocks until every band finishes. Do not call
 * it from inside another dispatch() -- nesting deadlocks against a pool with
 * no spare workers. Lazy pool construction is thread-safe.
 */
class JobSystem {
public:
    /// Body of one band, called as @c body(begin, end) over a half-open range.
    using BandBody = std::function<void(i32 begin, i32 end)>;

    /// @param workerCount Bands to split work into. 0 auto-detects from
    ///        hardware concurrency.
    explicit JobSystem(i32 workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /**
     * @brief Splits [0, @p itemCount) into contiguous bands and runs @p body on
     *        each, blocking until all have finished.
     *
     * Bands own disjoint ranges, so a body writing only within its given range
     * needs no synchronisation of its own.
     *
     * @param itemCount Size of the range; zero or negative does nothing.
     * @param body Must not throw. An exception escaping a band is discarded.
     */
    void dispatch(i32 itemCount, const BandBody& body) const;

    /// Bands dispatch() splits work into, after auto-detection.
    [[nodiscard]] i32 workerCount() const noexcept { return _workerCount; }

private:
    //! Builds _pool exactly once, from whichever thread's dispatch() gets
    //! there first. A no-op on every call after the first.
    void _ensurePool() const;

    i32 _workerCount = 1;

    //! Sized to _workerCount - 1: dispatch() runs the first band on the
    //! calling thread. Mutable because dispatch() is logically const but
    //! builds the pool lazily on its first call.
    mutable std::once_flag _poolOnce;
    mutable std::unique_ptr<ink::ThreadPool> _pool;
};

} // namespace aura3d

#endif // AURA_JOB_SYSTEM_H
