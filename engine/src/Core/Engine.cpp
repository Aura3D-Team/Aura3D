#include "aura/Core/Engine.h"
#include "aura/Renderer/RendererFactory.h"
#include "aura/Core/AuraSettings/AuraSettings.h"

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

    //! Before the renderer: a backend's own worker pool is sized from the same
    //! setting, and both are read once here rather than re-resolved per use.
    _jobs = std::make_unique<aura3d::JobSystem>(config->getCpuThreads());

    _configureWindow();
    _createRenderer();

    //! After the renderer, but independent of it: audio has no dependency on a
    //! graphics backend, and _adoptRenderer() deliberately leaves it alone so a
    //! backend switch does not interrupt playback.
    _createAudio();

    //! Last: it samples every other subsystem, and _adoptRenderer() re-attaches
    //! it across a backend switch, so it has to exist by the time one happens.
    _createDebugMode();
}

Engine::~Engine()
{
#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * Before everything else, and explicitly rather than by member order.
     *
     * Two orderings have to hold and neither is obvious from the declarations:
     * the observer must leave FrameProfiler's slot before it is destroyed
     * (the profiler holds a raw pointer to it), and the final report the
     * destructor writes reads the renderer's GPU counters, so it cannot outlive
     * the renderer.
     */
    aura3d::installFrameObserver(nullptr);
    _debugMode.reset();
#endif

    /*
     * Audio goes first, explicitly. Its device runs a thread that reads clip
     * data owned by the AudioEngine, and on the SDL backend the device also
     * lives inside an SDL subsystem the window manager will tear down with
     * SDL_Quit() as the last window dies. Both orderings only work if audio is
     * gone before the renderer, and member destruction order alone would not
     * guarantee that if the members are ever reordered.
     */
    _audio.reset();
}

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

    _adoptRenderer(requested);

    INK_INFO << "Window backend: " << aura3d::WindowBackendToString(config->getWindowBackend());
}

void Engine::_createAudio()
{
    const aura3d::AuraSettings* config = aura3d::AuraSettings::get();

    wma::AudioDeviceConfig deviceConfig;
    deviceConfig.sampleRate      = static_cast<u32>(config->getAudioSampleRate());
    deviceConfig.channelCount    = static_cast<u16>(config->getAudioChannels());
    deviceConfig.framesPerBuffer = static_cast<u32>(config->getAudioBufferFrames());

    /*
     * openAudioDevice() degrades until something opens, ending at the null
     * device -- so this always yields a usable device and audio() is never
     * null, even on a machine with no sound hardware at all.
     */
    _audio = std::make_unique<aura3d::AudioEngine>(
        wma::openAudioDevice(deviceConfig, config->getAudioBackend()),
        static_cast<u32>(config->getAudioMaxVoices()));

    _audio->setMasterVolume(config->getMasterVolume());

    //! The cache needs both halves: the renderer for textures and meshes, this
    //! for sounds. Set here rather than in _adoptRenderer() because the audio
    //! engine outlives every backend switch.
    if (_resources)
        _resources->setAudioEngine(_audio.get());
}

void Engine::_createDebugMode()
{
#ifdef AURA_ENABLE_DEBUG_MODE
    _debugMode = std::make_unique<aura3d::DebugMode>(
        aura3d::DebugModeConfig::fromSettings(aura3d::AuraSettings::get()));

    _debugMode->attachRenderer(_renderer.get());

    //! From here on every AURA_FRAME_END() in the active backend lands in the
    //! sample ring. Detached again in the destructor, before _debugMode dies.
    aura3d::installFrameObserver(_debugMode.get());
#endif
}

void Engine::_adoptRenderer(aura3d::RendererChoice choice)
{
    /*
     * The single path by which this Engine comes to own a renderer, shared by
     * first-time startup and switchBackend(). Keeping it in one place is what
     * stops the two from drifting: every step here has to happen on both paths,
     * and a step added to only one of them fails in whichever the author was
     * not looking at.
     */

    // create() resolves an unavailable backend to whatever this build does have
    // (RendererFactory::resolve), so record what we actually ended up with. It
    // also points Camera at the backend's clip-space convention.
    _renderer = aura3d::RendererFactory::create(choice, _windowDetails);
    _rendererChoice = _renderer->getBackendType();

    INK_INFO << "Backend: " << aura3d::RendererChoiceToString(_rendererChoice);

    //! _jobs is constructed before the very first call to _adoptRenderer()
    //! (see the constructor), so it is always safe to hand over here, on both
    //! the startup path and switchBackend()'s.
    _renderer->initialize(aura3d::AuraSettings::get(), _jobs.get());

    //! Rebound rather than rebuilt where possible: the cache survives a backend
    //! switch as an object, even though every handle in it does not.
    if (_resources)
        _resources->setRenderer(_renderer.get());
    else
        _resources = std::make_unique<aura3d::ResourceManager>(_renderer.get());

#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * Rebound like the cache, and for the same reason: the GPU counters the
     * report reads belong to the backend, and the outgoing one's are gone. Null
     * on the very first call, when the constructor has not built it yet --
     * _createDebugMode() attaches the renderer itself in that case.
     */
    if (_debugMode)
        _debugMode->attachRenderer(_renderer.get());
#endif
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

#ifdef AURA_ENABLE_DEBUG_MODE
    //! For the same reason as the cache above: a raw pointer to a renderer that
    //! is about to be destroyed. _adoptRenderer() hands it the new one.
    if (_debugMode)
        _debugMode->attachRenderer(nullptr);
#endif

    if (_renderer)
    {
        _renderer->cleanup();
        _renderer.reset();
    }

    // Re-read the window config so a hot-edited settings.json takes effect here.
    _configureWindow();

    _adoptRenderer(resolved);
}
