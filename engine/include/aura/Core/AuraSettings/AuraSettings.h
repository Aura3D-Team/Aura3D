#ifndef AURASETTINGS_H
#define AURASETTINGS_H

#include <cctype>
#include <optional>
#include <string>

#include <ink/EnhancedJson.h>
#include <ink/Inkogger.h>

#include <wma/core/Types.hpp>

/**
 * @brief List of supported presentation modes, mirroring WebGPU's PresentMode.
 * Uses X-Macros for synchronized enum and string conversions (see RENDERER_LIST).
 */
#define VSYNC_MODE_LIST \
    X(AutoVsync)        \
    X(AutoNoVsync)      \
    X(Fifo)             \
    X(FifoRelaxed)      \
    X(Immediate)        \
    X(Mailbox)

/// Windowing backends wma can create a window through. Mirrors
/// wma::WindowBackend; keep in sync by hand, there's no cross-library way to
/// generate one from the other.
#define WINDOW_BACKEND_LIST \
    X(GLFW)                 \
    X(SDL3)                 \
    X(X11)                  \
    X(WAYLAND)

/**
 * @brief Audio backends wma can play sound through. Mirrors wma::AudioBackend
 *        (separate axis from WINDOW_BACKEND_LIST -- only SDL3 appears in both).
 *
 * Two columns, unlike the lists above: AudioBackend's enumerators are
 * PascalCase (Alsa, Sdl3, Null), so the second column carries the uppercase
 * form a case-folded config value is compared against.
 */
#define AUDIO_BACKEND_LIST \
    X(Alsa, "ALSA")        \
    X(Sdl3, "SDL3")        \
    X(Null, "NULL")

namespace aura3d {

/**
 * @brief Converts a string representation to a wma::WindowBackend. Case-insensitive.
 * @return true if the string matches a known backend, false otherwise.
 */
inline bool WindowBackendFromString(const std::string& s, wma::WindowBackend& out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

#define X(name) if (up == #name) { out = wma::WindowBackend::name; return true; }
    WINDOW_BACKEND_LIST
#undef X

    return false;
}

/**
 * @brief Converts a wma::WindowBackend to its exact string representation.
 */
inline const char* WindowBackendToString(wma::WindowBackend backend)
{
    switch (backend)
    {
#define X(name) case wma::WindowBackend::name: return #name;
        WINDOW_BACKEND_LIST
#undef X
    }
    return "UNKNOWN";
}

/**
 * @brief Converts a string representation to a wma::AudioBackend. Case-insensitive.
 * @return true if the string matches a known backend, false otherwise.
 */
inline bool AudioBackendFromString(const std::string& s, wma::AudioBackend& out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

#define X(name, upper) if (up == upper) { out = wma::AudioBackend::name; return true; }
    AUDIO_BACKEND_LIST
#undef X

    //! Spellings a config file is likely to use that the enumerators do not
    //! cover. "SDL" without the version number is the common one.
    if (up == "SDL")                    { out = wma::AudioBackend::Sdl3; return true; }
    if (up == "NONE" || up == "SILENT") { out = wma::AudioBackend::Null; return true; }

    return false;
}

/**
 * @brief Converts a wma::AudioBackend to its exact string representation.
 */
inline const char* AudioBackendToString(wma::AudioBackend backend)
{
    switch (backend)
    {
#define X(name, upper) case wma::AudioBackend::name: return #name;
        AUDIO_BACKEND_LIST
#undef X
    }
    return "UNKNOWN";
}

/**
 * @brief Strongly-typed enum selecting how the swapchain presents frames.
 *
 * AutoVsync and AutoNoVsync are portable fallback groups; the rest request a
 * specific Vulkan present mode and fall back to Fifo (always supported) when
 * the surface doesn't support it.
 */
enum class VSyncMode
{
#define X(name) name,
    VSYNC_MODE_LIST
#undef X
};

/**
 * @brief Converts a string representation to a VSyncMode enum. Case-insensitive.
 * @return true if the string matches a known mode, false otherwise.
 */
inline bool VSyncModeFromString(const std::string& s, VSyncMode& out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

#define X(name) if (up == #name) { out = VSyncMode::name; return true; }
    VSYNC_MODE_LIST
#undef X

    return false;
}

/**
 * @brief Converts a VSyncMode enum value to its exact string representation.
 */
inline const char* VSyncModeToString(VSyncMode mode)
{
    switch (mode)
    {
#define X(name) case VSyncMode::name: return #name;
        VSYNC_MODE_LIST
#undef X
    }
    return "UNKNOWN";
}

/**
 * @struct AuraConfig
 * @brief The engine's configuration as a plain C++ value.
 *
 * Every field is the value the matching AuraSettings accessor returns when the
 * JSON document says nothing about it. Hand one to Engine and the application
 * needs no settings.json at all -- which is the point: a tool built on Aura3D
 * (an image viewer, a level exporter) ships one binary rather than a binary
 * plus a file it must not lose.
 *
 * With a file as well, the file wins key by key: an absent key reads from
 * here, a present one overrides. Nothing merges the two documents -- the
 * defaults are held separately, so reload() cannot drop them.
 *
 * @code
 * aura3d::AuraConfig config;
 * config.window.title  = "Viewer";
 * config.window.width  = 1024;
 * config.window.height = 768;
 * config.renderer.backend = "opengl";
 *
 * Engine engine(config);                    // no settings.json anywhere
 * Engine tweakable(config, "settings.json"); // ... or one that may override
 * @endcode
 */
struct AuraConfig {
    struct Window {
        int width = 1280;
        int height = 720;

