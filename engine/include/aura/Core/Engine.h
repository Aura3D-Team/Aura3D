#ifndef ENGINE_H
#define ENGINE_H

#include <memory>
#include <string>

#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/ResourceManager/ResourceManager.h"
#include "aura/Renderer/IRenderer.h"

/**
 * @class Engine
 * @brief Owns the configuration, the renderer and the asset cache.
 */
class Engine
{
public:
    explicit Engine(const std::string& configPath);
    ~Engine();

    aura3d::IRenderer* getRenderer() const { return _renderer.get(); }
    aura3d::RendererChoice getBackend() const { return _rendererChoice; }

    //! Engine-wide configuration. Use the typed accessors rather than reaching
    //! for the raw JSON.
    const aura3d::AuraSettings* getSettings() const;

    //! Path-keyed texture/mesh cache bound to the current renderer.
    aura3d::ResourceManager* resources() { return _resources.get(); }

    /**
     * @brief Tears the current renderer down and brings up @p choice instead.
     *
     * The request is resolved through RendererFactory, so an unavailable
     * backend degrades rather than failing. Every handle previously issued by
     * the old renderer becomes invalid and the asset cache is dropped: reload
     * assets through resources() afterwards.
     *
     * No-op when @p choice already resolves to the active backend.
     */
    void switchBackend(aura3d::RendererChoice choice);

private:
    void _configureWindow();
    void _createRenderer();

private:
    std::unique_ptr<aura3d::IRenderer> _renderer;
    std::unique_ptr<aura3d::ResourceManager> _resources;
    wma::WindowDetails _windowDetails;
    aura3d::RendererChoice _rendererChoice = aura3d::RendererChoice::SOFTWARE;
};

#endif // ENGINE_H
