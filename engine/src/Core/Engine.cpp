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

    configureWindow();
    createRenderer();
}

Engine::~Engine() = default;

void Engine::configureWindow()
{
    auto config = AuraSettings::get()->getSettings();

    _windowDetails = {};
    _windowDetails.width      = config->getPath<uint>("window/width", 1280);
    _windowDetails.height     = config->getPath<uint>("window/height", 720);
    _windowDetails.resizable  = config->getPath<uint>("window/resizable", true);
    _windowDetails.vsync      = config->getPath<uint>("window/vsync", false);
    _windowDetails.targetFPS  = config->getPath<uint>("window/fps_limit", 60);

    if (_windowDetails.vsync) {
        _windowDetails.targetFPS = 0;
    }
}

void Engine::createRenderer()
{
    auto config = AuraSettings::get()->getSettings();

    std::string backendStr = config->getPath<std::string>("renderer/backend", "software");
    INK_ASSERT_MSG(
        aura3d::RendererChoiceFromString(backendStr, _rendererChoice),
        "Unsupported renderer backend: " + backendStr
    );

    std::string modeStr = config->getPath<std::string>("renderer/mode", "2d");
    INK_ASSERT_MSG(
        aura3d::RendererModeFromString(modeStr, _rendererMode),
        "Unsupported renderer mode: " + modeStr
    );

    INK_INFO << "Backend: " << aura3d::RendererChoiceToString(_rendererChoice)
             << " | Mode: " << aura3d::RendererModeToString(_rendererMode);

    switch (_rendererChoice)
    {
    case aura3d::RendererChoice::SOFTWARE:
        INK_INFO << "Initializing Software Renderer...";
        _renderer = std::make_unique<aura3d::cpu::CPURenderer>(_windowDetails, _rendererMode);
        break;

    case aura3d::RendererChoice::OPENGL:
        INK_INFO << "Initializing OpenGL Renderer...";
        _renderer = std::make_unique<aura3d::gl::OpenGLRenderer>(_windowDetails, _rendererMode);
        break;

    case aura3d::RendererChoice::VULKAN:
        INK_INFO << "Initializing Vulkan Renderer...";
        _renderer = std::make_unique<aura3d::vk::VulkanRenderer>(_windowDetails, _rendererMode);
        break;
    }
}
