#ifndef ENGINE_H
#define ENGINE_H

#include "aura/aura.h"

#include "aura/Renderer/Software/CPURenderer.h"
#include "aura/Renderer/Vulkan/VulkanRenderer.h"
#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

class Engine
{
public:
    Engine(const std::string& configPath);
    ~Engine();

    // The main entry point to start the game loop
    void run();

private:
    // Helper to read window settings from config
    void configureWindow();

    // Instantiate the renderer
    void createRenderer();

private:
    std::unique_ptr<aura3d::IRenderer> _renderer;
    wma::WindowDetails _windowDetails;
};

#endif // ENGINE_H