        //! Empty takes APPLICATION_NAME.
        std::string title;

        wma::WindowBackend backend = wma::WindowBackend::SDL3;

        bool resizable = true;
        bool fullscreen = false;
        bool vsync = false;

        //! Unset derives the mode from @c vsync: Fifo when on, AutoNoVsync off.
        std::optional<VSyncMode> vsyncMode;

        int fpsLimit = 60;
    } window;

    struct Renderer {
        std::string backend = "vulkan";

        //! Unset follows the build: on in a debug build, off under NDEBUG.
        std::optional<bool> validationLayers;

        int maxFramesInFlight = 2;
    } renderer;

    struct Graphics {
        std::string gpuPreference = "discrete";
        int msaaSamples = 1;
        int cpuThreads = 0;
    } graphics;

    struct Audio {
        //! Unset resolves through wma::getDefaultAudioBackend(), as the string
        //! "auto" does in the JSON.
        std::optional<wma::AudioBackend> backend;

        f32 masterVolume = 1.0f;
        int sampleRate = 48000;
        int channels = 2;
        int bufferFrames = 1024;
        int maxVoices = 32;
    } audio;

    struct Paths {
        std::string shaders = "./resources/shaders/";
        std::string textures = "./resources/textures/";
        std::string models = "./resources/models/";
        std::string audio = "./resources/audio/";
        std::string logs = "./logs/";
    } paths;

    struct Logging {
        //! Unset follows the build: TRACE in a debug build, INFO under NDEBUG.
        std::optional<ink::LogLevel> level;

        bool toFile = false;
    } logging;
};

/**
 * @class AuraSettings
 * @brief Process-wide access to the engine configuration document.
 *
 * The typed accessors below wrap ink::EnhancedJson::getPath<T> with the
 * engine's schema paths and sensible defaults, so callers never need to know
 * the JSON layout or repeat literals. The raw document remains reachable via
 * getSettings() for subsystems with bespoke schemas (e.g. the VMA config).
 */
class AuraSettings {
public:
    static AuraSettings* get();

    ink::EnhancedJson* getSettings();

    /**
     * @brief Installs the values every accessor falls back to.
     *
     * Held beside the JSON document rather than merged into it, so reload()
     * cannot drop them. Call before anything reads a setting -- Engine's
     * AuraConfig constructors do.
     */
    void setDefaults(const AuraConfig& defaults);

