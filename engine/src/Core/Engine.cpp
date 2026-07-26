#include "aura/Core/Engine.h"
#include "aura/Renderer/RendererFactory.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/Camera/Camera.h"

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

    aura3d::AuraSettings::get()->reload(configPath);

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
    _windowDetails.vsync = config->getVSync();
    _windowDetails.targetFPS = config->getFPSLimit();

    if (_windowDetails.vsync) {
        //! The display drives pacing; an extra limiter would only fight it
        _windowDetails.targetFPS = 0;
    }
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
    aura3d::Camera::setClipSpace(backend == aura3d::RendererChoice::VULKAN
                                 ? aura3d::Camera::ClipSpace::Vulkan
                                 : aura3d::Camera::ClipSpace::OpenGL);

    INK_INFO << "Backend: " << aura3d::RendererChoiceToString(_rendererChoice);

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
    aura3d::Camera::setClipSpace(backend == aura3d::RendererChoice::VULKAN
                                 ? aura3d::Camera::ClipSpace::Vulkan
                                 : aura3d::Camera::ClipSpace::OpenGL);
    _renderer->initialize(aura3d::AuraSettings::get());

    if (_resources)
        _resources->setRenderer(_renderer.get());
    else
        _resources = std::make_unique<aura3d::ResourceManager>(_renderer.get());
}
