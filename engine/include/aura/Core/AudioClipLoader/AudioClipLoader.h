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
 * Float rather than the 16-bit integers most files store, since that is what
 * the mixer and wma::IAudioDevice both consume -- converted once at load time
 * rather than per sample in the audio callback.
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
 * Supports WAV (RIFF: 8/16/24/32-bit PCM, 32/64-bit float, any channel count,
 * parsed in-tree) and OGG Vorbis (via the vendored stb_vorbis). Format is
 * chosen by sniffing magic bytes, not the file extension.
 */
class AudioClipLoader {
public:
    /// Decodes @p path to interleaved float PCM. Returns an invalid
    /// AudioClipData if missing, truncated, or unsupported; see makeSilence()
    /// for a fallback that lets playback continue regardless.
    static AudioClipData loadPCM(const std::string& path);

    /**
     * @brief Builds a silent clip of @p durationSeconds. Generated in memory,
     *        so always available regardless of file system access.
     *
     * @param durationSeconds Clamped to at least one frame.
     * @param sampleRate      Frames per second; clamped to at least 1.
     * @param channelCount    Interleaved channels; clamped to at least 1.
     */
    static AudioClipData makeSilence(f32 durationSeconds = 0.25f,
                                     u32 sampleRate = 48000,
                                     u16 channelCount = 2);

    /**
     * @brief Builds a @p frequencyHz sine tone: a test signal with known
     *        content, for tests and demos that need predictable samples.
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
