#ifndef AURA_AUDIO_HANDLES_H
#define AURA_AUDIO_HANDLES_H

#pragma once

#include "aura/Core/Handle.h"

namespace aura3d {

//! Tag types distinguishing one resource family from another at compile
//! time. Never defined -- see Handle<Tag>'s class comment.
struct AudioClipTag;
struct AudioSourceTag;

/**
 * @brief An audio clip owned by the AudioEngine: decoded PCM ready to play.
 *
 * The audio counterpart to TextureHandle/MeshHandle, and deliberately the same
 * shape -- an opaque Handle<Tag> sharing isValidHandle()'s semantics with
 * every other resource handle rather than declaring a second convention that
 * means the same thing. Handles stay valid until unloadClip(), or until the
 * AudioEngine that issued them is destroyed.
 */
using AudioClipHandle = Handle<AudioClipTag>;

/**
 * @brief One playing (or paused) instance of a clip: a "voice".
 *
 * Distinct from AudioClipHandle because the relationship is one-to-many — the
 * same footstep clip can be playing four times at four positions, which is four
 * voices over one clip.
 *
 * Voices are recycled. A handle is invalidated when playback finishes or stop()
 * is called, and the underlying slot is then reused by a later play(), so a
 * handle held across frames must be checked with AudioEngine::isPlaying()
 * rather than assumed live. Generation counting makes a stale handle read as
 * "not playing" instead of silently addressing whatever took the slot -- see
 * AudioEngine::makeSourceHandle()/resolveVoice(), which pack and unpack the
 * slot/generation pair into this handle's raw value.
 */
using AudioSourceHandle = Handle<AudioSourceTag>;

} // namespace aura3d

#endif // AURA_AUDIO_HANDLES_H
