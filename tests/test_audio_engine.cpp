#include "aura/Core/AudioEngine/AudioEngine.h"

#include <cmath>
#include <memory>

#include <wma/audio/backends/null/NullAudioDevice.hpp>

#include "TestUtils.h"

using namespace aura3d;

namespace {

//! Deterministic device format for every case below.
constexpr u32 kRate = 48000;
constexpr u16 kChannels = 2;
constexpr u32 kFramesPerBuffer = 256;

bool approx(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

/**
 * @brief An AudioEngine over a null device, plus the handle to pump it.
 *
 * The null device is what makes these tests possible at all: there is no audio
 * thread and no clock, so a test drives the mixer itself through
 * renderFrames() and inspects exactly what a real device would have played.
 * That also means the suite runs unchanged in CI, where there is no sound
 * hardware to open.
 */
struct TestRig {
    wma::NullAudioDevice* device = nullptr;
    std::unique_ptr<AudioEngine> engine;

    explicit TestRig(u32 maxVoices = 8)
    {
        auto owned = std::make_unique<wma::NullAudioDevice>();
        device = owned.get();

        wma::AudioDeviceConfig config;
        config.sampleRate      = kRate;
        config.channelCount    = kChannels;
        config.framesPerBuffer = kFramesPerBuffer;
        (void)owned->open(config);

        engine = std::make_unique<AudioEngine>(std::move(owned), maxVoices);
    }

    //! Mixes @p frames and returns the interleaved result.
    std::span<const f32> render(u32 frames = kFramesPerBuffer)
    {
        return device->renderFrames(frames);
    }

    //! Largest absolute sample in @p channel of the next mixed block.
    f32 peak(u16 channel, u32 frames = kFramesPerBuffer)
    {
        const std::span<const f32> block = render(frames);
        f32 result = 0.0f;
        for (usize i = channel; i < block.size(); i += kChannels)
            result = std::max(result, std::fabs(block[i]));
        return result;
    }
};

//! A constant-amplitude mono clip: every sample is +amplitude, which makes the
//! gain applied to it directly readable out of the mixed output.
AudioClipData constantClip(f32 amplitude, f32 seconds = 1.0f, u16 channels = 1)
{
    AudioClipData clip;
    clip.sampleRate = kRate;
    clip.channelCount = channels;
    clip.samples.assign(static_cast<usize>(seconds * kRate) * channels, amplitude);
    return clip;
}

void test_device_format_reported()
{
    TestRig rig;

    AURA_CHECK(rig.engine->sampleRate() == kRate, "AudioEngine: reports the device's sample rate");
    AURA_CHECK(rig.engine->channelCount() == kChannels, "AudioEngine: reports the device's channel count");
    AURA_CHECK(rig.engine->backend() == wma::AudioBackend::Null, "AudioEngine: reports the device's backend");
    AURA_CHECK(rig.engine->isDeviceRunning(), "AudioEngine: starts the device it is given");
    AURA_CHECK(rig.engine->activeVoiceCount() == 0, "AudioEngine: starts with no active voices");
}

void test_play_stop_lifecycle()
{
    TestRig rig;

    const AudioClipHandle clip = rig.engine->createClip(constantClip(0.5f));
    AURA_CHECK(isValidHandle(clip), "createClip: returns a valid handle for valid PCM");

    const AudioSourceHandle voice = rig.engine->play(clip);
    AURA_CHECK(isValidHandle(voice), "play: returns a valid voice handle");
    AURA_CHECK(rig.engine->isPlaying(voice), "play: the new voice reports as playing");
    AURA_CHECK(rig.engine->activeVoiceCount() == 1, "play: occupies exactly one voice slot");

    rig.engine->stop(voice);
    AURA_CHECK(!rig.engine->isPlaying(voice), "stop: the voice no longer reports as playing");
    AURA_CHECK(rig.engine->activeVoiceCount() == 0, "stop: frees the voice slot");

    // Repeating an operation on a dead handle must be a no-op, not a crash:
    // game code routinely holds a handle past the end of the sound.
    rig.engine->stop(voice);
    rig.engine->pause(voice);
    rig.engine->setSourceGain(voice, 0.5f);
    AURA_CHECK(!rig.engine->isPlaying(voice), "stop: operating on a stopped handle stays a no-op");
}

void test_invalid_inputs_are_rejected()
{
    TestRig rig;

    AURA_CHECK(!isValidHandle(rig.engine->play(AudioClipHandle{})),
              "play: an invalid clip handle yields no voice");

    // A handle in range but never issued: nothing should resolve it.
    AURA_CHECK(!isValidHandle(rig.engine->play(AudioClipHandle{12345u})),
              "play: an unknown clip handle yields no voice");

    AudioClipData empty;
    AURA_CHECK(!isValidHandle(rig.engine->createClip(empty)),
              "createClip: invalid PCM yields no handle");

    AURA_CHECK(!rig.engine->isPlaying(AudioSourceHandle{}),
              "isPlaying: the invalid sentinel is never playing");
}

void test_missing_file_falls_back_to_silence()
{
    TestRig rig;

    // The documented contract, and the audio counterpart of the checkerboard:
    // a missing asset costs silence, never a failure the caller must handle.
    const AudioClipHandle clip = rig.engine->loadClip("does_not_exist_anywhere.wav");
    AURA_CHECK(isValidHandle(clip), "loadClip: a missing file still yields a usable handle");

    const AudioSourceHandle voice = rig.engine->play(clip);
    AURA_CHECK(isValidHandle(voice), "loadClip: the fallback clip can be played");
    AURA_CHECK(approx(rig.peak(0), 0.0f), "loadClip: the fallback clip is actually silent");
}

void test_pause_and_resume()
{
    TestRig rig;

    const AudioClipHandle clip = rig.engine->createClip(constantClip(1.0f));
    const AudioSourceHandle voice = rig.engine->play(clip, 1.0f);

    AURA_CHECK(rig.peak(0) > 0.5f, "play: an audible voice produces non-zero output");

    rig.engine->pause(voice);
    AURA_CHECK(rig.engine->isPaused(voice), "pause: the voice reports as paused");
    AURA_CHECK(!rig.engine->isPlaying(voice), "pause: a paused voice does not report as playing");
    AURA_CHECK(approx(rig.peak(0), 0.0f), "pause: a paused voice contributes nothing to the mix");

    // Still occupying its slot -- paused is not stopped.
    AURA_CHECK(rig.engine->activeVoiceCount() == 1, "pause: the voice keeps its slot");

    rig.engine->resume(voice);
    AURA_CHECK(rig.engine->isPlaying(voice), "resume: the voice reports as playing again");
    AURA_CHECK(rig.peak(0) > 0.5f, "resume: the voice contributes to the mix again");
}

void test_gain_and_master_volume()
{
    TestRig rig;

    const AudioClipHandle clip = rig.engine->createClip(constantClip(1.0f));
    const AudioSourceHandle voice = rig.engine->play(clip, 0.5f);

    AURA_CHECK(approx(rig.peak(0), 0.5f, 0.01f), "play: per-voice gain scales the output");

    rig.engine->setMasterVolume(0.5f);
    AURA_CHECK(approx(rig.engine->masterVolume(), 0.5f), "setMasterVolume: the value is read back");
    AURA_CHECK(approx(rig.peak(0), 0.25f, 0.01f), "setMasterVolume: master multiplies the voice gain");

    rig.engine->setMasterVolume(0.0f);
    AURA_CHECK(approx(rig.peak(0), 0.0f), "setMasterVolume: zero silences the mix entirely");

    // Out-of-range values clamp rather than producing negative or wildly
    // amplified output.
    rig.engine->setMasterVolume(5.0f);
    AURA_CHECK(approx(rig.engine->masterVolume(), 1.0f), "setMasterVolume: clamps above 1.0");
    rig.engine->setMasterVolume(-3.0f);
    AURA_CHECK(approx(rig.engine->masterVolume(), 0.0f), "setMasterVolume: clamps below 0.0");

    rig.engine->setMasterVolume(1.0f);
    rig.engine->setSourceGain(voice, 0.25f);
    AURA_CHECK(approx(rig.peak(0), 0.25f, 0.01f), "setSourceGain: takes effect on the next block");
}

/*
 * Regression coverage for a bug where mix() returned before touching any
 * voice at all when master volume was 0: cursors never advanced and
 * non-looping voices never retired while muted. Two symptoms, tested
 * separately below -- a one-shot fired while muted staying "playing" forever
 * (silently exhausting the voice pool), and a looping voice resuming from
 * frame 0 on unmute instead of from wherever a further-forward cursor would
 * have been.
 */

void test_muted_one_shot_still_retires()
{
    TestRig rig;

    // Two frames long, so a single kFramesPerBuffer block runs it to
    // completion, same shape as test_looping_and_natural_end's short clip.
    const AudioClipHandle shortClip = rig.engine->createClip(constantClip(1.0f, 2.0f / kRate));

    rig.engine->setMasterVolume(0.0f);

    const AudioSourceHandle oneShot = rig.engine->play(shortClip, 1.0f);
    (void)rig.render();
    rig.engine->update(0.016f);

    AURA_CHECK(!rig.engine->isPlaying(oneShot),
              "mix: a non-looping voice retires on schedule even while muted");
    AURA_CHECK(rig.engine->activeVoiceCount() == 0,
              "update: a voice that finished while muted still releases its slot");

    // The failure mode this guards: a muted engine that never retires voices
    // silently exhausts a fixed-size pool. Confirm the slot just freed is
    // actually usable rather than merely reporting activeVoiceCount() == 0.
    AURA_CHECK(isValidHandle(rig.engine->play(shortClip, 1.0f)),
              "play: the slot freed while muted is reusable");
}

void test_muted_playback_advances_cursor()
{
    TestRig rig;

    /*
     * A monotonically increasing ramp: peak() of a rendered block is the
     * value at the block's last frame (the running maximum of a non-negative,
     * non-decreasing sequence), which makes the mixer's read position directly
     * observable from the output alone -- no cursor accessor needed.
     */
    constexpr u32 kRampFrames = kFramesPerBuffer * 4;
    AudioClipData ramp;
    ramp.sampleRate = kRate;
    ramp.channelCount = 1;
    ramp.samples.resize(kRampFrames);
    for (u32 i = 0; i < kRampFrames; ++i)
        ramp.samples[i] = static_cast<f32>(i) / static_cast<f32>(kRampFrames - 1);

    const AudioClipHandle clip = rig.engine->createClip(ramp);

    AudioSourceDesc desc;
    desc.clip = clip;
    desc.loop = true;
    desc.gain = 1.0f;
    (void)rig.engine->play(desc);

    rig.engine->setMasterVolume(0.0f);
    AURA_CHECK(approx(rig.peak(0, kFramesPerBuffer), 0.0f),
              "muted: nothing is written to the output while silenced");

    rig.engine->setMasterVolume(1.0f);
    const f32 peakAfterMutedBlock = rig.peak(0, kFramesPerBuffer);

    // Two candidate readings for the very next audible block: the ramp's value
    // at the end of the *second* window, [kFramesPerBuffer, 2*kFramesPerBuffer),
    // if the cursor advanced through the muted block as it should; the value at
    // the end of the *first* window if the pre-fix bug left the cursor frozen
    // at 0 while muted, so unmuting replayed from the start.
    const f32 expectedIfAdvanced = ramp.samples[2 * kFramesPerBuffer - 1];
    const f32 expectedIfFrozen   = ramp.samples[kFramesPerBuffer - 1];

    AURA_CHECK(approx(peakAfterMutedBlock, expectedIfAdvanced, 0.01f),
              "mix: the cursor keeps advancing through a muted block");
    AURA_CHECK(!approx(peakAfterMutedBlock, expectedIfFrozen, 0.05f),
              "mix: unmuting does not resume playback from where it was muted");
}

void test_output_is_clamped()
{
    TestRig rig;

    // Several loud voices summed together exceed full scale. The mixer must
    // clamp: an unclamped sum wraps around, which is an audible click rather
    // than the graceful distortion clipping gives.
    const AudioClipHandle clip = rig.engine->createClip(constantClip(1.0f));
    for (int i = 0; i < 6; ++i)
        (void)rig.engine->play(clip, 1.0f);

    const std::span<const f32> block = rig.render();
    bool withinRange = true;
    for (const f32 sample : block) {
        if (sample < -1.0f || sample > 1.0f) { withinRange = false; break; }
    }
    AURA_CHECK(withinRange, "mix: summed output is clamped to [-1, 1] rather than wrapping");
}

void test_looping_and_natural_end()
{
    TestRig rig;

    // Two frames long, so a single 256-frame block runs far past its end.
    const AudioClipHandle shortClip = rig.engine->createClip(constantClip(1.0f, 2.0f / kRate));

    const AudioSourceHandle oneShot = rig.engine->play(shortClip, 1.0f);
    (void)rig.render();
    rig.engine->update(0.016f);
    AURA_CHECK(!rig.engine->isPlaying(oneShot), "mix: a non-looping voice ends when the clip runs out");
    AURA_CHECK(rig.engine->activeVoiceCount() == 0, "update: a finished voice releases its slot");

    AudioSourceDesc desc;
    desc.clip = shortClip;
    desc.loop = true;
    desc.gain = 1.0f;
    const AudioSourceHandle looping = rig.engine->play(desc);

    (void)rig.render();
    rig.engine->update(0.016f);
    AURA_CHECK(rig.engine->isPlaying(looping), "mix: a looping voice survives passing the clip's end");
    // A looping voice keeps producing sound rather than falling silent after
    // the first pass through the clip.
    AURA_CHECK(rig.peak(0) > 0.5f, "mix: a looping voice keeps producing output");

    rig.engine->setSourceLooping(looping, false);
    (void)rig.render();
    rig.engine->update(0.016f);
    AURA_CHECK(!rig.engine->isPlaying(looping), "setSourceLooping: clearing the flag lets the voice end");
}

void test_voice_pool_exhaustion()
{
    TestRig rig(4);

    const AudioClipHandle clip = rig.engine->createClip(constantClip(0.1f));

    for (int i = 0; i < 4; ++i)
        AURA_CHECK(isValidHandle(rig.engine->play(clip, 0.1f)),
                  "play: voices are handed out up to the pool size");

    // Dropping the newest request is deliberate: cutting off something already
    // audible to make room would be more noticeable than one missing sound.
    AURA_CHECK(!isValidHandle(rig.engine->play(clip, 0.1f)),
              "play: an exhausted pool drops the request rather than stealing a voice");

    rig.engine->stopAll();
    AURA_CHECK(rig.engine->activeVoiceCount() == 0, "stopAll: releases every voice");
    AURA_CHECK(isValidHandle(rig.engine->play(clip, 0.1f)),
              "play: slots are reusable once freed");
}

void test_recycled_slot_invalidates_old_handle()
{
    TestRig rig(1);

    const AudioClipHandle clip = rig.engine->createClip(constantClip(0.5f));

    const AudioSourceHandle first = rig.engine->play(clip);
    rig.engine->stop(first);

    // The pool has exactly one slot, so this necessarily reuses it. The old
    // handle must not address the new voice -- that is precisely what the
    // generation counter exists to prevent.
    const AudioSourceHandle second = rig.engine->play(clip);

    AURA_CHECK(isValidHandle(second), "play: the freed slot is reused");
    AURA_CHECK(first != second, "play: a recycled slot issues a distinct handle");
    AURA_CHECK(!rig.engine->isPlaying(first), "play: the stale handle does not resolve to the new voice");
    AURA_CHECK(rig.engine->isPlaying(second), "play: the new handle resolves correctly");

    // And a stale handle must not be able to mutate the live voice.
    rig.engine->stop(first);
    AURA_CHECK(rig.engine->isPlaying(second), "stop: a stale handle cannot stop the voice that replaced it");
}

void test_spatial_panning()
{
    TestRig rig;

    const AudioClipHandle clip = rig.engine->createClip(constantClip(1.0f));

    // Listener at the origin looking down -Z, which makes +X its right.
    rig.engine->setListener({.position = glm::vec3(0.0f),
                             .forward  = glm::vec3(0.0f, 0.0f, -1.0f),
                             .up       = glm::vec3(0.0f, 1.0f, 0.0f)});

    AudioSourceDesc desc;
    desc.clip        = clip;
    desc.loop        = true;
    desc.gain        = 1.0f;
    desc.spatial     = true;
    desc.minDistance = 100.0f;  // no distance attenuation, isolating the pan
    desc.maxDistance = 200.0f;
    desc.position    = glm::vec3(10.0f, 0.0f, 0.0f); // hard right

    const AudioSourceHandle voice = rig.engine->play(desc);
    AURA_CHECK(isValidHandle(voice), "play: a spatial voice starts");

    {
        const std::span<const f32> block = rig.render(64);
        f32 left = 0.0f, right = 0.0f;
        for (usize i = 0; i + 1 < block.size(); i += 2) {
            left  = std::max(left,  std::fabs(block[i]));
            right = std::max(right, std::fabs(block[i + 1]));
        }
        AURA_CHECK(right > left, "mix: a source to the listener's right is louder in the right channel");
        AURA_CHECK(left < 0.1f, "mix: a hard-right source is nearly absent from the left channel");
    }

    rig.engine->setSourcePosition(voice, glm::vec3(-10.0f, 0.0f, 0.0f)); // hard left
    {
        const std::span<const f32> block = rig.render(64);
        f32 left = 0.0f, right = 0.0f;
        for (usize i = 0; i + 1 < block.size(); i += 2) {
            left  = std::max(left,  std::fabs(block[i]));
            right = std::max(right, std::fabs(block[i + 1]));
        }
        AURA_CHECK(left > right, "setSourcePosition: moving a source left swaps the louder channel");
    }

    // Directly ahead is equidistant from both ears, so the pan must be centred.
    rig.engine->setSourcePosition(voice, glm::vec3(0.0f, 0.0f, -10.0f));
    {
        const std::span<const f32> block = rig.render(64);
        f32 left = 0.0f, right = 0.0f;
        for (usize i = 0; i + 1 < block.size(); i += 2) {
            left  = std::max(left,  std::fabs(block[i]));
            right = std::max(right, std::fabs(block[i + 1]));
        }
        AURA_CHECK(approx(left, right, 0.02f), "mix: a source straight ahead is centred between the channels");
    }
}

void test_spatial_distance_attenuation()
{
    TestRig rig;

    const AudioClipHandle clip = rig.engine->createClip(constantClip(1.0f));

    rig.engine->setListener({.position = glm::vec3(0.0f),
                             .forward  = glm::vec3(0.0f, 0.0f, -1.0f),
                             .up       = glm::vec3(0.0f, 1.0f, 0.0f)});

    AudioSourceDesc desc;
    desc.clip        = clip;
    desc.loop        = true;
    desc.gain        = 1.0f;
    desc.spatial     = true;
    desc.minDistance = 1.0f;
    desc.maxDistance = 10.0f;
    desc.position    = glm::vec3(0.0f, 0.0f, -1.0f); // inside minDistance

    const AudioSourceHandle voice = rig.engine->play(desc);

    const f32 near = std::max(rig.peak(0, 64), rig.peak(1, 64));

    rig.engine->setSourcePosition(voice, glm::vec3(0.0f, 0.0f, -5.0f)); // mid-range
    const f32 mid = std::max(rig.peak(0, 64), rig.peak(1, 64));

    rig.engine->setSourcePosition(voice, glm::vec3(0.0f, 0.0f, -50.0f)); // beyond maxDistance
    const f32 far = std::max(rig.peak(0, 64), rig.peak(1, 64));

    AURA_CHECK(near > mid, "mix: a nearer source is louder than a mid-range one");
    AURA_CHECK(mid > far, "mix: a mid-range source is louder than a distant one");
    AURA_CHECK(approx(far, 0.0f), "mix: a source past maxDistance is fully attenuated to silence");

    // A non-spatial voice ignores position entirely -- that is what makes it the
    // right choice for music and UI sounds.
    AudioSourceDesc flat;
    flat.clip = clip;
    flat.loop = true;
    flat.gain = 1.0f;
    flat.spatial = false;
    flat.position = glm::vec3(0.0f, 0.0f, -1000.0f);

    rig.engine->stop(voice);
    (void)rig.engine->play(flat);
    AURA_CHECK(rig.peak(0, 64) > 0.5f, "mix: a non-spatial voice is unaffected by its position");
}

void test_clip_unload_stops_voices()
{
    TestRig rig;

    const AudioClipHandle clip = rig.engine->createClip(constantClip(1.0f));
    const AudioSourceHandle voice = rig.engine->play(clip, 1.0f);

    AURA_CHECK(rig.engine->isPlaying(voice), "play: the voice starts");

    // Unloading must stop the voices using the clip first -- otherwise the
    // mixer would read samples that have just been freed.
    rig.engine->unloadClip(clip);

    AURA_CHECK(!rig.engine->isPlaying(voice), "unloadClip: stops voices still playing that clip");
    AURA_CHECK(approx(rig.peak(0), 0.0f), "unloadClip: nothing is left playing afterwards");
    AURA_CHECK(!isValidHandle(rig.engine->play(clip)), "unloadClip: the clip handle no longer resolves");
}

void test_resampling_preserves_duration()
{
    TestRig rig;

    // A clip at half the device rate must come out twice as long in frames, or
    // it would play back at double speed.
    AudioClipData halfRate;
    halfRate.sampleRate = kRate / 2;
    halfRate.channelCount = 1;
    halfRate.samples.assign(kRate / 2, 0.5f); // exactly 1 second

    const AudioClipHandle clip = rig.engine->createClip(halfRate);
    AURA_CHECK(isValidHandle(clip), "createClip: accepts a clip at a different rate than the device");

    AudioSourceDesc desc;
    desc.clip = clip;
    desc.gain = 1.0f;
    const AudioSourceHandle voice = rig.engine->play(desc);

    // One second at the device rate is far more than a single block, so a clip
    // that was NOT resampled would already have run out here.
    for (int block = 0; block < 4; ++block)
        (void)rig.render();
    rig.engine->update(0.016f);

    AURA_CHECK(rig.engine->isPlaying(voice),
              "createClip: a half-rate clip is resampled rather than playing at double speed");
    AURA_CHECK(approx(rig.peak(0), 0.5f, 0.01f),
              "createClip: resampling preserves the sample values");
}

} // namespace

int main()
{
    test_device_format_reported();
    test_play_stop_lifecycle();
    test_invalid_inputs_are_rejected();
    test_missing_file_falls_back_to_silence();
    test_pause_and_resume();
    test_gain_and_master_volume();
    test_muted_one_shot_still_retires();
    test_muted_playback_advances_cursor();
    test_output_is_clamped();
    test_looping_and_natural_end();
    test_voice_pool_exhaustion();
    test_recycled_slot_invalidates_old_handle();
    test_spatial_panning();
    test_spatial_distance_attenuation();
    test_clip_unload_stops_voices();
    test_resampling_preserves_duration();
    AURA_TEST_MAIN_RETURN();
}
