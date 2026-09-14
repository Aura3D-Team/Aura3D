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
#include "aura/Utils/AudioMailbox.h"
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

    /// Long-form mode marker; currently fully decoded and mixed like Static.
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
 * Control calls are serialized outside the audio thread. Each voice has a
 * bounded latest-state mailbox: repeated controls before a block coalesce.
 * Mixing performs no allocation, reclamation, or locking.
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

    /// Stops its voices. Samples are freed once the audio thread has
    /// acknowledged the stop (here or in a later update()), or at once when
    /// no callback can be running.
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

    /// Clips whose samples are still held, including unloaded ones awaiting
    /// the audio thread's acknowledgement.
    [[nodiscard]] usize clipCount() const noexcept;

    void setSourceGain(AudioSourceHandle source, f32 gain) noexcept;
    void setSourcePosition(AudioSourceHandle source, const glm::vec3& position) noexcept;
    void setSourceLooping(AudioSourceHandle source, bool loop) noexcept;

    //! Moves the ears. Takes effect on the next mixed block.
    void setListener(const AudioListener3D& listener) noexcept;
    [[nodiscard]] AudioListener3D listener() const noexcept;

    //! Overall output gain, clamped to [0, 1]. Applied last, after every voice.
    void setMasterVolume(f32 volume) noexcept;
    [[nodiscard]] f32 masterVolume() const noexcept;

    /// Reclaims retired PCM after two callback boundaries. Safe to skip;
    /// reclamation then waits for the next unload, update() or destruction.
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
        //! Unloaded by the game thread; kept alive until the mixer has moved past it.
        bool retired = false;
        //! Value of _mixEpoch at which the samples are provably unread. Two
        //! callbacks past the retirement: the one that may have been mid-block
        //! when it happened, and the one that consumed the stop.
        u32 retireAfter = 0;
    };

    /// One playing instance of a clip. `generation` makes recycled slots
    /// safe: a stale handle's generation no longer matches its slot's, so the
    /// operation is ignored rather than applied to the voice that replaced it.
    struct Voice {
        AudioClipHandle clip;
        //! Resolved once on play() so the mixer never touches the clip map.
        //! Valid until the clip's retireAfter epoch; see reclaimClips().
        const Clip* data = nullptr;
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
        //! Bumped per play() and echoed back in VoiceChannel::finished. Wider
        //! than `generation` so a wrapped handle cannot be mistaken for a
        //! completed one.
        u32 serial = 0;
    };

    //! The device's mix callback. Runs on the audio thread: consumes the
    //! mailboxes, mixes _mixVoices into @p output, then acknowledges finished
    //! voices and advances _mixEpoch. No allocation, no locks.
    void mix(std::span<f32> output);

    //! Per-channel gains for @p voice, from its position relative to the
    //! listener. Distance attenuation plus equal-power stereo panning.
    void computeSpatialGains(const Voice& voice, f32& leftGain, f32& rightGain) const noexcept;

    //! Splits a handle into slot index and generation, or returns false when it
    //! refers to a slot that has since been recycled.
    [[nodiscard]] bool resolveVoice(AudioSourceHandle handle, usize& slotOut) const noexcept;

    //! Packs a slot index and generation into one handle; see resolveVoice().
    [[nodiscard]] static AudioSourceHandle makeSourceHandle(usize slot, u16 generation) noexcept;

    std::unique_ptr<wma::IAudioDevice> _device;

    //! The game -> audio link for one voice slot.
    struct VoiceChannel {
        //! Latest desired Voice state; the mixer takes it at the top of each block.
        AudioMailbox<Voice> commands;
        //! Serial of the last voice the mixer saw end in this slot. The game
        //! thread compares it against Voice::serial to know the slot is free.
        std::atomic<u32> finished{0};
    };

    //! Publishes _voices[index] to its channel. Call with _voiceMutex held.
    void publishVoice(usize index) noexcept;
    //! Stops every voice on @p handle and marks the clip for reclamation.
    //! Call with _voiceMutex held.
    void retireClip(AudioClipHandle handle) noexcept;
    //! Frees retired clips the mixer can no longer be reading -- past their
    //! epoch, or at once when no callback runs. Call with _voiceMutex held.
    void reclaimClips() noexcept;

    //! Serializes the control API. Never taken by the audio thread.
    mutable std::mutex _voiceMutex;
    //! Desired state, owned by the game thread; what the API reads and writes.
    std::vector<Voice> _voices;
    //! Playback state, owned by the audio thread: the cursors actually advancing.
    std::vector<Voice> _mixVoices;
    //! One channel per slot, indexed like _voices and _mixVoices.
    std::unique_ptr<VoiceChannel[]> _channels;
    //! Listener as last set by the game thread.
    AudioListener3D _listener{};
    //! Listener the mixer spatializes against.
    AudioListener3D _mixListener{};
    //! Carries _listener to _mixListener; consumed once per block.
    AudioMailbox<AudioListener3D> _listenerCommands;
    //! Incremented at the end of every mix(); the clock reclaimClips() waits on.
    std::atomic<u32> _mixEpoch{0};
    //! False while the device is the engine's own null fallback or failed to
    //! start: no callback advances _mixEpoch, so retirement cannot wait on it.
    bool _mixerLive = true;
    std::unordered_map<AudioClipHandle, std::unique_ptr<Clip>> _clips;
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
