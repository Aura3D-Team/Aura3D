#ifndef ENGINE_H
#define ENGINE_H

#include "aura/Renderer/IRenderer.h"

class Engine
{
public:
    Engine(const std::string& configPath);
    ~Engine();

    aura3d::IRenderer* getRenderer() const { return _renderer.get(); }
    aura3d::RendererChoice getBackend() const { return _rendererChoice; }

private:
    void _configureWindow();
    void _createRenderer();

private:
    std::unique_ptr<aura3d::IRenderer> _renderer;
    wma::WindowDetails _windowDetails;
    aura3d::RendererChoice _rendererChoice = aura3d::RendererChoice::SOFTWARE;
};

#endif // ENGINE_H
