#ifndef AURA_RESOURCE_MANAGER_H
#define AURA_RESOURCE_MANAGER_H

#pragma once

#include <string>
#include <unordered_map>

#include "aura/Core/AudioEngine/AudioEngine.h"
#include "aura/Renderer/IRenderer.h"

namespace aura3d {

/**
 * @class ResourceManager
 * @brief Path-keyed cache in front of the renderer's and audio engine's asset
 *        entry points.
 *
 * Loading the same file twice returns the same handle instead of uploading a
 * second copy to the GPU (or decoding a second copy of the same PCM). Handles
 * stay valid for as long as the subsystem that produced them.
 *
 * Textures and meshes belong to the renderer and are dropped by unloadAll()
 * when a backend switch invalidates them; sounds belong to the audio engine,
 * which survives that switch, so they are cached and released separately. See
 * unloadSounds().
 *
 * @note Not thread-safe; call from the thread that owns the renderer.
 */
class ResourceManager {
public:
    explicit ResourceManager(IRenderer* renderer, AudioEngine* audio = nullptr);

    //! Loads (or returns the cached) texture for @p path. Falls back to the
    //! checkerboard when the file is unusable, exactly like the renderer does.
    TextureHandle loadTexture(const std::string& path);

    //! Loads (or returns the cached) mesh for @p path. Falls back to a cube
    //! when the file is unusable.
    MeshHandle loadMesh(const std::string& path);

    /**
     * @brief Loads (or returns the cached) audio clip for @p path.
     *
     * Falls back to silence when the file is unusable, mirroring the texture
     * path's checkerboard. Pass AudioClipMode::Streaming for long-form audio
     * such as music.
     *
     * @note @p mode is only consulted the first time a path is loaded; a later
     *       call with a different mode returns the already-cached clip rather
     *       than decoding a second copy.
     */
    AudioClipHandle loadSound(const std::string& path,
                              AudioClipMode mode = AudioClipMode::Static);

    /**
     * @brief Forgets every cached renderer handle (textures and meshes).
     *
     * Only drops the cache: the GPU resources belong to the renderer and are
     * released when it is cleaned up. Call this after switching backends, since
     * handles from the old renderer no longer mean anything to the new one.
     *
     * Deliberately leaves sounds alone — they are owned by the audio engine,
     * which a backend switch does not touch. Use unloadSounds() for those.
     */
    void unloadAll();

    /**
     * @brief Forgets every cached sound and releases it from the audio engine.
     *
     * Unlike unloadAll() this really does free the underlying data: clips are
     * held by the audio engine for as long as something references them, so
     * dropping the cache alone would leak the PCM for the engine's lifetime.
     * Any voice still playing one of these clips is stopped.
     */
    void unloadSounds();

    //! Repoints the cache at @p renderer and drops stale texture/mesh handles.
    void setRenderer(IRenderer* renderer);

    //! Repoints the cache at @p audio and drops stale clip handles.
    void setAudioEngine(AudioEngine* audio);

    size_t cachedTextureCount() const { return _textureCache.size(); }
    size_t cachedMeshCount()    const { return _meshCache.size(); }
    size_t cachedSoundCount()   const { return _soundCache.size(); }

private:
    std::unordered_map<std::string, TextureHandle> _textureCache;
    std::unordered_map<std::string, MeshHandle> _meshCache;
    std::unordered_map<std::string, AudioClipHandle> _soundCache;
    IRenderer* _renderer;
    AudioEngine* _audio;
};

} // namespace aura3d

#endif // AURA_RESOURCE_MANAGER_H
