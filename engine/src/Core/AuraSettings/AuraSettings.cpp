#include "aura/Core/AuraSettings/AuraSettings.h"

#include <wma/wma.hpp>

#include "aura/aura.h"

namespace aura3d {
AuraSettings* AuraSettings::get()
{
    static AuraSettings instance;
    return &instance;
}

ink::EnhancedJson* AuraSettings::getSettings()
{
    return &_settings;
}

void AuraSettings::setDefaults(const AuraConfig& defaults)
{
    _defaults = defaults;
}

void AuraSettings::reload(const std::string& path)
{
    _settings = ink::EnhancedJson::loadFromFile(path);

    //! loadFromFile() reports a missing or malformed file as a null document
    //! rather than an error, which now matters: an application may legitimately
    //! ship none, so the two cases have to read differently in the log.
    if (_settings.is_null())
    {
        _settings = ink::EnhancedJson::object();
        INK_WARN << "AuraSettings: no readable configuration at '" << path
                 << "'; running on the compiled-in defaults";
        return;
    }

    INK_INFO << "AuraSettings: loaded configuration from " << path;
}

int AuraSettings::getWindowWidth() const
{
    return _settings.getPath<int>("/window/width", _defaults.window.width);
}

int AuraSettings::getWindowHeight() const
{
    return _settings.getPath<int>("/window/height", _defaults.window.height);
}

std::string AuraSettings::getWindowTitle() const
{
    const std::string& fallback = _defaults.window.title;

    return _settings.getPath<std::string>(
        "/window/title", fallback.empty() ? std::string{APPLICATION_NAME} : fallback);
}

wma::WindowBackend AuraSettings::getWindowBackend() const
{
    const std::string backendStr =
        _settings.getPath<std::string>("/window/backend", std::string());

    if (backendStr.empty())
        return _defaults.window.backend;

    wma::WindowBackend backend;
    if (WindowBackendFromString(backendStr, backend))
        return backend;

    INK_WARN << "AuraSettings: unsupported window backend '" << backendStr
             << "'; falling back to " << WindowBackendToString(_defaults.window.backend);
    return _defaults.window.backend;
}

bool AuraSettings::getWindowResizable() const
{
    return _settings.getPath<bool>("/window/resizable", _defaults.window.resizable);
}

bool AuraSettings::getFullscreen() const
{
    return _settings.getPath<bool>("/window/fullscreen", _defaults.window.fullscreen);
}

bool AuraSettings::getVSync() const
{
    return _settings.getPath<bool>("/window/vsync", _defaults.window.vsync);
}

VSyncMode AuraSettings::getVSyncMode() const
{
    const std::string modeStr = _settings.getPath<std::string>("/window/vsync_mode", std::string());

    VSyncMode mode;
    if (!modeStr.empty() && VSyncModeFromString(modeStr, mode)) {
        return mode;
    }

    if (_defaults.window.vsyncMode)
        return *_defaults.window.vsyncMode;

    return getVSync() ? VSyncMode::Fifo : VSyncMode::AutoNoVsync;
}

int AuraSettings::getFPSLimit() const
{
    return _settings.getPath<int>("/window/fps_limit", _defaults.window.fpsLimit);
}

std::string AuraSettings::getRendererBackend() const
{
    return _settings.getPath<std::string>("/renderer/backend", _defaults.renderer.backend);
}

bool AuraSettings::getValidationLayers() const
{
#ifdef NDEBUG
    constexpr bool kBuildDefault = false;
#else
    constexpr bool kBuildDefault = true;
#endif
    return _settings.getPath<bool>(
        "/renderer/validation_layers",
        _defaults.renderer.validationLayers.value_or(kBuildDefault));
}

int AuraSettings::getMaxFramesInFlight() const
{
    return _settings.getPath<int>("/renderer/max_frames_in_flight",
                                  _defaults.renderer.maxFramesInFlight);
}

std::string AuraSettings::getGpuPreference() const
{
    return _settings.getPath<std::string>("/graphics/gpu_preference",
                                          _defaults.graphics.gpuPreference);
}

int AuraSettings::getMsaaSamples() const
{
    return _settings.getPath<int>("/graphics/msaa_samples", _defaults.graphics.msaaSamples);
}

int AuraSettings::getCpuThreads() const
{
    return _settings.getPath<int>("/graphics/cpu_threads", _defaults.graphics.cpuThreads);
}

wma::AudioBackend AuraSettings::getAudioBackend() const
{
    const std::string backendStr =
        _settings.getPath<std::string>("/audio/backend", std::string());

    //! An absent key defers to the compiled-in choice; an explicit "auto" asks
    //! for the platform's, which is what it has always meant.
    if (backendStr.empty())
    {
        return _defaults.audio.backend ? *_defaults.audio.backend
                                       : wma::getDefaultAudioBackend();
    }

    if (backendStr == "auto")
        return wma::getDefaultAudioBackend();

    wma::AudioBackend backend;
    if (AudioBackendFromString(backendStr, backend))
    {
        if (!wma::isAudioBackendAvailable(backend))
        {
            INK_WARN << "AuraSettings: audio backend '" << backendStr
                     << "' is not compiled into this build of wma; using the default";
            return wma::getDefaultAudioBackend();
        }
        return backend;
    }

    INK_WARN << "AuraSettings: unsupported audio backend '" << backendStr
             << "'; falling back to the platform default";
    return wma::getDefaultAudioBackend();
}

f32 AuraSettings::getMasterVolume() const
{
    return _settings.getPath<f32>("/audio/master_volume", _defaults.audio.masterVolume);
}

int AuraSettings::getAudioSampleRate() const
{
    return _settings.getPath<int>("/audio/sample_rate", _defaults.audio.sampleRate);
}

int AuraSettings::getAudioChannels() const
{
    return _settings.getPath<int>("/audio/channels", _defaults.audio.channels);
}

int AuraSettings::getAudioBufferFrames() const
{
    return _settings.getPath<int>("/audio/buffer_frames", _defaults.audio.bufferFrames);
}

int AuraSettings::getAudioMaxVoices() const
{
    return _settings.getPath<int>("/audio/max_voices", _defaults.audio.maxVoices);
}

std::string AuraSettings::getShadersPath() const
{
    return _settings.getPath<std::string>("/paths/shaders", _defaults.paths.shaders);
}

std::string AuraSettings::getTexturesPath() const
{
    return _settings.getPath<std::string>("/paths/textures", _defaults.paths.textures);
}

std::string AuraSettings::getModelsPath() const
{
    return _settings.getPath<std::string>("/paths/models", _defaults.paths.models);
}

std::string AuraSettings::getAudioPath() const
{
    return _settings.getPath<std::string>("/paths/audio", _defaults.paths.audio);
}

std::string AuraSettings::getLogsPath() const
{
    return _settings.getPath<std::string>("/paths/logs", _defaults.paths.logs);
}

ink::LogLevel AuraSettings::getLogLevel() const
{
#ifdef NDEBUG
    constexpr ink::LogLevel kBuildDefault = ink::LogLevel::INFO;
#else
    constexpr ink::LogLevel kBuildDefault = ink::LogLevel::TRACE;
#endif
    const ink::LogLevel fallback = _defaults.logging.level.value_or(kBuildDefault);

    const std::string levelStr = _settings.getPath<std::string>("/logging/level", std::string());
    if (levelStr.empty())
        return fallback;

    std::string up;
    up.reserve(levelStr.size());
    for (const char c : levelStr)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    for (u32 i = 0; i < std::to_underlying(ink::LogLevel::COUNT); ++i)
    {
        if (up == ink::MAP_COLORS_FOR_LEVEL[i].desc)
            return static_cast<ink::LogLevel>(i);
    }
    return fallback;
}

bool AuraSettings::getLogToFile() const
{
    return _settings.getPath<bool>("/logging/write_to_file", _defaults.logging.toFile);
}

} // namespace aura3d
