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
     * @brief Benchmark/debug instrumentation, or nullptr in a normal build.
     *
     * Non-null only when the engine was compiled with AURA_ENABLE_DEBUG_MODE.
     * Unlike audio(), a null check is therefore the normal case rather than a
     * defensive one -- which is exactly why this returns a pointer and is
     * declared unconditionally: application code writes
     *
     * @code
     * if (aura3d::DebugMode* debug = engine.debugMode())
     *     debug->update(dt);
     * @endcode
     *
     * once, and it compiles and does the right thing in either build.
     *
     * Call DebugMode::update() once per frame; the final report is written by
     * this Engine's destructor whether or not anything asked for it.
     */
    [[nodiscard]] aura3d::DebugMode* debugMode() const noexcept
    {
#ifdef AURA_ENABLE_DEBUG_MODE
        return _debugMode.get();
#else
        return nullptr;
#endif
    }

    /**
     * @brief Runs @p body over [0, @p itemCount) in parallel, returning once
     *        every band has finished.
     *
     * The engine's shared parallel-for, for CPU work that never reaches a
     * renderer -- generating a texture procedurally, transforming geometry,
     * evaluating a field. Sized from @c graphics.cpu_threads, the same setting
     * that sizes the software rasteriser and the Vulkan command recorder, so an
     * application does not start a second set of threads beside them.
     *
     * See aura3d::JobSystem::dispatch() for the band contract.
     */
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
     * The request is resolved through RendererFactory, so an unavailable
     * backend degrades rather than failing. Every handle previously issued by
     * the old renderer becomes invalid and the asset cache is dropped: reload
     * assets through resources() afterwards.
     *
     * No-op when @p choice already resolves to the active backend.
     */
    void switchBackend(aura3d::RendererChoice choice);

private:
    void _configureWindow();
    void _createRenderer();
    void _createAudio();

    /**
     * @brief Builds the debug subsystem and installs it as the frame observer.
     *
     * A no-op without AURA_ENABLE_DEBUG_MODE. Declared unconditionally so the
     * constructor's call site does not need a guard of its own.
     */
    void _createDebugMode();

    /**
     * @brief Brings up @p choice through RendererFactory and adopts the result.
     *
     * The one path by which this Engine comes to own a renderer: construction,
     * recording the backend actually resolved to, initialization, and rebinding
     * the asset cache. Both startup and switchBackend() go through it so the
     * two cannot drift apart.
     */
    void _adoptRenderer(aura3d::RendererChoice choice);

private:
    std::unique_ptr<aura3d::JobSystem> _jobs;
    std::unique_ptr<aura3d::IRenderer> _renderer;
    std::unique_ptr<aura3d::ResourceManager> _resources;
    std::unique_ptr<aura3d::AudioEngine> _audio;
#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * Gated rather than merely left null in a normal build: the member itself
     * is the footprint AURA_ENABLE_DEBUG_MODE promises not to have. Declared
     * after _renderer so member destruction alone would already tear it down
     * first -- the destructor makes that explicit anyway, since the final
     * report reads the renderer's GPU counters.
     */
    std::unique_ptr<aura3d::DebugMode> _debugMode;
#endif
    wma::WindowDetails _windowDetails;
    aura3d::RendererChoice _rendererChoice = aura3d::RendererChoice::SOFTWARE;
};

#endif // ENGINE_H
