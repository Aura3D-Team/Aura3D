#include "aura/Core/Engine.h"

#include "aura/Core/AuraSettings/AuraSettings.h"

Engine::Engine(const std::string& configPath)
{
#ifdef NDEBUG
    const ink::LogLevel logSeverity = ink::LogLevel::INFO;
#else
    const ink::LogLevel logSeverity = ink::LogLevel::TRACE;
#endif

    INK_CORE_LOGGER;
    INK_CORE_LOGGER->setName(APPLICATION_NAME);
    ink::LogManager::getInstance().setGlobalLevel(logSeverity);
    auto engine_settings = AuraSettings::get()->getSettings();
    *engine_settings = ink::EnhancedJson::loadFromFile(configPath);

    // Configure Internal State
    configureWindow();
    createRenderer();
}

Engine::~Engine() = default;

void Engine::configureWindow()
{
    auto config = AuraSettings::get()->getSettings();

    _windowDetails = {};
    _windowDetails.width = config->getPath<uint>("window.width", 1280);
    _windowDetails.height = config->getPath<uint>("window.height", 720);
    _windowDetails.resizable = config->getPath<uint>("window.resizable", true);
    _windowDetails.vsync = config->getPath<uint>("window.vsync", false);
    _windowDetails.targetFPS = config->getPath<uint>("window.fps", 60);

    if (_windowDetails.vsync) {
        _windowDetails.targetFPS = 0;
    }
}

void Engine::createRenderer()
{
    auto config = AuraSettings::get()->getSettings();

    // Determine Renderer Backend
    aura3d::RendererChoice renderChoice;
    INK_ASSERT_MSG(aura3d::RendererChoiceFromString(config->get<std::string>("renderer_backend"), renderChoice), "Unsupported renderer backend.");

    switch (renderChoice)
    {
    case aura3d::RendererChoice::SOFTWARE:
        INK_INFO << "Initializing Software Renderer...";
        _renderer = std::make_unique<aura3d::cpu::CPURenderer>(_windowDetails);
        break;

    case aura3d::RendererChoice::OPENGL:
        INK_INFO << "Initializing OpenGL Renderer...";
        _renderer = std::make_unique<aura3d::gl::OpenGLRenderer>(_windowDetails);
        break;

    case aura3d::RendererChoice::VULKAN:
        _renderer = std::make_unique<aura3d::vk::VulkanRenderer>(_windowDetails);
        break;
    }
}
