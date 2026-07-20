#ifndef AURASETTINGS_H
#define AURASETTINGS_H

#include <string>

#include <ink/EnhancedJson.h>

namespace aura3d {

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
