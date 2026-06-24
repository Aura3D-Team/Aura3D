#include "aura/Core/Engine.h"
#include "aura/Renderer/RendererFactory.h"
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
    ink::EnhancedJson* engine_settings = aura3d::AuraSettings::get()->getSettings();
    *engine_settings = ink::EnhancedJson::loadFromFile(configPath);

    _configureWindow();
    _createRenderer();
}

Engine::~Engine() = default;

void Engine::_configureWindow()
{
    ink::EnhancedJson* config = aura3d::AuraSettings::get()->getSettings();

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

void Engine::_createRenderer()
{
    ink::EnhancedJson* config = aura3d::AuraSettings::get()->getSettings();

    /* Use the compile-time default as the fallback so that WASM/Android
       builds work even if settings.json still says "vulkan". */
    const std::string defaultBackend =
        aura3d::RendererChoiceToString(aura3d::RendererFactory::defaultChoice());

    std::string backendStr = config->getPath<std::string>("renderer/backend", defaultBackend);
    INK_ASSERT_MSG(
        aura3d::RendererChoiceFromString(backendStr, _rendererChoice),
        "Unsupported renderer backend: " + backendStr
    );

    INK_INFO << "Backend: " << aura3d::RendererChoiceToString(_rendererChoice);

    _renderer = aura3d::RendererFactory::create(_rendererChoice, _windowDetails);
    _renderer->initialize(aura3d::AuraSettings::get());
}
