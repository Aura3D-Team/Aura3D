#include "aura/Core/AuraSettings/AuraSettings.h"

//! For getDefaultAudioBackend()/isAudioBackendAvailable(), which resolve the
//! "auto" audio backend. The header itself only needs wma::AudioBackend the
//! type, which comes from wma/core/Types.hpp, so the factory declarations are
//! pulled in here rather than widening what every consumer of AuraSettings.h
//! has to parse.
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

void AuraSettings::reload(const std::string& path)
{
    _settings = ink::EnhancedJson::loadFromFile(path);
    INK_INFO << "AuraSettings: loaded configuration from " << path;
}

int AuraSettings::getWindowWidth() const
{
    return _settings.getPath<int>("/window/width", 1280);
}

int AuraSettings::getWindowHeight() const
{
    return _settings.getPath<int>("/window/height", 720);
}

std::string AuraSettings::getWindowTitle() const
{
    return _settings.getPath<std::string>("/window/title", APPLICATION_NAME);
}

wma::WindowBackend AuraSettings::getWindowBackend() const
{
    const std::string backendStr = _settings.getPath<std::string>("/window/backend", "SDL3");

    wma::WindowBackend backend;
    if (WindowBackendFromString(backendStr, backend))
        return backend;

    INK_WARN << "AuraSettings: unsupported window backend '" << backendStr
             << "'; falling back to SDL3";
    return wma::WindowBackend::SDL3;
}

bool AuraSettings::getWindowResizable() const
{
    return _settings.getPath<bool>("/window/resizable", true);
}

bool AuraSettings::getFullscreen() const
{
    return _settings.getPath<bool>("/window/fullscreen", false);
}

bool AuraSettings::getVSync() const
{
    return _settings.getPath<bool>("/window/vsync", false);
}

VSyncMode AuraSettings::getVSyncMode() const
{
    const std::string modeStr = _settings.getPath<std::string>("/window/vsync_mode", std::string());

    VSyncMode mode;
    if (!modeStr.empty() && VSyncModeFromString(modeStr, mode)) {
        return mode;
    }

    // No explicit mode: derive from the legacy boolean flag so existing
    // settings.json files keep behaving the same.
    return getVSync() ? VSyncMode::Fifo : VSyncMode::AutoNoVsync;
}

int AuraSettings::getFPSLimit() const
{
    return _settings.getPath<int>("/window/fps_limit", 60);
}

std::string AuraSettings::getRendererBackend() const
{
    return _settings.getPath<std::string>("/renderer/backend", "vulkan");
}

bool AuraSettings::getValidationLayers() const
{
#ifdef NDEBUG
    constexpr bool kDefault = false;
#else
    constexpr bool kDefault = true;
#endif
    return _settings.getPath<bool>("/renderer/validation_layers", kDefault);
}

int AuraSettings::getMaxFramesInFlight() const
{
    return _settings.getPath<int>("/renderer/max_frames_in_flight", 2);
}

std::string AuraSettings::getGpuPreference() const
{
    return _settings.getPath<std::string>("/graphics/gpu_preference", "discrete");
}

int AuraSettings::getMsaaSamples() const
{
    return _settings.getPath<int>("/graphics/msaa_samples", 1);
}

int AuraSettings::getCpuThreads() const
{
    return _settings.getPath<int>("/graphics/cpu_threads", 0);
}

wma::AudioBackend AuraSettings::getAudioBackend() const
{
    const std::string backendStr = _settings.getPath<std::string>("/audio/backend", "auto");

    //! "auto" is the documented default and means "let libwma decide", which is
    //! the only answer that is correct on every platform: ALSA exists on desktop
    //! Linux and nowhere else, so a hardcoded value in a shared config file
    //! would be wrong for some target.
    if (backendStr == "auto" || backendStr.empty())
        return wma::getDefaultAudioBackend();

    wma::AudioBackend backend;
    if (AudioBackendFromString(backendStr, backend))
    {
        //! A backend the build left out would degrade at openAudioDevice()
        //! anyway, but saying so here names the setting that was ignored.
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
    return _settings.getPath<f32>("/audio/master_volume", 1.0f);
}

int AuraSettings::getAudioSampleRate() const
{
    return _settings.getPath<int>("/audio/sample_rate", 48000);
}

int AuraSettings::getAudioChannels() const
{
    return _settings.getPath<int>("/audio/channels", 2);
}

int AuraSettings::getAudioBufferFrames() const
{
    return _settings.getPath<int>("/audio/buffer_frames", 1024);
}

int AuraSettings::getAudioMaxVoices() const
{
    return _settings.getPath<int>("/audio/max_voices", 32);
}

std::string AuraSettings::getShadersPath() const
{
    return _settings.getPath<std::string>("/paths/shaders", "./resources/shaders/");
}

std::string AuraSettings::getTexturesPath() const
{
    return _settings.getPath<std::string>("/paths/textures", "./resources/textures/");
}

std::string AuraSettings::getModelsPath() const
{
    return _settings.getPath<std::string>("/paths/models", "./resources/models/");
}

std::string AuraSettings::getAudioPath() const
{
    return _settings.getPath<std::string>("/paths/audio", "./resources/audio/");
}

std::string AuraSettings::getLogsPath() const
{
    return _settings.getPath<std::string>("/paths/logs", "./logs/");
}

ink::LogLevel AuraSettings::getLogLevel() const
{
#ifdef NDEBUG
    constexpr ink::LogLevel kDefault = ink::LogLevel::INFO;
#else
    constexpr ink::LogLevel kDefault = ink::LogLevel::TRACE;
#endif

    const std::string levelStr = _settings.getPath<std::string>("/logging/level", std::string());
    if (levelStr.empty()) 
        return kDefault;

    std::string up;
    up.reserve(levelStr.size());
    for (const char c : levelStr) 
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    // Reuses the same name table the logger itself prints with, so the two
    // never drift apart.
    for (u32 i = 0; i < std::to_underlying(ink::LogLevel::COUNT); ++i) 
    {
        if (up == ink::MAP_COLORS_FOR_LEVEL[i].desc)
            return static_cast<ink::LogLevel>(i);
    }
    return kDefault;
}

bool AuraSettings::getLogToFile() const
{
    return _settings.getPath<bool>("/logging/write_to_file", false);
}

} // namespace aura3d
