#include "aura/Core/AudioEngine/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace aura3d {
namespace {
constexpr u32 kVoiceSlotBits = 16;
constexpr u32 kVoiceSlotMask = (1u << kVoiceSlotBits) - 1u;

std::vector<f32> resample(const std::vector<f32>& input, u16 channels, u32 srcRate, u32 dstRate)
{
    if (srcRate == dstRate || channels == 0 || input.empty())
        return input;

    const usize srcFrames = input.size() / channels;
    if (srcFrames == 0)
        return input;

    const f64 ratio = static_cast<f64>(dstRate) / static_cast<f64>(srcRate);
    const usize dstFrames = std::max<usize>(1u, static_cast<usize>(static_cast<f64>(srcFrames) * ratio));

    std::vector<f32> output(dstFrames * channels);

    for (usize frame = 0; frame < dstFrames; ++frame)
    {
        const f64 srcPos = static_cast<f64>(frame) / ratio;
        const usize base = static_cast<usize>(srcPos);
        const f32 frac = static_cast<f32>(srcPos - static_cast<f64>(base));

        const usize next = std::min(base + 1, srcFrames - 1);

        for (u16 channel = 0; channel < channels; ++channel)
        {
            const f32 a = input[base * channels + channel];
            const f32 b = input[next * channels + channel];
            output[frame * channels + channel] = a + (b - a) * frac;
        }
    }

    return output;
}

} // namespace

AudioEngine::AudioEngine(std::unique_ptr<wma::IAudioDevice> device, u32 maxVoices)
    : _device(std::move(device))
{
    if (!_device)
    {
        INK_WARN << "[Aura3D] AudioEngine constructed without a device; falling back to a null device";
        _device = wma::createAudioDevice(wma::AudioBackend::Null);
        (void)_device->open(wma::AudioDeviceConfig{});
    }

    const wma::AudioDeviceConfig& config = _device->getConfig();
    _sampleRate   = config.sampleRate;
    _channelCount = config.channelCount;

    _voices.resize(std::clamp<usize>(maxVoices, 1u, kVoiceSlotMask));

    _device->setMixCallback([this](std::span<f32> output) { mix(output); });

    if (_device->start() != wma::WmaCode::Ok)
    {
        INK_WARN << "[Aura3D] audio device failed to start; continuing without sound";
    }
    else
    {
        INK_INFO << "[Aura3D] audio ready: " << wma::audioBackendName(_device->getBackendType())
                 << ", " << _sampleRate << " Hz, " << _channelCount << " ch, "
                 << _voices.size() << " voices";
    }
}

AudioEngine::~AudioEngine()
{
    if (_device)
    {
        _device->stop();
        _device->close();
    }
}

AudioClipHandle AudioEngine::loadClip(const std::string& path, AudioClipMode mode)
{
    AudioClipData data = AudioClipLoader::loadPCM(path);

    if (!data.valid())
    {
        INK_WARN << "[Aura3D] could not load audio clip '" << path << "'; substituting silence";
        data = AudioClipLoader::makeSilence(0.1f, _sampleRate, _channelCount);
    }

    return createClip(data, mode);
}

AudioClipHandle AudioEngine::createClip(const AudioClipData& data, AudioClipMode mode)
{
    if (!data.valid())
        return {};

    Clip clip;
    clip.channelCount = data.channelCount;
    clip.mode         = mode;
    clip.samples      = resample(data.samples, data.channelCount, data.sampleRate, _sampleRate);

    const std::scoped_lock lock(_clipMutex);

    const AudioClipHandle handle{_nextClipHandle++};
    _clips.emplace(handle, std::move(clip));
    return handle;
}

void AudioEngine::unloadClip(AudioClipHandle clip) noexcept
{
    if (!isValidHandle(clip))
        return;

    {
        const std::scoped_lock lock(_voiceMutex);
        for (Voice& voice : _voices)
        {
            if (voice.active && voice.clip == clip)
            {
                voice.active = false;
                voice.clip   = {};
            }
        }
    }

    const std::scoped_lock lock(_clipMutex);
    _clips.erase(clip);
}

void AudioEngine::unloadAllClips() noexcept
{
    stopAll();

    const std::scoped_lock lock(_clipMutex);
    _clips.clear();
}

