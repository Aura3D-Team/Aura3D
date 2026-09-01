#ifndef AURA_AUDIO_ENGINE_H
#define AURA_AUDIO_ENGINE_H

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <wma/wma.hpp>

#include "aura/Core/AudioClipLoader/AudioClipLoader.h"
#include "aura/Core/AudioEngine/AudioHandles.h"
#include "aura/aura.h"

namespace aura3d {

/**
 * @brief How a clip's samples reach the mixer.
 */
enum class AudioClipMode : u8 {
    /// Decoded once, held fully in memory. Right choice for sound effects:
    /// playing costs no decoding/allocation, and one buffer backs every
    /// simultaneous voice.
    Static,

    /**
     * @brief Decoded up front but played through a bounded ring buffer.
     *        For music/ambience -- long clips where holding every frame of
     *        PCM is wasteful.
     *
     * @note Currently decodes the whole file at load time too; only playback
     *       differs. Exists as the seam incremental decoding will slot into.
     */
    Streaming
};

/**
 * @struct AudioListener3D
 * @brief Where the ears are, for spatialized voices.
 *
 * Normally driven from the scene camera each frame; see Camera::position() and
 * Camera::forward().
 */
struct AudioListener3D {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

/**
 * @struct AudioSourceDesc
 * @brief Everything needed to start one voice.
 */
struct AudioSourceDesc {
    AudioClipHandle clip;

    //! Restart from the beginning on reaching the end instead of finishing.
    bool loop = false;

    //! Linear gain applied before the master volume. 1.0 is unattenuated.
    f32 gain = 1.0f;

    /// Position this voice in the world rather than playing it flat. A
    /// non-spatial voice is heard at full level in both channels (UI clicks,
    /// music); a spatial one is attenuated by distance and panned by
    /// direction relative to the listener.
    bool spatial = false;

    glm::vec3 position{0.0f};

    //! Distance within which no attenuation is applied. Prevents the gain from
    //! diverging as a source approaches the listener.
    f32 minDistance = 1.0f;

    //! Distance at which the voice is fully attenuated to silence.
    f32 maxDistance = 100.0f;
};

/**
 * @class AudioEngine
 * @brief Clip playback, mixing and 3D spatialization on top of an audio device.
 *
 * Owns the clip cache and voice pool, and supplies wma::IAudioDevice's mixing
 * callback. Handles rather than pointers, matching the renderer's textures and
 * meshes: a voice can end at any moment on the audio thread, so a stale
 * AudioSourceHandle reads as "not playing" instead of dangling.
 *
 * @note Thread-safety: safe to call from the game thread while the device's
 *       audio thread mixes -- shared state sits behind a short plain mutex
 *       (critical sections are pure arithmetic, no allocation or I/O). Not
 *       safe to call from two threads concurrently.
 */
class AudioEngine {
public:
    /**
     * @brief Takes ownership of @p device and starts it.
     *
     * @p device must already be open (wma::openAudioDevice()). A device that
     * fails to start is not fatal -- the engine stays usable, just silent.
     *
     * @param maxVoices Size of the voice pool, fixed for the engine's
     *                  lifetime so the mixer never allocates. At least 1.
     */
    explicit AudioEngine(std::unique_ptr<wma::IAudioDevice> device, u32 maxVoices = 32);

    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /**
     * @brief Loads @p path, converting it to the device's format.
     *
     * @return An always-valid handle. A missing or undecodable file yields a
     *         short silent clip and a warning.
     *
     * @note Not cached here; use ResourceManager::loadSound() for that.
     */
    [[nodiscard]] AudioClipHandle loadClip(const std::string& path,
                                           AudioClipMode mode = AudioClipMode::Static);

    /// Registers already-decoded PCM as a clip, resampled to the device's
    /// rate now rather than during playback. For generated audio; see
    /// AudioClipLoader::makeSineTone().
    [[nodiscard]] AudioClipHandle createClip(const AudioClipData& data,
                                             AudioClipMode mode = AudioClipMode::Static);

    /// Releases @p clip's samples, stopping any voice still playing it first.
    void unloadClip(AudioClipHandle clip) noexcept;

    //! Forgets every clip and stops every voice.
    void unloadAllClips() noexcept;

    /// Starts a voice. @return An invalid handle if @p desc names an unknown
    /// clip or every voice slot is busy -- a dropped sound, not an error.
    [[nodiscard]] AudioSourceHandle play(const AudioSourceDesc& desc);

    //! Convenience overload: play @p clip once, unspatialized, at @p gain.
    [[nodiscard]] AudioSourceHandle play(AudioClipHandle clip, f32 gain = 1.0f);

    //! Ends @p source and frees its slot. Its handle is invalid afterwards.
    void stop(AudioSourceHandle source) noexcept;

    //! Ends every voice. Clips stay loaded.
    void stopAll() noexcept;

