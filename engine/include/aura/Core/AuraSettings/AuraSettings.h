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
    //! Informational only: MAX_FRAMES_IN_FLIGHT (VkAuraCore.h) is a compile-time
    //! constant, so this value is not propagated to the Vulkan renderer.
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

    //! Paths
    std::string getShadersPath()  const;      //! default "./resources/shaders/"
    std::string getTexturesPath() const;      //! default "./resources/textures/"
    std::string getModelsPath()   const;      //! default "./resources/models/"
    std::string getLogsPath()     const;      //! default "./logs/"

    //! Logging
    ink::LogLevel getLogLevel()  const;       //! default: TRACE in debug builds, INFO in release
    bool          getLogToFile() const;       //! default false

private:
    ink::EnhancedJson _settings;
};

} // namespace aura3d

#endif // AURASETTINGS_H
