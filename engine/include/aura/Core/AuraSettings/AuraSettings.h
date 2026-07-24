#ifndef AURASETTINGS_H
#define AURASETTINGS_H

#include <cctype>
#include <string>

#include <ink/EnhancedJson.h>

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

namespace aura3d {

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
    bool        getWindowResizable() const;   //! default true
    bool        getFullscreen()      const;   //! default false
    bool        getVSync()           const;   //! default false
    VSyncMode   getVSyncMode()       const;   //! default: window/vsync_mode if set, else derived from getVSync()
    int         getFPSLimit()        const;   //! default 60

    //! Renderer
    std::string getRendererBackend()  const;  //! default "vulkan"
    bool        getValidationLayers() const;  //! default true in debug builds
    int         getMaxFramesInFlight() const; //! default 2

    //! Paths-
    std::string getShadersPath()  const;      //! default "./resources/shaders/"
    std::string getTexturesPath() const;      //! default "./resources/textures/"
    std::string getModelsPath()   const;      //! default "./resources/models/"

private:
    ink::EnhancedJson _settings;
};

} // namespace aura3d

#endif // AURASETTINGS_H
