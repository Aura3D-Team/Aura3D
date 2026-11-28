#ifndef ENGINE_H
#define ENGINE_H

#include <memory>
#include <string>

#include "aura/Core/AudioEngine/AudioEngine.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/DebugMode/DebugMode.h"
#include "aura/Core/JobSystem/JobSystem.h"
#include "aura/Renderer/IRenderer.h"

namespace aura3d
{

/**
 * @class Engine
 * @brief Owns the configuration, the renderer, the audio engine and the asset cache.
 */
class Engine
{
  public:
    //! Configured entirely from @p configPath, on the engine's built-in
    //! defaults for anything it does not set.
    explicit Engine(const std::string &configPath);

    /**
     * @brief Configured entirely from code, with no settings.json involved.
     *
     * For an application that ships as one binary and has nothing to hand a
     * user to edit -- a viewer, a tool, an exported demo.
     */
    explicit Engine(const aura3d::AuraConfig &config);

    /**
     * @brief @p defaults in code, overridden key by key by @p configPath.
     *
     * The file need not exist: what it does not set (or when it is not there
     * at all) reads from @p defaults. This is the form to reach for when an
     * application wants sensible built-in settings that a user may still
     * override without the application shipping a file of its own.
     */
    Engine(const aura3d::AuraConfig &defaults, const std::string &configPath,
           aura3d::IRenderer::WindowFactory windowFactory = {});

    ~Engine();

    //! The active renderer. A new object after switchBackend(), so fetch it again then.
    [[nodiscard]] IRenderer &renderer() const noexcept
    {
        return *_renderer;
    }
    [[nodiscard]] RendererChoice backend() const noexcept
    {
        return _rendererChoice;
    }

    //! Engine-wide configuration. Use the typed accessors rather than reaching
    //! for the raw JSON.
    [[nodiscard]] const AuraSettings &settings() const noexcept;

    /**
     * @brief Clip playback, mixing and 3D positional audio.
     *
     * A machine with no sound hardware gets a silent device rather than nothing.
     * Call AudioEngine::update() once per frame.
     */
    [[nodiscard]] AudioEngine &audio() const noexcept
    {
        return *_audio;
    }

    /**
     * @brief Benchmark/debug instrumentation, or nullptr outside an
     *        AURA_ENABLE_DEBUG_MODE build.
     *
     * Declared unconditionally (unlike the member behind it) so callers can
     * write `if (auto* debug = engine.debugMode()) debug->update(dt);` once
     * and have it compile and behave correctly in either build. The final
     * report is written by ~Engine() regardless of whether anything asked.
     */
    [[nodiscard]] aura3d::DebugMode *debugMode() const noexcept
    {
#ifdef AURA_ENABLE_DEBUG_MODE
        return _debugMode.get();
#else
        return nullptr;
#endif
    }

    /// Runs @p body over [0, @p itemCount) in parallel, returning once every
    /// band has finished. Shares the engine's thread pool rather than opening
    /// a new one. See aura3d::JobSystem::dispatch() for the band contract.
    void dispatch(i32 itemCount, const aura3d::JobSystem::BandBody &body) const
    {
        _jobs->dispatch(itemCount, body);
    }

    /// The same facility as a reference, for subsystems that take one without
    /// depending on the whole Engine.
    [[nodiscard]] const aura3d::JobSystem &jobs() const noexcept
    {
        return *_jobs;
    }

    /**
     * @brief Tears the current renderer down and brings up @p choice instead.
     *
     * Resolved through RendererFactory, so an unavailable backend degrades
     * rather than failing, and one that fails to start falls back further down
     * the chain. Every handle the old renderer gave out is invalid afterwards:
     * create textures and meshes again. No-op if @p choice already resolves to
     * the active backend.
     */
    void switchBackend(aura3d::RendererChoice choice);

  private:
    void _configureWindow();
    void _createRenderer();
    void _createAudio();

    /// Builds the debug subsystem and installs it as the frame observer.
    /// No-op without AURA_ENABLE_DEBUG_MODE; declared unconditionally so the
    /// constructor needs no guard around calling it.
    void _createDebugMode();

    /// Brings up @p choice through RendererFactory, falling back past a backend
    /// that fails to start, and adopts the result. The one path to owning a
    /// renderer, shared by startup and switchBackend() so the two cannot drift apart.
    void _adoptRenderer(aura3d::RendererChoice choice);

  private:
    aura3d::IRenderer::WindowFactory _windowFactory;
    std::unique_ptr<aura3d::JobSystem> _jobs;
    std::unique_ptr<aura3d::IRenderer> _renderer;
    std::unique_ptr<aura3d::AudioEngine> _audio;
#ifdef AURA_ENABLE_DEBUG_MODE
    //! Declared after _renderer: the final report reads the renderer's GPU
    //! counters, so it must be torn down first.
    std::unique_ptr<aura3d::DebugMode> _debugMode;
#endif
    wma::WindowDetails _windowDetails;
    aura3d::RendererChoice _rendererChoice = aura3d::RendererChoice::SOFTWARE;
};

} // namespace aura3d

#endif // ENGINE_H