AudioSourceHandle AudioEngine::play(const AudioSourceDesc& desc)
{
    if (!isValidHandle(desc.clip))
        return {};

    {
        const std::scoped_lock clipLock(_clipMutex);
        if (!_clips.contains(desc.clip))
        {
            INK_WARN << "[Aura3D] play() called with an unknown clip handle";
            return {};
        }
    }

    const std::scoped_lock lock(_voiceMutex);

    const auto slot = std::find_if(_voices.begin(), _voices.end(),
                                   [](const Voice& v) { return !v.active; });

    if (slot == _voices.end())
    {
        return {};
    }

    const usize index = static_cast<usize>(std::distance(_voices.begin(), slot));

    slot->generation = static_cast<u16>(slot->generation + 1u);
    if (slot->generation == 0)
        slot->generation = 1;

    slot->clip        = desc.clip;
    slot->cursor      = 0;
    slot->gain        = std::max(0.0f, desc.gain);
    slot->position    = desc.position;
    slot->spatial     = desc.spatial;
    slot->loop        = desc.loop;
    slot->minDistance = std::max(0.0f, desc.minDistance);
    slot->maxDistance = std::max(slot->minDistance + 0.001f, desc.maxDistance);
    slot->paused      = false;
    slot->active      = true;

    return makeSourceHandle(index, slot->generation);
}

AudioSourceHandle AudioEngine::play(AudioClipHandle clip, f32 gain)
{
    AudioSourceDesc desc;
    desc.clip = clip;
    desc.gain = gain;
    return play(desc);
}

void AudioEngine::stop(AudioSourceHandle source) noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    usize slot = 0;
    if (!resolveVoice(source, slot))
        return;

    _voices[slot].active = false;
    _voices[slot].clip   = {};
}

void AudioEngine::stopAll() noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    for (Voice& voice : _voices)
    {
        voice.active = false;
        voice.clip   = {};
    }
}

void AudioEngine::pause(AudioSourceHandle source) noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    usize slot = 0;
    if (resolveVoice(source, slot))
        _voices[slot].paused = true;
}

void AudioEngine::resume(AudioSourceHandle source) noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    usize slot = 0;
    if (resolveVoice(source, slot))
        _voices[slot].paused = false;
}

bool AudioEngine::isPlaying(AudioSourceHandle source) const noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    usize slot = 0;
    return resolveVoice(source, slot) && !_voices[slot].paused;
}

bool AudioEngine::isPaused(AudioSourceHandle source) const noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    usize slot = 0;
    return resolveVoice(source, slot) && _voices[slot].paused;
}

u32 AudioEngine::activeVoiceCount() const noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    return static_cast<u32>(std::count_if(_voices.begin(), _voices.end(),
                                          [](const Voice& v) { return v.active; }));
}

void AudioEngine::setSourceGain(AudioSourceHandle source, f32 gain) noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    usize slot = 0;
    if (resolveVoice(source, slot))
        _voices[slot].gain = std::max(0.0f, gain);
}

void AudioEngine::setSourcePosition(AudioSourceHandle source, const glm::vec3& position) noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    usize slot = 0;
    if (resolveVoice(source, slot))
        _voices[slot].position = position;
}

void AudioEngine::setSourceLooping(AudioSourceHandle source, bool loop) noexcept
{
    const std::scoped_lock lock(_voiceMutex);

    usize slot = 0;
    if (resolveVoice(source, slot))
        _voices[slot].loop = loop;
}

void AudioEngine::setListener(const AudioListener3D& listener) noexcept
{
    const std::scoped_lock lock(_voiceMutex);
    _listener = listener;
}

AudioListener3D AudioEngine::listener() const noexcept
{
    const std::scoped_lock lock(_voiceMutex);
    return _listener;
}

