#include "aura/Core/AudioClipLoader/AudioClipLoader.h"

#include <cmath>
#include <string>

#include "TestUtils.h"

using namespace aura3d;

namespace {

std::string assetPath(const std::string& name)
{
    return std::string(AURA_TEST_ASSETS_DIR) + "/" + name;
}

//! PCM conversion is exact for the values the fixtures use, but comparing
//! floats for equality is still the wrong habit -- one epsilon of slack costs
//! nothing and keeps the tests meaningful if the conversion is ever reworked.
bool approx(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

// tests/assets/valid_audio.wav is 4 stereo frames of 16-bit PCM at 44.1 kHz,
// with one exact known value per sample:
//   frame 0: (0, 16384)  frame 1: (-32768, 32767)
//   frame 2: (16384, 0)  frame 3: (-16384, 32767)
void test_load_valid_wav()
{
    const AudioClipData clip = AudioClipLoader::loadPCM(assetPath("valid_audio.wav"));

    AURA_CHECK(clip.valid(), "loadPCM: valid_audio.wav decodes to a valid AudioClipData");
    AURA_CHECK(clip.sampleRate == 44100, "loadPCM: valid_audio.wav reports 44100 Hz");
    AURA_CHECK(clip.channelCount == 2, "loadPCM: valid_audio.wav reports 2 channels");
    AURA_CHECK(clip.frameCount() == 4, "loadPCM: valid_audio.wav has 4 frames");
    AURA_CHECK(clip.samples.size() == 8, "loadPCM: 4 stereo frames yield 8 interleaved samples");

    AURA_CHECK(approx(clip.samples[0], 0.0f), "loadPCM: silence decodes to 0.0");
    AURA_CHECK(approx(clip.samples[1], 0.5f), "loadPCM: 16384 decodes to +0.5");
    AURA_CHECK(approx(clip.samples[2], -1.0f), "loadPCM: -32768 decodes to -1.0 (the negative full scale)");
    AURA_CHECK(approx(clip.samples[3], 1.0f, 1e-3f), "loadPCM: 32767 decodes to approximately +1.0");

    AURA_CHECK(approx(clip.durationSeconds(), 4.0f / 44100.0f),
              "loadPCM: duration is frameCount over sampleRate");
}

// mono8.wav is 4 frames of unsigned 8-bit PCM at 8 kHz: 128 is silence, not 0.
void test_load_8bit_mono_wav()
{
    const AudioClipData clip = AudioClipLoader::loadPCM(assetPath("mono8.wav"));

    AURA_CHECK(clip.valid(), "loadPCM: mono8.wav decodes to a valid AudioClipData");
    AURA_CHECK(clip.sampleRate == 8000, "loadPCM: mono8.wav reports 8000 Hz");
    AURA_CHECK(clip.channelCount == 1, "loadPCM: mono8.wav reports 1 channel");
    AURA_CHECK(clip.frameCount() == 4, "loadPCM: mono8.wav has 4 frames");

    // 8-bit WAV is stored unsigned around a 128 midpoint, unlike every other
    // width, which is signed. Getting this wrong offsets the whole clip.
    AURA_CHECK(approx(clip.samples[0], 0.0f), "loadPCM: 8-bit 128 is silence, not full scale");
    AURA_CHECK(approx(clip.samples[1], 0.9922f, 1e-3f), "loadPCM: 8-bit 255 is near +1.0");
    AURA_CHECK(approx(clip.samples[2], -1.0f), "loadPCM: 8-bit 0 is -1.0");
    AURA_CHECK(approx(clip.samples[3], 0.5f), "loadPCM: 8-bit 192 is +0.5");
}

// float32.wav stores IEEE floats rather than integers: no normalization applies.
void test_load_float32_wav()
{
    const AudioClipData clip = AudioClipLoader::loadPCM(assetPath("float32.wav"));

    AURA_CHECK(clip.valid(), "loadPCM: float32.wav decodes to a valid AudioClipData");
    AURA_CHECK(clip.sampleRate == 48000, "loadPCM: float32.wav reports 48000 Hz");
    AURA_CHECK(clip.channelCount == 1, "loadPCM: float32.wav reports 1 channel");

    AURA_CHECK(approx(clip.samples[0], 0.0f),  "loadPCM: float 0.0 passes through unchanged");
    AURA_CHECK(approx(clip.samples[1], 0.5f),  "loadPCM: float 0.5 passes through unchanged");
    AURA_CHECK(approx(clip.samples[2], -0.5f), "loadPCM: float -0.5 passes through unchanged");
    AURA_CHECK(approx(clip.samples[3], 1.0f),  "loadPCM: float 1.0 passes through unchanged");
}

// tests/assets/valid_audio.ogg is music.wav re-encoded to stereo 48 kHz Vorbis.
// Vorbis is lossy, so the samples themselves cannot be asserted exactly -- what
// matters here is that the container is recognized, stb_vorbis is wired up, and
// the decoded stream carries the format and length the file claims.
void test_load_ogg_vorbis()
{
    const AudioClipData clip = AudioClipLoader::loadPCM(assetPath("valid_audio.ogg"));

    AURA_CHECK(clip.valid(), "loadPCM: valid_audio.ogg decodes to a valid AudioClipData");
    AURA_CHECK(clip.sampleRate == 48000, "loadPCM: valid_audio.ogg reports 48000 Hz");
    AURA_CHECK(clip.channelCount == 2, "loadPCM: valid_audio.ogg reports 2 channels");

    // Encoders pad to a whole block, so the decoded length is close to but not
    // exactly the source duration.
    AURA_CHECK(approx(clip.durationSeconds(), 1.68f, 0.05f),
              "loadPCM: valid_audio.ogg decodes to roughly its 1.68s source duration");

    // Lossy or not, a decode that produced only silence would mean the samples
    // never made it out of stb_vorbis.
    f32 peak = 0.0f;
    for (const f32 sample : clip.samples)
        peak = std::max(peak, std::fabs(sample));

    AURA_CHECK(peak > 0.05f, "loadPCM: the decoded Ogg carries actual signal, not silence");
    AURA_CHECK(peak <= 1.0f, "loadPCM: decoded Ogg samples stay within [-1, 1]");
}

void test_load_corrupt_and_missing()
{
    const AudioClipData corrupt = AudioClipLoader::loadPCM(assetPath("corrupt_audio.wav"));
    AURA_CHECK(!corrupt.valid(), "loadPCM: a truncated WAV is reported invalid, not silently accepted");
    AURA_CHECK(corrupt.samples.empty(), "loadPCM: a truncated WAV yields no samples");

    const AudioClipData missing = AudioClipLoader::loadPCM(assetPath("does_not_exist.wav"));
    AURA_CHECK(!missing.valid(), "loadPCM: a missing file is reported invalid");

    // An unrecognized container must be rejected outright rather than
    // misparsed: this PNG is neither RIFF nor OggS.
    const AudioClipData wrongFormat = AudioClipLoader::loadPCM(assetPath("valid_texture.png"));
    AURA_CHECK(!wrongFormat.valid(), "loadPCM: a non-audio file is rejected rather than misparsed");
}

void test_make_silence_defaults()
{
    const AudioClipData clip = AudioClipLoader::makeSilence();

    AURA_CHECK(clip.valid(), "makeSilence: default output is a valid AudioClipData");
    AURA_CHECK(clip.sampleRate == 48000, "makeSilence: defaults to 48000 Hz");
    AURA_CHECK(clip.channelCount == 2, "makeSilence: defaults to stereo");
    AURA_CHECK(approx(clip.durationSeconds(), 0.25f, 1e-3f), "makeSilence: defaults to 0.25s");

    bool allSilent = true;
    for (const f32 sample : clip.samples) {
        if (sample != 0.0f) { allSilent = false; break; }
    }
    AURA_CHECK(allSilent, "makeSilence: every sample is exactly zero");
}

void test_make_silence_clamping()
{
    // Degenerate inputs are documented to clamp rather than produce an empty
    // (and therefore invalid) clip that callers would have to special-case.
    const AudioClipData clip = AudioClipLoader::makeSilence(0.0f, 0, 0);

    AURA_CHECK(clip.valid(), "makeSilence: zero duration/rate/channels still produces a valid clip");
    AURA_CHECK(clip.sampleRate >= 1, "makeSilence: sample rate is clamped to at least 1");
    AURA_CHECK(clip.channelCount >= 1, "makeSilence: channel count is clamped to at least 1");
    AURA_CHECK(clip.frameCount() >= 1, "makeSilence: length is clamped to at least one frame");

    const AudioClipData negative = AudioClipLoader::makeSilence(-5.0f);
    AURA_CHECK(negative.valid(), "makeSilence: a negative duration is clamped rather than overflowing");
}

void test_make_sine_tone()
{
    const AudioClipData clip = AudioClipLoader::makeSineTone(440.0f, 0.5f, 0.5f, 48000, 2);

    AURA_CHECK(clip.valid(), "makeSineTone: produces a valid AudioClipData");
    AURA_CHECK(clip.sampleRate == 48000, "makeSineTone: honors the requested sample rate");
    AURA_CHECK(clip.channelCount == 2, "makeSineTone: honors the requested channel count");
    AURA_CHECK(approx(clip.durationSeconds(), 0.5f, 1e-3f), "makeSineTone: honors the requested duration");

    // A sine starts at zero and every channel carries the same signal.
    AURA_CHECK(approx(clip.samples[0], 0.0f), "makeSineTone: the waveform starts at zero");
    AURA_CHECK(approx(clip.samples[0], clip.samples[1]),
              "makeSineTone: both channels carry the identical signal");

    f32 peak = 0.0f;
    for (const f32 sample : clip.samples)
        peak = std::max(peak, std::fabs(sample));

    AURA_CHECK(approx(peak, 0.5f, 0.01f), "makeSineTone: peak amplitude matches the request");

    // Amplitude above 1.0 would clip in the mixer, so it is clamped at source.
    const AudioClipData loud = AudioClipLoader::makeSineTone(440.0f, 0.05f, 9.0f);
    f32 loudPeak = 0.0f;
    for (const f32 sample : loud.samples)
        loudPeak = std::max(loudPeak, std::fabs(sample));
    AURA_CHECK(loudPeak <= 1.0f, "makeSineTone: amplitude is clamped to at most 1.0");

    // Above Nyquist a tone aliases to something else entirely; the frequency is
    // clamped rather than silently producing a different pitch.
    const AudioClipData aliased = AudioClipLoader::makeSineTone(96000.0f, 0.05f, 0.5f, 48000, 1);
    AURA_CHECK(aliased.valid(), "makeSineTone: a frequency above Nyquist is clamped, not rejected");
}

} // namespace

int main()
{
    test_load_valid_wav();
    test_load_8bit_mono_wav();
    test_load_float32_wav();
    test_load_ogg_vorbis();
    test_load_corrupt_and_missing();
    test_make_silence_defaults();
    test_make_silence_clamping();
    test_make_sine_tone();
    AURA_TEST_MAIN_RETURN();
}
