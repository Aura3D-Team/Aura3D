#include "aura/Core/Profiling/FrameProfiler.h"

#ifdef AURA_PROFILE_FRAME

#include <ink/Inkogger.h>

namespace aura3d {

namespace {

//! Report cadence. Long enough that the log itself is not the workload at
//! several thousand frames a second, short enough to react to a scene change.
constexpr u32 kFramesPerReport = 2000;

} // namespace

void FrameProfiler::endFrame() noexcept
{
    if (++_frames < kFramesPerReport)
        return;

    const auto now = Clock::now();
    const f64 windowMicros = std::chrono::duration<f64, std::micro>(now - _windowStart).count();
    const f64 frameMicros = windowMicros / static_cast<f64>(_frames);

    /*
     * Phases are reported against measured wall time per frame, not against
     * their own sum: the two differ by whatever the loop spends outside any
     * scope (window event pump, application logic), and that gap is itself a
     * finding. Printing the total makes it visible rather than silently
     * redistributing it across the phases.
     */
    i64 accountedNanos = 0;
    for (const i64 phaseNanos : _totals)
        accountedNanos += phaseNanos;

    const f64 accountedMicros =
        static_cast<f64>(accountedNanos) / 1000.0 / static_cast<f64>(_frames);

    INK_INFO << "frame: " << frameMicros << " us (" << (1.0e6 / frameMicros)
             << " FPS) over " << _frames << " frames";

    for (u32 i = 0; i < static_cast<u32>(FramePhase::COUNT); ++i)
    {
        const f64 phaseMicros =
            static_cast<f64>(_totals[i]) / 1000.0 / static_cast<f64>(_frames);

        INK_INFO << "    " << toString(static_cast<FramePhase>(i)) << ": "
                 << phaseMicros << " us (" << (100.0 * phaseMicros / frameMicros) << "%)";
    }

    INK_INFO << "    [unscoped]: " << (frameMicros - accountedMicros) << " us";

    _totals.fill(0);
    _frames = 0;
    _windowStart = now;
}

} // namespace aura3d

#endif // AURA_PROFILE_FRAME