void AudioEngine::setMasterVolume(f32 volume) noexcept
{
    _masterVolume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

f32 AudioEngine::masterVolume() const noexcept
{
    return _masterVolume.load(std::memory_order_relaxed);
}

void AudioEngine::update(f32 )
{
    const std::scoped_lock lock(_voiceMutex);

    for (Voice& voice : _voices)
    {
        if (!voice.active && isValidHandle(voice.clip))
            voice.clip = {};
    }
}

u32 AudioEngine::sampleRate() const noexcept
{
    return _sampleRate;
}

u16 AudioEngine::channelCount() const noexcept
{
    return _channelCount;
}

wma::AudioBackend AudioEngine::backend() const noexcept
{
    return _device ? _device->getBackendType() : wma::AudioBackend::Null;
}

bool AudioEngine::isDeviceRunning() const noexcept
{
    return _device && _device->isRunning();
}

void AudioEngine::resumeDevice()
{
    if (_device && !_device->isRunning())
        (void)_device->start();
}

AudioSourceHandle AudioEngine::makeSourceHandle(usize slot, u16 generation) noexcept
{
    return AudioSourceHandle{(static_cast<u32>(generation) << kVoiceSlotBits) |
                             (static_cast<u32>(slot) & kVoiceSlotMask)};
}

bool AudioEngine::resolveVoice(AudioSourceHandle handle, usize& slotOut) const noexcept
{
    if (!isValidHandle(handle))
        return false;

    const usize slot = static_cast<usize>(handle.value() & kVoiceSlotMask);
    const u16 generation = static_cast<u16>(handle.value() >> kVoiceSlotBits);

    if (slot >= _voices.size())
        return false;

    const Voice& voice = _voices[slot];

    if (!voice.active || voice.generation != generation)
        return false;

    slotOut = slot;
    return true;
}

void AudioEngine::computeSpatialGains(const Voice& voice, f32& leftGain, f32& rightGain) const noexcept
{
    const glm::vec3 toSource = voice.position - _listener.position;
    const f32 distance = glm::length(toSource);

    f32 attenuation = 1.0f;
    if (distance > voice.minDistance)
    {
        const f32 span = voice.maxDistance - voice.minDistance;
        attenuation = 1.0f - std::clamp((distance - voice.minDistance) / span, 0.0f, 1.0f);
    }

    f32 pan = 0.0f;

    constexpr f32 kMinPanDistance = 0.0001f;
    if (distance > kMinPanDistance)
    {
        const glm::vec3 forward = glm::normalize(_listener.forward);
        const glm::vec3 up      = glm::normalize(_listener.up);

        const glm::vec3 right = glm::normalize(glm::cross(forward, up));

        pan = std::clamp(glm::dot(toSource / distance, right), -1.0f, 1.0f);
    }

    const f32 angle = (pan + 1.0f) * 0.25f * std::numbers::pi_v<f32>;
    leftGain  = std::cos(angle) * attenuation;
    rightGain = std::sin(angle) * attenuation;
}

void AudioEngine::mix(std::span<f32> output)
{
    std::fill(output.begin(), output.end(), 0.0f);

    const f32 master = _masterVolume.load(std::memory_order_relaxed);

    const std::scoped_lock lock(_voiceMutex, _clipMutex);

    const u16 outChannels = _channelCount;
    const usize outFrames = outChannels == 0 ? 0 : output.size() / outChannels;
    if (outFrames == 0)
        return;

    for (Voice& voice : _voices)
    {
        if (!voice.active || voice.paused || !isValidHandle(voice.clip))
            continue;

        const auto entry = _clips.find(voice.clip);
        if (entry == _clips.end())
        {
            voice.active = false;
            continue;
        }

        const Clip& clip = entry->second;
        const u16 clipChannels = clip.channelCount;
        if (clipChannels == 0 || clip.samples.empty())
        {
            voice.active = false;
            continue;
        }

        const usize clipFrames = clip.samples.size() / clipChannels;

        f32 leftGain  = 1.0f;
        f32 rightGain = 1.0f;
        if (voice.spatial)
            computeSpatialGains(voice, leftGain, rightGain);

        const f32 voiceGain = voice.gain * master;
        leftGain  *= voiceGain;
        rightGain *= voiceGain;

        if (leftGain <= 0.0f && rightGain <= 0.0f)
        {
            voice.cursor += outFrames;
            if (voice.cursor >= clipFrames)
            {
                if (voice.loop)
                    voice.cursor %= clipFrames;
                else
                    voice.active = false;
            }
            continue;
        }

        for (usize frame = 0; frame < outFrames; ++frame)
        {
            if (voice.cursor >= clipFrames)
            {
                if (!voice.loop)
                {
                    voice.active = false;
                    break;
                }
                voice.cursor = 0;
            }

            const usize clipBase = voice.cursor * clipChannels;

            if (clipChannels == 1)
            {
                const f32 sample = clip.samples[clipBase];

                if (outChannels >= 2)
                {
                    output[frame * outChannels + 0] += sample * leftGain;
                    output[frame * outChannels + 1] += sample * rightGain;

                    for (u16 channel = 2; channel < outChannels; ++channel)
                        output[frame * outChannels + channel] += sample * voiceGain;
                }
                else
                {
                    output[frame * outChannels] += sample * voiceGain;
                }
            }
            else
            {
                for (u16 channel = 0; channel < outChannels; ++channel)
                {
                    const u16 sourceChannel = std::min<u16>(channel, static_cast<u16>(clipChannels - 1));
                    const f32 sample = clip.samples[clipBase + sourceChannel];

                    const f32 channelGain = (channel == 0) ? leftGain
                                          : (channel == 1) ? rightGain
                                                           : voiceGain;

                    output[frame * outChannels + channel] += sample * channelGain;
                }
            }

            ++voice.cursor;
        }
    }

    for (f32& sample : output)
        sample = std::clamp(sample, -1.0f, 1.0f);
}

} // namespace aura3d
