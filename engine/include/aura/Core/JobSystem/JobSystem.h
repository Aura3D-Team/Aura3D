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
 * The engine's own parallelism is expressed through batch entry points --
 * IRenderer::drawMeshes() hands a whole draw list over so a backend can record
 * it across threads, and the software rasteriser defers a frame's triangles to
 * one dispatch in endRenderPass(). This is the same idea for work that is not
 * rendering at all: generating a texture procedurally, transforming a mesh,
 * evaluating a field. Those never reach a renderer, so no draw-batching API can
 * express them, and without this every such caller would open its own pool
 * alongside the ones the engine already runs.
 *
 * Deliberately not a pool accessor. Callers get a way to *run work*, never a
 * handle to the threads running it, so the pool's lifetime, size and scheduling
 * stay the engine's business.
 *
 * The pool itself is built lazily, on the first dispatch() call rather than in
 * the constructor: an Engine owns one of these unconditionally (see Engine.h),
 * but plenty of runs -- anything that never calls dispatch() directly and
 * whose renderer backend has its own draw-recording pool (Vulkan) or now
 * shares this one (the software rasteriser, see CpuFrameBufferManager) --
 * never need it at all. Building `workerCount - 1` OS threads that would then
 * sit blocked on a condition variable for the process' entire lifetime is
 * pure waste in exactly that case.
 *
 * @par Threading
 * dispatch() is synchronous: it returns only once every band has finished, so
 * a caller never observes a partially written result. It is safe to call from
 * any thread that is not itself inside a dispatch() -- nesting would deadlock
 * against a pool that has no spare workers. The lazy pool construction is
 * itself thread-safe (std::call_once), so two threads racing to be the first
 * caller cannot build the pool twice.
 */
class JobSystem {
public:
    /// Body of one band, called as @c body(begin, end) over a half-open range.
    using BandBody = std::function<void(i32 begin, i32 end)>;

    /**
     * @param workerCount Bands to split work into. 0 auto-detects from the
     *        hardware concurrency -- the same convention
     *        AuraSettings::getCpuThreads() uses for the rasteriser and the
     *        Vulkan command recorder.
     */
    explicit JobSystem(i32 workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /**
     * @brief Splits [0, @p itemCount) into contiguous bands and runs @p body on
     *        each, blocking until all of them have finished.
     *
     * Bands own disjoint ranges, so a body that writes only within the range it
     * is given needs no synchronisation of its own -- which is the whole point,
     * and the reason the range rather than the individual index is handed over:
     * a body amortises its setup across its whole band instead of paying it per
     * element.
     *
     * @param itemCount Size of the range; zero or negative does nothing.
     * @param body Must not throw. An exception escaping a band is discarded at
     *        the join, because the bands still queued hold a reference to
     *        @p body and must be drained before this frame's stack unwinds.
     */
    void dispatch(i32 itemCount, const BandBody& body) const;

    /// Bands dispatch() splits work into, after auto-detection.
    [[nodiscard]] i32 workerCount() const noexcept { return _workerCount; }

private:
    //! Builds _pool exactly once, from whichever thread's dispatch() gets
    //! there first. A no-op on every call after the first.
    void _ensurePool() const;

    i32 _workerCount = 1;

    /*
     * Sized to _workerCount - 1: dispatch() runs the first band on the calling
     * thread rather than idling it. A single-core machine -- and an Emscripten
     * build without pthreads -- therefore creates no threads at all and takes
     * the serial path.
     *
     * Both members are mutable: dispatch() is logically const (it does not
     * change what this object represents), but lazy construction has to write
     * through a const instance to build the pool on that first call.
     */
    mutable std::once_flag _poolOnce;
    mutable std::unique_ptr<ink::ThreadPool> _pool;
};

} // namespace aura3d

#endif // AURA_JOB_SYSTEM_H
