#ifndef ENGINE_H
#define ENGINE_H

#include <memory>
#include <string>

#include "aura/Core/AudioEngine/AudioEngine.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/DebugMode/DebugMode.h"
#include "aura/Core/JobSystem/JobSystem.h"
#include "aura/Core/ResourceManager/ResourceManager.h"
#include "aura/Renderer/IRenderer.h"

/**
 * @class Engine
 * @brief Owns the configuration, the renderer, the audio engine and the asset cache.
 */
class Engine
{
public:
    explicit Engine(const std::string& configPath);
    ~Engine();

    aura3d::IRenderer* getRenderer() const { return _renderer.get(); }
    aura3d::RendererChoice getBackend() const { return _rendererChoice; }

    //! Engine-wide configuration. Use the typed accessors rather than reaching
    //! for the raw JSON.
    const aura3d::AuraSettings* getSettings() const;

    //! Path-keyed texture/mesh/sound cache bound to the current renderer.
    aura3d::ResourceManager* resources() { return _resources.get(); }

    /**
     * @brief Clip playback, mixing and 3D positional audio.
     *
     * Always non-null: a machine with no sound hardware gets a silent device
     * rather than nothing at all, so callers never need to null-check this.
     * Call AudioEngine::update() once per frame.
     */
    aura3d::AudioEngine* audio() const { return _audio.get(); }

    /**
     * @brief Benchmark/debug instrumentation, or nullptr outside an
     *        AURA_ENABLE_DEBUG_MODE build.
     *
     * Declared unconditionally (unlike the member behind it) so callers can
     * write `if (auto* debug = engine.debugMode()) debug->update(dt);` once
     * and have it compile and behave correctly in either build. The final
     * report is written by ~Engine() regardless of whether anything asked.
     */
    [[nodiscard]] aura3d::DebugMode* debugMode() const noexcept
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
    void dispatch(i32 itemCount, const aura3d::JobSystem::BandBody& body) const
    {
        _jobs->dispatch(itemCount, body);
    }

    /// The same facility as a reference, for subsystems that take one without
    /// depending on the whole Engine.
    [[nodiscard]] const aura3d::JobSystem& jobs() const noexcept { return *_jobs; }

    /**
     * @brief Tears the current renderer down and brings up @p choice instead.
     *
     * Resolved through RendererFactory, so an unavailable backend degrades
     * rather than failing. Drops the asset cache -- reload through resources()
     * afterwards. No-op if @p choice already resolves to the active backend.
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

    /// Brings up @p choice through RendererFactory and adopts the result.
    /// The one path to owning a renderer, shared by startup and
    /// switchBackend() so the two cannot drift apart.
    void _adoptRenderer(aura3d::RendererChoice choice);

private:
    std::unique_ptr<aura3d::JobSystem> _jobs;
    std::unique_ptr<aura3d::IRenderer> _renderer;
    std::unique_ptr<aura3d::ResourceManager> _resources;
    std::unique_ptr<aura3d::AudioEngine> _audio;
#ifdef AURA_ENABLE_DEBUG_MODE
    //! Declared after _renderer: the final report reads the renderer's GPU
    //! counters, so it must be torn down first.
    std::unique_ptr<aura3d::DebugMode> _debugMode;
#endif
    wma::WindowDetails _windowDetails;
    aura3d::RendererChoice _rendererChoice = aura3d::RendererChoice::SOFTWARE;
};

#endif // ENGINE_H
