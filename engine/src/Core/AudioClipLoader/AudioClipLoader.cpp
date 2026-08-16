#include "aura/Core/AudioClipLoader/AudioClipLoader.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numbers>
#include <span>

//! Declarations only: stb_vorbis.c is compiled as its own translation unit
//! (see the root CMakeLists.txt), the way vendor/glad/glad.c is. Without this
//! guard the include would pull the whole implementation in here as well and
//! the two copies would collide at link time. STB_VORBIS_NO_STDIO /
//! STB_VORBIS_NO_PUSHDATA_API are set on the target rather than here, so this
//! TU and the compiled one cannot disagree about the API they see.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace aura3d {

namespace {

//! WAV sample encodings, as stored in the `fmt ` chunk's wFormatTag.
constexpr u16 kWaveFormatPcm        = 0x0001;
constexpr u16 kWaveFormatIeeeFloat  = 0x0003;
//! WAVE_FORMAT_EXTENSIBLE: the real encoding moves into the chunk's GUID, whose
//! first two bytes are one of the tags above.
constexpr u16 kWaveFormatExtensible = 0xFFFE;

//! Reads a whole file into memory. Both decoders want a contiguous buffer --
//! stb_vorbis is compiled without stdio, and the WAV parser walks chunks -- and
//! audio files are small enough relative to the meshes and textures already
//! loaded this way that streaming the read would not buy anything.
std::vector<u8> readEntireFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};

    const std::streamoff size = file.tellg();
    if (size <= 0)
        return {};

    std::vector<u8> bytes(static_cast<usize>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        return {};

    return bytes;
}

//! Little-endian fixed-width reads. RIFF is little-endian regardless of host,
//! so these are byte assembles rather than a memcpy of the host representation.
[[nodiscard]] u16 readU16LE(std::span<const u8> data, usize offset) noexcept
{
    return static_cast<u16>(data[offset]) |
           static_cast<u16>(static_cast<u16>(data[offset + 1]) << 8);
}

[[nodiscard]] u32 readU32LE(std::span<const u8> data, usize offset) noexcept
{
    return static_cast<u32>(data[offset]) |
           (static_cast<u32>(data[offset + 1]) << 8) |
           (static_cast<u32>(data[offset + 2]) << 16) |
           (static_cast<u32>(data[offset + 3]) << 24);
}

[[nodiscard]] bool hasTag(std::span<const u8> data, usize offset, const char (&tag)[5]) noexcept
{
    return offset + 4 <= data.size() && std::memcmp(data.data() + offset, tag, 4) == 0;
}

/**
 * @brief Converts one interleaved sample to float.
 *
 * Integer PCM is normalized by the magnitude of its most negative value, which
 * is the convention that maps the full stored range into [-1, 1] without
 * clipping the negative extreme. 8-bit is the odd one out: WAV stores it
 * unsigned with 128 as silence, every other width is signed.
 */
[[nodiscard]] f32 decodeSample(std::span<const u8> data, usize byteOffset, u16 bitsPerSample, u16 formatTag) noexcept
{
    if (formatTag == kWaveFormatIeeeFloat)
    {
        if (bitsPerSample == 32)
            return std::bit_cast<f32>(readU32LE(data, byteOffset));

        //! 64-bit doubles: assemble both halves, then narrow.
        const u64 lo = readU32LE(data, byteOffset);
        const u64 hi = readU32LE(data, byteOffset + 4);
        return static_cast<f32>(std::bit_cast<f64>(lo | (hi << 32)));
    }

    switch (bitsPerSample)
    {
    case 8:
        return (static_cast<f32>(data[byteOffset]) - 128.0f) / 128.0f;

    case 16:
        return static_cast<f32>(static_cast<i16>(readU16LE(data, byteOffset))) / 32768.0f;

    case 24:
    {
        //! Sign-extend the 24-bit value into 32 bits by placing it in the high
        //! three bytes and arithmetic-shifting back down.
        const u32 raw = (static_cast<u32>(data[byteOffset]) << 8) |
                        (static_cast<u32>(data[byteOffset + 1]) << 16) |
                        (static_cast<u32>(data[byteOffset + 2]) << 24);
        return static_cast<f32>(static_cast<i32>(raw) >> 8) / 8388608.0f;
    }

    case 32:
        return static_cast<f32>(static_cast<i32>(readU32LE(data, byteOffset))) / 2147483648.0f;

    default:
        return 0.0f;
    }
}

