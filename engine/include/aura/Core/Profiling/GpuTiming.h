#ifndef AURA_GPUTIMING_H
#define AURA_GPUTIMING_H

#pragma once

#include "aura/aura.h"

namespace aura3d
{

/// GPU time of the most recently *retired* frame, not the one being recorded: reading the current
/// frame would stall the CPU on the GPU. Trails the CPU by the frames in flight.
struct GpuTimingStats
{
    bool available = false; ///< False while timing is off, before the first result, or without a GPU.
    f64 frameMillis = 0.0;  ///< From the frame's first GPU command to its last.

    //! Frames whose results came back unusable (never submitted, device reset, disjoint). A
    //! steadily rising count means the timing measures less of the run than it appears to.
    u64 droppedSamples = 0;
};

} // namespace aura3d

#endif // AURA_GPUTIMING_H
