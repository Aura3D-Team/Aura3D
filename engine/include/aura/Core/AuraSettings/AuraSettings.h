#ifndef AURASETTINGS_H
#define AURASETTINGS_H

#include <cctype>
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

/**
 * @brief List of windowing backends wma can create a window through.
 *
 * Mirrors @c wma::WindowBackend (defined in libwma's core/Types.hpp, not
 * here) rather than declaring a fresh enum -- unlike VSYNC_MODE_LIST, which
 * owns the type it enumerates, this list only owns the string<->enum mapping
 * for a type the engine doesn't control. Keep in sync with wma::WindowBackend
 * by hand; there's no way to generate one from the other across the library
 * boundary.
 */
#define WINDOW_BACKEND_LIST \
    X(GLFW)                 \
    X(SDL3)                 \
    X(X11)                  \
    X(WAYLAND)

/**
 * @brief List of audio backends wma can play sound through.
 *
 * Mirrors @c wma::AudioBackend, and like WINDOW_BACKEND_LIST it owns only the
 * string<->enum mapping for a type the engine does not control — keep the two
 * in sync by hand.
 *
 * Deliberately a separate axis from WINDOW_BACKEND_LIST: GLFW, X11 and Wayland
 * are display protocols with no audio API, so `window.backend` and
 * `audio.backend` are configured independently and only SDL3 appears in both.
 */
/*
 * Two arguments, unlike the lists above: wma::AudioBackend's enumerators are
 * PascalCase (Alsa, Sdl3, Null) rather than the all-caps WindowBackend uses, so
 * stringifying the enumerator does not give a token that a case-folded config
 * value can be compared against. The second column carries that uppercase form.
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
     * @brief Replaces the in-memory configuration with the contents of @p path.
     *
     * Intended for picking up hot-edited config at runtime. Values already read
     * by live subsystems are not re-applied; re-create or re-query them.
     */
    void reload(const std::string& path);

    //! Window
    int         getWindowWidth()     const;   //! default 1280
    int         getWindowHeight()    const;   //! default 720
    std::string getWindowTitle()     const;   //! default "Aura3D"
    //! Windowing library backend wma creates the window through. Default
    //! "SDL3" -- the only backend all three of Vulkan/OpenGL/CPU exercise on
    //! every platform this engine targets. An unrecognized value falls back
    //! to SDL3 with a warning, the same pattern getRendererBackend() uses.
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
    //! per-frame GPU resource -- command buffers, fences, semaphores, the
    //! transform/light UBOs and their descriptor sets, the overlay's
    //! vertex/index buffers -- is sized to this at renderer init; see
    //! aura3d::vk::GetMaxFramesInFlight() (VkAuraCore.h), which is what every
    //! one of those actually reads. Raising it trades a little latency and
    //! per-frame-resource memory for more CPU/GPU overlap; see the WaitFence
    //! phase in a AURA_PROFILE_FRAME report before assuming it will help --
    //! it targets only that phase, not Acquire or Present.
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
    /**
     * @brief Platform API sound is played through.
     *
     * Default "auto", which resolves through wma::getDefaultAudioBackend() —
     * ALSA on desktop Linux, SDL3 everywhere else. An unrecognized value falls
     * back to that same default with a warning, matching getRendererBackend().
     *
     * Independent of window.backend: the two are separate axes, and a value
     * here never constrains which windowing backend can be used.
     */
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
    ink::EnhancedJson _settings;
};

} // namespace aura3d

#endif // AURASETTINGS_H