    //! Suspends @p source where it is, leaving its handle valid.
    void pause(AudioSourceHandle source) noexcept;

    //! Resumes a paused @p source from where it stopped.
    void resume(AudioSourceHandle source) noexcept;

    //! Whether @p source is a live voice that is not paused. False for a handle
    //! whose voice has finished, been stopped, or had its slot recycled.
    [[nodiscard]] bool isPlaying(AudioSourceHandle source) const noexcept;

    //! Whether @p source is a live voice that is currently paused.
    [[nodiscard]] bool isPaused(AudioSourceHandle source) const noexcept;

    //! Number of voices currently occupying a slot, playing or paused.
    [[nodiscard]] u32 activeVoiceCount() const noexcept;

    void setSourceGain(AudioSourceHandle source, f32 gain) noexcept;
    void setSourcePosition(AudioSourceHandle source, const glm::vec3& position) noexcept;
    void setSourceLooping(AudioSourceHandle source, bool loop) noexcept;

    //! Moves the ears. Takes effect on the next mixed block.
    void setListener(const AudioListener3D& listener) noexcept;
    [[nodiscard]] AudioListener3D listener() const noexcept;

    //! Overall output gain, clamped to [0, 1]. Applied last, after every voice.
    void setMasterVolume(f32 volume) noexcept;
    [[nodiscard]] f32 masterVolume() const noexcept;

    /// Per-frame upkeep: reclaims finished voice slots and tops up streaming
    /// buffers. Skipping it doesn't break playback, but finished voices stop
    /// being recycled and play() eventually starts returning invalid handles.
    void update(f32 deltaSeconds);

    //! The device's granted output format.
    [[nodiscard]] u32 sampleRate() const noexcept;
    [[nodiscard]] u16 channelCount() const noexcept;

    //! Which platform backend is actually driving output.
    [[nodiscard]] wma::AudioBackend backend() const noexcept;

    //! Whether the device is running. False on a machine with no audio hardware
    //! (the null device), where every other operation still behaves normally.
    [[nodiscard]] bool isDeviceRunning() const noexcept;

    /// Re-attempts start() on a device that is open but not running. For the
    /// web: browsers block audio until a user gesture, so a WASM build should
    /// call this from an input handler. No-op once already running.
    void resumeDevice();

private:
    //! Decoded PCM plus the metadata the mixer needs to walk it.
    struct Clip {
        std::vector<f32> samples;
        u16 channelCount = 0;
        AudioClipMode mode = AudioClipMode::Static;
    };

    /// One playing instance of a clip. `generation` makes recycled slots
    /// safe: a stale handle's generation no longer matches its slot's, so the
    /// operation is ignored rather than applied to the voice that replaced it.
    struct Voice {
        AudioClipHandle clip;
        //! Playback position in frames. Plain integer since clips are
        //! resampled to the device rate at load, so there's no fractional
        //! position to track.
        usize cursor = 0;
        f32 gain = 1.0f;
        glm::vec3 position{0.0f};
        bool spatial = false;
        bool loop = false;
        bool active = false;
        bool paused = false;
        f32 minDistance = 1.0f;
        f32 maxDistance = 100.0f;
        u16 generation = 0;
    };

    //! Fills @p output on the device's audio thread. The whole reason the locks
    //! below exist.
    void mix(std::span<f32> output);

    //! Per-channel gains for @p voice, from its position relative to the
    //! listener. Distance attenuation plus equal-power stereo panning.
    void computeSpatialGains(const Voice& voice, f32& leftGain, f32& rightGain) const noexcept;

    //! Splits a handle into slot index and generation, or returns false when it
    //! refers to a slot that has since been recycled.
    [[nodiscard]] bool resolveVoice(AudioSourceHandle handle, usize& slotOut) const noexcept;

    [[nodiscard]] static AudioSourceHandle makeSourceHandle(usize slot, u16 generation) noexcept;

    std::unique_ptr<wma::IAudioDevice> _device;

    //! Guards _voices and _listener: written by the game thread, read by the
    //! audio thread on every block.
    mutable std::mutex _voiceMutex;
    std::vector<Voice> _voices;
    AudioListener3D _listener{};

    //! Guards _clips. Separate from _voiceMutex so that loading a clip on the
    //! game thread does not contend with the mixer, which only reads clips.
    mutable std::mutex _clipMutex;
    std::unordered_map<AudioClipHandle, Clip> _clips;
    //! Raw counter, not AudioClipHandle: it is never itself handed out, only
    //! ever wrapped into one at the point a clip is created (see createClip()).
    AudioClipHandle::ValueType _nextClipHandle = 1;

    //! Atomic rather than mutex-guarded: read once per mixed block and written
    //! from the game thread, with no invariant tying it to anything else.
    std::atomic<f32> _masterVolume{1.0f};

    u32 _sampleRate = 48000;
    u16 _channelCount = 2;
};

} // namespace aura3d

#endif // AURA_AUDIO_ENGINE_H
