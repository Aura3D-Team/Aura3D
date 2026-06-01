#ifndef ENGINE_H
#define ENGINE_H

#include "aura/aura.h"

#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Software/CPURenderer.h"
#include "aura/Renderer/Vulkan/VulkanRenderer.h"
#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

class Engine
{
public:
    Engine(const std::string& configPath);
    ~Engine();

    aura3d::IRenderer* getRenderer() const { return _renderer.get(); }
    aura3d::RendererChoice getBackend() const { return _rendererChoice; }
    aura3d::RendererMode getMode() const { return _rendererMode; }

private:
    void configureWindow();
    void createRenderer();

private:
    std::unique_ptr<aura3d::IRenderer> _renderer;
    wma::WindowDetails _windowDetails;
    aura3d::RendererChoice _rendererChoice = aura3d::RendererChoice::SOFTWARE;
    aura3d::RendererMode _rendererMode = aura3d::RendererMode::MODE_2D;
};

#endif // ENGINE_H
