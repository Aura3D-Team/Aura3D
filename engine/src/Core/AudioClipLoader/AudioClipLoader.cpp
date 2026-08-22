#include "aura/Core/AudioClipLoader/AudioClipLoader.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numbers>
#include <span>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace aura3d {
namespace {
constexpr u16 kWaveFormatPcm        = 0x0001;
constexpr u16 kWaveFormatIeeeFloat  = 0x0003;

constexpr u16 kWaveFormatExtensible = 0xFFFE;

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

[[nodiscard]] f32 decodeSample(std::span<const u8> data, usize byteOffset, u16 bitsPerSample, u16 formatTag) noexcept
{
    if (formatTag == kWaveFormatIeeeFloat)
    {
        if (bitsPerSample == 32)
            return std::bit_cast<f32>(readU32LE(data, byteOffset));

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

        if (body + chunkSize > bytes.size())
            break;

        if (hasTag(bytes, cursor, "fmt "))
        {
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

        cursor = body + chunkSize + (chunkSize & 1u);
    }

    if (!haveFormat || dataSize == 0)
    {
        INK_WARN << "[Aura3D] '" << path << "' is missing a 'fmt ' or 'data' chunk";
        return {};
    }

    if (formatTag != kWaveFormatPcm && formatTag != kWaveFormatIeeeFloat)
    {
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

    const f32 nyquist = static_cast<f32>(clip.sampleRate) * 0.5f;
    const f32 frequency = std::clamp(frequencyHz, 1.0f, std::nextafter(nyquist, 0.0f));
    const f32 peak = std::clamp(amplitude, 0.0f, 1.0f);

    clip.samples.resize(frames * clip.channelCount);

    const f32 angularStep = 2.0f * std::numbers::pi_v<f32> * frequency / static_cast<f32>(clip.sampleRate);

    for (usize frame = 0; frame < frames; ++frame)
    {
        const f32 value = peak * std::sin(angularStep * static_cast<f32>(frame));

        for (u16 channel = 0; channel < clip.channelCount; ++channel)
            clip.samples[frame * clip.channelCount + channel] = value;
    }

    return clip;
}

} // namespace aura3d