/**
 * @brief Decodes a RIFF/WAVE buffer.
 *
 * Walks the chunk list rather than assuming `fmt ` and `data` sit at fixed
 * offsets: real files routinely carry LIST/INFO metadata between them, and
 * anything written by a DAW usually does.
 */
AudioClipData decodeWav(std::span<const u8> bytes, const std::string& path)
{
    constexpr usize kRiffHeaderSize = 12;
    constexpr usize kChunkHeaderSize = 8;

    if (bytes.size() < kRiffHeaderSize || !hasTag(bytes, 0, "RIFF") || !hasTag(bytes, 8, "WAVE"))
    {
        INK_WARN << "[Aura3D] '" << path << "' is not a RIFF/WAVE file";
        return {};
    }

    u16 formatTag    = 0;
    u16 channelCount = 0;
    u32 sampleRate   = 0;
    u16 bitsPerSample = 0;
    bool haveFormat  = false;

    usize dataOffset = 0;
    usize dataSize   = 0;

    usize cursor = kRiffHeaderSize;
    while (cursor + kChunkHeaderSize <= bytes.size())
    {
        const u32 chunkSize = readU32LE(bytes, cursor + 4);
        const usize body    = cursor + kChunkHeaderSize;

        //! A chunk claiming to extend past the end of the file is corruption,
        //! not something to read through.
        if (body + chunkSize > bytes.size())
            break;

        if (hasTag(bytes, cursor, "fmt "))
        {
            //! 16 bytes covers the common fields; extensible adds a GUID whose
            //! leading two bytes carry the encoding the header claims is 0xFFFE.
            if (chunkSize < 16)
                break;

            formatTag     = readU16LE(bytes, body + 0);
            channelCount  = readU16LE(bytes, body + 2);
            sampleRate    = readU32LE(bytes, body + 4);
            bitsPerSample = readU16LE(bytes, body + 14);

            if (formatTag == kWaveFormatExtensible && chunkSize >= 26)
                formatTag = readU16LE(bytes, body + 24);

            haveFormat = true;
        }
        else if (hasTag(bytes, cursor, "data"))
        {
            dataOffset = body;
            dataSize   = chunkSize;
        }

        //! Chunks are word-aligned: an odd size is followed by a pad byte that
        //! is not counted in the size field.
        cursor = body + chunkSize + (chunkSize & 1u);
    }

    if (!haveFormat || dataSize == 0)
    {
        INK_WARN << "[Aura3D] '" << path << "' is missing a 'fmt ' or 'data' chunk";
        return {};
    }

    if (formatTag != kWaveFormatPcm && formatTag != kWaveFormatIeeeFloat)
    {
        //! Compressed WAV variants (ADPCM, mu-law, ...) are out of scope: use
        //! OGG for anything that needs to be smaller than plain PCM.
        INK_WARN << "[Aura3D] '" << path << "' uses unsupported WAV encoding 0x"
                 << std::hex << formatTag << std::dec << " (only PCM and IEEE float are supported)";
        return {};
    }

    const bool bitsSupported = (formatTag == kWaveFormatIeeeFloat)
                                   ? (bitsPerSample == 32 || bitsPerSample == 64)
                                   : (bitsPerSample == 8 || bitsPerSample == 16 ||
                                      bitsPerSample == 24 || bitsPerSample == 32);

    if (channelCount == 0 || sampleRate == 0 || !bitsSupported)
    {
        INK_WARN << "[Aura3D] '" << path << "' has an unsupported WAV format: "
                 << channelCount << " ch, " << sampleRate << " Hz, " << bitsPerSample << " bits";
        return {};
    }

    const usize bytesPerSample = bitsPerSample / 8u;
    const usize sampleCount    = dataSize / bytesPerSample;
    //! Truncate a trailing partial frame rather than emitting one with missing
    //! channels, which would swap the stereo pairing for everything after it.
    const usize frameCount     = sampleCount / channelCount;

    if (frameCount == 0)
    {
        INK_WARN << "[Aura3D] '" << path << "' contains no complete audio frames";
        return {};
    }

    AudioClipData clip;
    clip.sampleRate   = sampleRate;
    clip.channelCount = channelCount;
    clip.samples.resize(frameCount * channelCount);

    for (usize i = 0; i < clip.samples.size(); ++i)
        clip.samples[i] = decodeSample(bytes, dataOffset + i * bytesPerSample, bitsPerSample, formatTag);

    return clip;
}

