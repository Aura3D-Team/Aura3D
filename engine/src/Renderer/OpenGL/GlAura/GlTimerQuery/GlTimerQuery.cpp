#include "aura/Renderer/OpenGL/GlAura/GlTimerQuery/GlTimerQuery.h"

#ifdef AURA_PROFILE_FRAME

#include <cstring>

#include <ink/Inkogger.h>

#include "aura/Core/Profiling/FrameProfiler.h"

namespace aura3d
{
namespace gl
{

namespace
{

#ifdef AURA_GLES
constexpr GLenum kTimeElapsed = GL_TIME_ELAPSED_EXT;

//! WebGL 2 exposes the extension under its own name; Emscripten prefixes both with GL_.
[[nodiscard]] bool hasTimerQueries() noexcept
{
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i)
    {
        const auto *name = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (name != nullptr && (std::strcmp(name, "GL_EXT_disjoint_timer_query") == 0 ||
                                std::strcmp(name, "GL_EXT_disjoint_timer_query_webgl2") == 0))
            return true;
    }
    return false;
}

//! Also clears the flag, so each call reports disjoint events since the last one.
[[nodiscard]] bool disjoint() noexcept
{
    GLint value = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &value);
    return value != 0;
}
#else
constexpr GLenum kTimeElapsed = GL_TIME_ELAPSED;

//! Core since 3.3, which this backend requires; checked so a stripped loader cannot crash.
[[nodiscard]] bool hasTimerQueries() noexcept
{
    return glGenQueries != nullptr && glBeginQuery != nullptr && glGetQueryObjectuiv != nullptr;
}

[[nodiscard]] constexpr bool disjoint() noexcept
{
    return false;
}
#endif

} // namespace

GlTimerQuery::~GlTimerQuery()
{
    if (_queries[0] != 0)
        glDeleteQueries(static_cast<GLsizei>(kSlots), _queries.data());
}

bool GlTimerQuery::create() noexcept
{
    if (!hasTimerQueries())
    {
        INK_INFO << "GlTimerQuery: the context has no timer queries; GPU timing disabled";
        return false;
    }
    glGenQueries(static_cast<GLsizei>(kSlots), _queries.data());
    //! Starts the disjoint flag clean, so the first frames are not discarded for history.
    (void)disjoint();
    return _queries[0] != 0;
}

void GlTimerQuery::collect() noexcept
{
    if (disjoint())
    {
        _droppedSamples += _pending;
        _pending = 0;
        return;
    }

    while (_pending > 0)
    {
        const GLuint query = _queries[(_next + kSlots - _pending) % kSlots];
        GLuint available = 0;
        glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == 0)
            break;
        //! 32 bits of nanoseconds is four seconds, and GLES has no 64-bit read without another extension.
        GLuint nanos = 0;
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &nanos);
        _lastFrameNanos = static_cast<i64>(nanos);
        ++_resolvedSamples;
        --_pending;
        AURA_FRAME_GPU(_lastFrameNanos);
    }
}

void GlTimerQuery::begin() noexcept
{
    collect();
    if (_open || _pending == kSlots)
    {
        ++_droppedSamples;
        return;
    }
    glBeginQuery(kTimeElapsed, _queries[_next]);
    _open = true;
}

void GlTimerQuery::end() noexcept
{
    if (!_open)
        return;
    glEndQuery(kTimeElapsed);
    _open = false;
    _next = (_next + 1) % kSlots;
    ++_pending;
}

GpuTimingStats GlTimerQuery::stats() const noexcept
{
    GpuTimingStats timing;
    timing.available = _resolvedSamples > 0;
    timing.frameMillis = static_cast<f64>(_lastFrameNanos) / 1.0e6;
    timing.droppedSamples = _droppedSamples;
    return timing;
}

} // namespace gl
} // namespace aura3d

#endif // AURA_PROFILE_FRAME
