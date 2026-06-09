#include "aura/Core/Engine.h"

#ifdef AURA_HAS_OPENGL
#include "aura/Renderer/OpenGL/OpenGLRenderer.h"
#endif
#ifdef AURA_HAS_VULKAN
#include "aura/Renderer/Vulkan/VulkanRenderer.h"
#endif
#ifdef AURA_HAS_CPU
#include "aura/Renderer/Software/CPURenderer.h"
#endif

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
    auto engine_settings = aura3d::AuraSettings::get()->getSettings();
    *engine_settings = ink::EnhancedJson::loadFromFile(configPath);

    configureWindow();
    createRenderer();
}

Engine::~Engine() = default;

void Engine::configureWindow()
{
    auto config = aura3d::AuraSettings::get()->getSettings();

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
    auto config = aura3d::AuraSettings::get()->getSettings();

    std::string backendStr = config->getPath<std::string>("renderer/backend", "vulkan");
    INK_ASSERT_MSG(
        aura3d::RendererChoiceFromString(backendStr, _rendererChoice),
        "Unsupported renderer backend: " + backendStr
    );

    INK_INFO << "Backend: " << aura3d::RendererChoiceToString(_rendererChoice);

    switch (_rendererChoice)
    {
#ifdef AURA_HAS_CPU
    case aura3d::RendererChoice::SOFTWARE:
        INK_INFO << "Initializing Software Renderer...";
        _renderer = std::make_unique<aura3d::cpu::CPURenderer>(_windowDetails);
        break;
#endif

#ifdef AURA_HAS_OPENGL
    case aura3d::RendererChoice::OPENGL:
        INK_INFO << "Initializing OpenGL Renderer...";
        _renderer = std::make_unique<aura3d::gl::OpenGLRenderer>(_windowDetails);
        break;
#endif

#ifdef AURA_HAS_VULKAN
    case aura3d::RendererChoice::VULKAN:
        INK_INFO << "Initializing Vulkan Renderer...";
        _renderer = std::make_unique<aura3d::vk::VulkanRenderer>(_windowDetails);
        break;
#endif
    }

    _renderer->initialize(aura3d::AuraSettings::get());
}