/**
 * @brief Decodes an Ogg Vorbis buffer through the vendored stb_vorbis.
 *
 * stb_vorbis hands back interleaved 16-bit samples in a malloc'd block; this
 * converts to float and frees it. The extra pass is the price of not
 * reimplementing Vorbis, and it happens once at load rather than per frame.
 */
AudioClipData decodeOggVorbis(std::span<const u8> bytes, const std::string& path)
{
    int channels = 0;
    int sampleRate = 0;
    short* decoded = nullptr;

    const int frameCount = stb_vorbis_decode_memory(bytes.data(),
                                                    static_cast<int>(bytes.size()),
                                                    &channels,
                                                    &sampleRate,
                                                    &decoded);

    if (frameCount <= 0 || !decoded || channels <= 0 || sampleRate <= 0)
    {
        INK_WARN << "[Aura3D] '" << path << "' could not be decoded as Ogg Vorbis";
        if (decoded)
            std::free(decoded);
        return {};
    }

    AudioClipData clip;
    clip.sampleRate   = static_cast<u32>(sampleRate);
    clip.channelCount = static_cast<u16>(channels);

    const usize sampleCount = static_cast<usize>(frameCount) * static_cast<usize>(channels);
    clip.samples.resize(sampleCount);

    for (usize i = 0; i < sampleCount; ++i)
        clip.samples[i] = static_cast<f32>(decoded[i]) / 32768.0f;

    //! stb_vorbis allocates with malloc, so it is freed with free -- not
    //! delete, and not wrapped in a unique_ptr with the wrong deleter.
    std::free(decoded);

    return clip;
}

} // namespace

AudioClipData AudioClipLoader::loadPCM(const std::string& path)
{
    const std::vector<u8> bytes = readEntireFile(path);

    if (bytes.empty())
    {
        INK_WARN << "[Aura3D] audio file '" << path << "' is missing or empty";
        return {};
    }

    const std::span<const u8> view{bytes};

    //! Sniffed from the magic bytes rather than the extension: a mislabelled
    //! file is common enough, and both container headers are unambiguous.
    if (hasTag(view, 0, "RIFF"))
        return decodeWav(view, path);

    if (hasTag(view, 0, "OggS"))
        return decodeOggVorbis(view, path);

    INK_WARN << "[Aura3D] audio file '" << path
             << "' is in an unrecognized format (expected RIFF/WAVE or Ogg Vorbis)";
    return {};
}

AudioClipData AudioClipLoader::makeSilence(f32 durationSeconds, u32 sampleRate, u16 channelCount)
{
    AudioClipData clip;
    clip.sampleRate   = std::max(1u, sampleRate);
    clip.channelCount = std::max<u16>(1u, channelCount);

    const f32 seconds = std::max(0.0f, durationSeconds);
    const usize frames = std::max<usize>(1u, static_cast<usize>(seconds * static_cast<f32>(clip.sampleRate)));

    clip.samples.assign(frames * clip.channelCount, 0.0f);
    return clip;
}

AudioClipData AudioClipLoader::makeSineTone(f32 frequencyHz,
                                            f32 durationSeconds,
                                            f32 amplitude,
                                            u32 sampleRate,
                                            u16 channelCount)
{
    AudioClipData clip;
    clip.sampleRate   = std::max(1u, sampleRate);
    clip.channelCount = std::max<u16>(1u, channelCount);

    const f32 seconds = std::max(0.0f, durationSeconds);
    const usize frames = std::max<usize>(1u, static_cast<usize>(seconds * static_cast<f32>(clip.sampleRate)));

    //! Clamped below Nyquist: a tone at or above half the sample rate aliases
    //! down to a different (and much lower) frequency than the caller asked for.
    const f32 nyquist = static_cast<f32>(clip.sampleRate) * 0.5f;
    const f32 frequency = std::clamp(frequencyHz, 1.0f, std::nextafter(nyquist, 0.0f));
    const f32 peak = std::clamp(amplitude, 0.0f, 1.0f);

    clip.samples.resize(frames * clip.channelCount);

    const f32 angularStep = 2.0f * std::numbers::pi_v<f32> * frequency / static_cast<f32>(clip.sampleRate);

    for (usize frame = 0; frame < frames; ++frame)
    {
        const f32 value = peak * std::sin(angularStep * static_cast<f32>(frame));

        //! Same value in every channel: a mono tone duplicated across a stereo
        //! pair, which is what a test signal wants (panning is the mixer's job).
        for (u16 channel = 0; channel < clip.channelCount; ++channel)
            clip.samples[frame * clip.channelCount + channel] = value;
    }

    return clip;
}

} // namespace aura3d