    //! What an unset key currently reads as.
    [[nodiscard]] const AuraConfig& defaults() const noexcept { return _defaults; }

    /**
     * @brief Replaces the in-memory configuration with the contents of @p path.
     *
     * Intended for picking up hot-edited config at runtime. Values already read
     * by live subsystems are not re-applied; re-create or re-query them.
     * Leaves setDefaults()' values standing, so a file that sets nothing -- or
     * is not there at all -- degrades to them rather than to nothing.
     */
    void reload(const std::string& path);

    //! Window
    int         getWindowWidth()     const;   //! default 1280
    int         getWindowHeight()    const;   //! default 720
    std::string getWindowTitle()     const;   //! default "Aura3D"
    //! Windowing backend wma creates the window through. Default "SDL3";
    //! unrecognized values fall back to it with a warning.
    wma::WindowBackend getWindowBackend() const;
    bool        getWindowResizable() const;   //! default true
    bool        getFullscreen()      const;   //! default false
    bool        getVSync()           const;   //! default false
    VSyncMode   getVSyncMode()       const;   //! default: window/vsync_mode if set, else derived from getVSync()
    int         getFPSLimit()        const;   //! default 60

    //! Renderer
    std::string getRendererBackend()  const;  //! default "vulkan"
    bool        getValidationLayers() const;  //! default true in debug builds
    //! Frames the CPU may record ahead of the GPU (Vulkan only). Every
    //! per-frame GPU resource is sized to this at renderer init; see
    //! aura3d::vk::GetMaxFramesInFlight(). Raising it trades latency and
    //! memory for more CPU/GPU overlap -- check the WaitFence phase of an
    //! AURA_PROFILE_FRAME report before assuming it will help.
    int         getMaxFramesInFlight() const; //! default 2

    //! Graphics (device selection & visual quality; Vulkan-only)
    std::string getGpuPreference()    const;  //! "discrete" | "integrated" | "any", default "discrete"
    int         getMsaaSamples()      const;  //! default 1 (off); clamped to the device's max supported count

    //! CPU (software) backend
    //! Worker threads the row-band rasteriser splits a frame across. 0 (the
    //! default) auto-detects via std::thread::hardware_concurrency(); any
    //! positive value pins the count instead (useful to leave headroom for
    //! other processes, or to force single-threaded rendering for profiling).
    int getCpuThreads() const;

    //! Audio
    //! Platform API sound is played through. Default "auto" (resolves via
    //! wma::getDefaultAudioBackend(): ALSA on desktop Linux, SDL3 elsewhere).
    //! Independent of window.backend.
    wma::AudioBackend getAudioBackend() const;
    f32  getMasterVolume()     const;         //! default 1.0, clamped to [0, 1]
    int  getAudioSampleRate()  const;         //! default 48000
    int  getAudioChannels()    const;         //! default 2 (stereo)
    //! Frames the device buffers per callback: the latency dial. Default 1024
    //! (~21 ms at 48 kHz). Lower means tighter timing and more risk of dropouts.
    int  getAudioBufferFrames() const;
    //! Voices that can sound simultaneously. Default 32; further play() calls
    //! are dropped rather than stealing an audible voice.
    int  getAudioMaxVoices()   const;

    //! Paths
    std::string getShadersPath()  const;      //! default "./resources/shaders/"
    std::string getTexturesPath() const;      //! default "./resources/textures/"
    std::string getModelsPath()   const;      //! default "./resources/models/"
    std::string getAudioPath()    const;      //! default "./resources/audio/"
    std::string getLogsPath()     const;      //! default "./logs/"

    //! Logging
    ink::LogLevel getLogLevel()  const;       //! default: TRACE in debug builds, INFO in release
    bool          getLogToFile() const;       //! default false

private:
    //! An object rather than a null document, so a lookup made before any
    //! reload() is an ordinary miss instead of a thrown-and-caught type error.
    ink::EnhancedJson _settings = ink::EnhancedJson::object();

    AuraConfig _defaults{};
};

} // namespace aura3d

#endif // AURASETTINGS_H
