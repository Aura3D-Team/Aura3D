#ifndef AURA_AUDIO_HANDLES_H
#define AURA_AUDIO_HANDLES_H

#pragma once

#include "aura/Core/Handle.h"

namespace aura3d {

//! Tag types distinguishing one resource family at compile time. Never
//! defined; see Handle<Tag>.
struct AudioClipTag;
struct AudioSourceTag;

/// A decoded audio clip owned by the AudioEngine, ready to play. Valid until
/// unloadClip() or AudioEngine destruction.
using AudioClipHandle = Handle<AudioClipTag>;

/**
 * @brief One playing (or paused) instance of a clip: a "voice".
 *
 * One clip can have many voices (e.g. the same footstep sound playing four
 * times at once). A voice's handle is invalidated when playback finishes or
 * stop() is called; check AudioEngine::isPlaying() rather than assuming a
 * held handle is still live.
 */
using AudioSourceHandle = Handle<AudioSourceTag>;

} // namespace aura3d

#endif // AURA_AUDIO_HANDLES_H
