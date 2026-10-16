#include "aura/Core/Engine.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Renderer/RendererFactory.h"

#include <filesystem>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

Engine::Engine(const std::string &configPath) : Engine(aura3d::AuraConfig{}, configPath)
{
}

Engine::Engine(const aura3d::AuraConfig &config) : Engine(config, std::string{})
{
}

Engine::Engine(const aura3d::AuraConfig &defaults, const std::string &configPath,
               aura3d::IRenderer::WindowFactory windowFactory)
    : _windowFactory(std::move(windowFactory))
{
    INK_CORE_LOGGER;
    INK_CORE_LOGGER->setName(APPLICATION_NAME);

    aura3d::AuraSettings::get()->configure(defaults, configPath);

    const aura3d::AuraSettings *config = aura3d::AuraSettings::get();
    ink::LogManager::getInstance().setGlobalLevel(config->logLevel());
    if (config->get(aura3d::setting::loggingFile))
    {
        const std::string logsPath = config->get(aura3d::setting::pathLogs);
        std::error_code ec;
        std::filesystem::create_directories(logsPath, ec);
        ink::LogManager::getInstance().setLogToFile(logsPath + APPLICATION_NAME + ".log");
    }

    _jobs = std::make_unique<aura3d::JobSystem>(config->get(aura3d::setting::rendererCpuThreads));

    _configureWindow();
    _createRenderer();

    _createAudio();

    _createDebugMode();
}

Engine::~Engine()
{
#ifdef AURA_ENABLE_DEBUG_MODE
    aura3d::installFrameObserver(nullptr);
    _debugMode.reset();
#endif

    _audio.reset();
}

const aura3d::AuraSettings *Engine::getSettings() const
{
    return aura3d::AuraSettings::get();
}

void Engine::_configureWindow()
{
    const aura3d::AuraSettings *config = aura3d::AuraSettings::get();

    _windowDetails = {};
    _windowDetails.width = config->get(aura3d::setting::windowWidth);
    _windowDetails.height = config->get(aura3d::setting::windowHeight);
    _windowDetails.resizable = config->get(aura3d::setting::windowResizable);
    _windowDetails.fullscreen = config->get(aura3d::setting::windowFullscreen);
    _windowDetails.decorationMode = config->windowDecorations();
    _windowDetails.vsync = aura3d::VSyncModeSyncsToDisplay(config->presentMode());
    _windowDetails.targetFPS = _windowDetails.vsync ? 0 : config->get(aura3d::setting::presentFpsLimit);

#ifdef __EMSCRIPTEN__
    double cssWidth = 0.0, cssHeight = 0.0;
    if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) == EMSCRIPTEN_RESULT_SUCCESS &&
        cssWidth > 0.0 && cssHeight > 0.0)
    {
        _windowDetails.width = static_cast<int>(cssWidth);
        _windowDetails.height = static_cast<int>(cssHeight);
    }
#endif
}

void Engine::_createRenderer()
{
    const aura3d::AuraSettings *config = aura3d::AuraSettings::get();

    const std::string defaultBackend = aura3d::RendererChoiceToString(aura3d::RendererFactory::defaultChoice());

    const std::string backendStr = config->get(aura3d::setting::rendererApi);

    aura3d::RendererChoice requested = aura3d::RendererFactory::defaultChoice();
    if (backendStr != "auto" && !aura3d::RendererChoiceFromString(backendStr, requested))
    {
        INK_WARN << "Unsupported renderer '" << backendStr << "'; falling back to " << defaultBackend;
        requested = aura3d::RendererFactory::defaultChoice();
    }

    _adoptRenderer(requested);

    INK_INFO << "Window backend: " << aura3d::WindowBackendToString(config->windowBackend());
}

void Engine::_createAudio()
{
    const aura3d::AuraSettings *config = aura3d::AuraSettings::get();

    wma::AudioDeviceConfig deviceConfig;
    deviceConfig.sampleRate = static_cast<u32>(config->get(aura3d::setting::audioSampleRate));
    deviceConfig.channelCount = static_cast<u16>(config->get(aura3d::setting::audioChannels));
    deviceConfig.framesPerBuffer = static_cast<u32>(config->get(aura3d::setting::audioBufferFrames));

    _audio = std::make_unique<aura3d::AudioEngine>(wma::openAudioDevice(deviceConfig, config->audioBackend()),
                                                   static_cast<u32>(config->get(aura3d::setting::audioMaxVoices)));

    _audio->setMasterVolume(static_cast<f32>(config->get(aura3d::setting::audioVolume)));

    if (_resources)
        _resources->setAudioEngine(_audio.get());
}

void Engine::_createDebugMode()
{
#ifdef AURA_ENABLE_DEBUG_MODE
    _debugMode =
        std::make_unique<aura3d::DebugMode>(aura3d::DebugModeConfig::fromSettings(aura3d::AuraSettings::get()));

    _debugMode->attachRenderer(_renderer.get());

    aura3d::installFrameObserver(_debugMode.get());
#endif
}

void Engine::_adoptRenderer(aura3d::RendererChoice choice)
{
    _renderer = aura3d::RendererFactory::create(choice, _windowDetails);
    _rendererChoice = _renderer->getBackendType();

    INK_INFO << "Backend: " << aura3d::RendererChoiceToString(_rendererChoice);

    _renderer->setWindowFactory(_windowFactory);
    _renderer->setGpuTimingEnabled(aura3d::AuraSettings::get()->get(aura3d::setting::rendererGpuTiming));
    _renderer->initialize(aura3d::AuraSettings::get(), _jobs.get());

    if (_resources)
        _resources->setRenderer(_renderer.get());
    else
        _resources = std::make_unique<aura3d::ResourceManager>(_renderer.get());

#ifdef AURA_ENABLE_DEBUG_MODE
    if (_debugMode)
        _debugMode->attachRenderer(_renderer.get());
#endif
}

void Engine::switchBackend(aura3d::RendererChoice choice)
{
    const aura3d::RendererChoice resolved = aura3d::RendererFactory::resolve(choice);

    if (resolved == _rendererChoice && _renderer)
    {
        INK_INFO << "switchBackend: already running on " << aura3d::RendererChoiceToString(resolved);
        return;
    }

    INK_INFO << "switchBackend: " << aura3d::RendererChoiceToString(_rendererChoice) << " -> "
             << aura3d::RendererChoiceToString(resolved);

    if (_resources)
        _resources->unloadAll();

#ifdef AURA_ENABLE_DEBUG_MODE
    if (_debugMode)
        _debugMode->attachRenderer(nullptr);
#endif

    if (_renderer)
    {
        _renderer->cleanup();
        _renderer.reset();
    }

    _configureWindow();

    _adoptRenderer(resolved);
}
