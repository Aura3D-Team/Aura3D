#ifndef GLTIMERQUERY_H
#define GLTIMERQUERY_H

#pragma once

#include <array>

#include <glad/glad.h>

#include "aura/Core/AuraCore.h"
#include "aura/Core/Profiling/GpuTiming.h"

namespace aura3d
{
namespace gl
{

/// GPU frame time through a ring of GL_TIME_ELAPSED queries, read without blocking once the GPU
/// has finished them. GLES and WebGL need EXT_disjoint_timer_query; a disjoint event, such as a
/// GPU frequency change, discards the frames it spans.
class GlTimerQuery
{
  public:
    GlTimerQuery() noexcept = default;
    ~GlTimerQuery();

    GlTimerQuery(const GlTimerQuery &) = delete;
    GlTimerQuery &operator=(const GlTimerQuery &) = delete;

    /// @return false when the context cannot time: the caller reports GPU timing unavailable.
    [[nodiscard]] bool create() noexcept;

    /// Collects every finished frame into stats() and the frame profiler, then opens this
    /// frame's query. A ring with no free slot, a GPU four frames behind, skips the frame.
    void begin() noexcept;
    void end() noexcept;

    [[nodiscard]] GpuTimingStats stats() const noexcept;

  private:
    static constexpr u32 kSlots = 4;

    void collect() noexcept;

    std::array<GLuint, kSlots> _queries{};
    u32 _next = 0;
    u32 _pending = 0;
    bool _open = false;
    i64 _lastFrameNanos = 0;
    u64 _resolvedSamples = 0;
    u64 _droppedSamples = 0;
};

} // namespace gl
} // namespace aura3d

#endif // GLTIMERQUERY_H
