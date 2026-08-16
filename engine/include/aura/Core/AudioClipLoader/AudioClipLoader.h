#ifndef AURA_AUDIO_CLIP_LOADER_H
#define AURA_AUDIO_CLIP_LOADER_H

#pragma once

#include <string>
#include <vector>

#include "aura/aura.h"

namespace aura3d {

/**
 * @struct AudioClipData
 * @brief Decoded, interleaved 32-bit float PCM.
 *
 * Float rather than the 16-bit integers most files store: it is what the mixer
 * sums in and what wma::IAudioDevice consumes, so converting once at load time
 * keeps the per-sample work out of the audio callback.
 */
struct AudioClipData {
    //! frameCount * channelCount samples, interleaved (L, R, L, R, ...),
    //! nominally in [-1, 1].
    std::vector<f32> samples;
    u32 sampleRate   = 0;
    u16 channelCount = 0;

    //! Samples per channel. The clip's duration is this over sampleRate.
    [[nodiscard]]
    u32 frameCount() const noexcept
    {
        return channelCount == 0 ? 0u : static_cast<u32>(samples.size() / channelCount);
    }

    [[nodiscard]]
    f32 durationSeconds() const noexcept
    {
        return sampleRate == 0 ? 0.0f : static_cast<f32>(frameCount()) / static_cast<f32>(sampleRate);
    }

    [[nodiscard]]
    bool valid() const noexcept
    {
        return sampleRate > 0 && channelCount > 0 && !samples.empty() &&
               samples.size() % channelCount == 0;
    }
};

/**
 * @class AudioClipLoader
 * @brief Decodes audio files to float PCM, with an always-available fallback.
 *
 * The audio sibling of ImageLoader: a static utility that turns a path into
 * plain data and knows nothing about the engine. Decoders are an implementation
 * detail, so this header stays free of third-party includes and can remain part
 * of the installed public API.
 *
 * Supported formats:
 *   - WAV (RIFF): 8/16/24/32-bit PCM and 32/64-bit IEEE float, any channel
 *     count. Parsed in-tree — the format is simple enough that a dependency
 *     would cost more than it saves.
 *   - OGG Vorbis: through the vendored stb_vorbis, for music and anything else
 *     long enough that WAV's size becomes a problem.
 *
 * The format is chosen by sniffing the file's magic bytes, not its extension.
 */
class AudioClipLoader {
public:
    /**
     * @brief Decodes @p path to interleaved float PCM.
     *
     * @return The decoded clip, or an invalid AudioClipData when the file is
     *         missing, truncated, or in a format/variant that is not supported.
     *         Callers that need playback to proceed regardless should fall back
     *         to makeSilence(), the way a missing texture falls back to
     *         ImageLoader::makeCheckerboard().
     */
    static AudioClipData loadPCM(const std::string& path);

    /**
     * @brief Builds a silent clip of @p durationSeconds.
     *
     * The audio equivalent of the magenta checkerboard, minus the "look at me":
     * a failed texture should be obvious on screen, whereas a failed sound
     * effect substituting a buzz would be worse than the silence it replaces.
     * Generated in memory, so it is available on every platform regardless of
     * file system access.
     *
     * @param durationSeconds Clamped to at least one frame.
     * @param sampleRate      Frames per second; clamped to at least 1.
     * @param channelCount    Interleaved channels; clamped to at least 1.
     */
    static AudioClipData makeSilence(f32 durationSeconds = 0.25f,
                                     u32 sampleRate = 48000,
                                     u16 channelCount = 2);

    /**
     * @brief Builds a @p frequencyHz sine tone: a test signal with known content.
     *
     * Unlike makeSilence() this is not a fallback — nothing substitutes it
     * automatically. It exists so tests and demos have a clip whose samples are
     * predictable enough to assert on (and audible enough to confirm a device
     * is really running) without shipping a fixture file.
     *
     * @param frequencyHz     Tone frequency; clamped into the audible range.
     * @param durationSeconds Clamped to at least one frame.
     * @param amplitude       Peak amplitude, clamped to [0, 1].
     */
    static AudioClipData makeSineTone(f32 frequencyHz = 440.0f,
                                      f32 durationSeconds = 1.0f,
                                      f32 amplitude = 0.25f,
                                      u32 sampleRate = 48000,
                                      u16 channelCount = 2);
};

} // namespace aura3d

#endif // AURA_AUDIO_CLIP_LOADER_H
