#include "aura/Core/Engine.h"
#include "aura/Renderer/RendererFactory.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/Camera/Camera.h"

#include <filesystem>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

Engine::Engine(const std::string& configPath)
{
    INK_CORE_LOGGER;
    INK_CORE_LOGGER->setName(APPLICATION_NAME);

    aura3d::AuraSettings::get()->reload(configPath);

    const aura3d::AuraSettings* config = aura3d::AuraSettings::get();
    ink::LogManager::getInstance().setGlobalLevel(config->getLogLevel());
    if (config->getLogToFile()) {
        const std::string logsPath = config->getLogsPath();
        std::error_code ec;
        std::filesystem::create_directories(logsPath, ec);
        ink::LogManager::getInstance().setLogToFile(logsPath + APPLICATION_NAME + ".log");
    }

    _configureWindow();
    _createRenderer();
}

Engine::~Engine() = default;

const aura3d::AuraSettings* Engine::getSettings() const
{
    return aura3d::AuraSettings::get();
}

void Engine::_configureWindow()
{
    const aura3d::AuraSettings* config = aura3d::AuraSettings::get();

    _windowDetails = {};
    _windowDetails.width = config->getWindowWidth();
    _windowDetails.height = config->getWindowHeight();
    _windowDetails.resizable = config->getWindowResizable();
    _windowDetails.fullscreen = config->getFullscreen();
    _windowDetails.vsync = config->getVSync();
    _windowDetails.targetFPS = config->getFPSLimit();

    if (_windowDetails.vsync) {
        //! The display drives pacing; an extra limiter would only fight it
        _windowDetails.targetFPS = 0;
    }

#ifdef __EMSCRIPTEN__
    // settings.json's window.width/height describe a fixed native window;
    // on the web there is no "window", only whatever CSS size the canvas
    // actually has in the browser viewport (index.html's canvas is styled
    // width:100%/height:100%). Override with the real viewport size here so
    // wma creates a window matching it from the very first frame, instead
    // of blindly creating whatever fixed size the config asked for -- SDL3
    // is the only window backend that works on Emscripten, and "#canvas"
    // is its default (and this project's only) canvas selector.
    double cssWidth = 0.0, cssHeight = 0.0;
    if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) == EMSCRIPTEN_RESULT_SUCCESS
        && cssWidth > 0.0 && cssHeight > 0.0) {
        _windowDetails.width = static_cast<int>(cssWidth);
        _windowDetails.height = static_cast<int>(cssHeight);
    }
#endif
}

void Engine::_createRenderer()
{
    const aura3d::AuraSettings* config = aura3d::AuraSettings::get();

    /* Use the compile-time default as the fallback so that WASM/Android
       builds work even if settings.json still says "vulkan". */
    const std::string defaultBackend =
        aura3d::RendererChoiceToString(aura3d::RendererFactory::defaultChoice());

    std::string backendStr = config->getRendererBackend();

    aura3d::RendererChoice requested;
    if (!aura3d::RendererChoiceFromString(backendStr, requested)) {
        INK_WARN << "Unsupported renderer backend '" << backendStr
                 << "'; falling back to " << defaultBackend;
        requested = aura3d::RendererFactory::defaultChoice();
    }

    // create() resolves an unavailable backend along VULKAN -> OPENGL ->
    // SOFTWARE, so record what we actually ended up with.
    _renderer = aura3d::RendererFactory::create(requested, _windowDetails);
    _rendererChoice = _renderer->getBackendType();
    aura3d::Camera::setClipSpace(_rendererChoice == aura3d::RendererChoice::VULKAN
                                 ? aura3d::Camera::ClipSpace::Vulkan
                                 : aura3d::Camera::ClipSpace::OpenGL);

    INK_INFO << "Backend: " << aura3d::RendererChoiceToString(_rendererChoice);
    INK_INFO << "Window backend: " << aura3d::WindowBackendToString(config->getWindowBackend());

    _renderer->initialize(aura3d::AuraSettings::get());

    if (_resources)
        _resources->setRenderer(_renderer.get());
    else
        _resources = std::make_unique<aura3d::ResourceManager>(_renderer.get());
}

void Engine::switchBackend(aura3d::RendererChoice choice)
{
    const aura3d::RendererChoice resolved = aura3d::RendererFactory::resolve(choice);

    if (resolved == _rendererChoice && _renderer) {
        INK_INFO << "switchBackend: already running on "
                 << aura3d::RendererChoiceToString(resolved);
        return;
    }

    INK_INFO << "switchBackend: " << aura3d::RendererChoiceToString(_rendererChoice)
             << " -> " << aura3d::RendererChoiceToString(resolved);

    /*
     * Drop the cache before the renderer dies: its handles refer to resources
     * owned by the outgoing backend and mean nothing to the incoming one.
     */
    if (_resources)
        _resources->unloadAll();

    if (_renderer) 
    {
        _renderer->cleanup();
        _renderer.reset();
    }

    // Re-read the window config so a hot-edited settings.json takes effect here.
    _configureWindow();

    _renderer = aura3d::RendererFactory::create(resolved, _windowDetails);
    _rendererChoice = _renderer->getBackendType();
    aura3d::Camera::setClipSpace(_rendererChoice == aura3d::RendererChoice::VULKAN
                                 ? aura3d::Camera::ClipSpace::Vulkan
                                 : aura3d::Camera::ClipSpace::OpenGL);
    _renderer->initialize(aura3d::AuraSettings::get());

    if (_resources)
        _resources->setRenderer(_renderer.get());
    else
        _resources = std::make_unique<aura3d::ResourceManager>(_renderer.get());
}
