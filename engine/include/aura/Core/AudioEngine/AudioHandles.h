#ifndef AURA_AUDIO_HANDLES_H
#define AURA_AUDIO_HANDLES_H

#pragma once

#include "aura/Renderer/RenderHandles.h"

namespace aura3d {

/**
 * @brief An audio clip owned by the AudioEngine: decoded PCM ready to play.
 *
 * The audio counterpart to TextureHandle/MeshHandle, and deliberately the same
 * shape — an opaque u32 sharing INVALID_HANDLE and isValidHandle() from
 * RenderHandles.h rather than declaring a second sentinel that means the same
 * thing. Handles stay valid until unloadClip(), or until the AudioEngine that
 * issued them is destroyed.
 */
using AudioClipHandle = u32;

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
 * "not playing" instead of silently addressing whatever took the slot.
 */
using AudioSourceHandle = u32;

} // namespace aura3d

#endif // AURA_AUDIO_HANDLES_H
