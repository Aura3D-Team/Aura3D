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
 *        entry points. Loading the same file twice returns the same handle.
 *
 * Textures/meshes (renderer-owned) are dropped by unloadAll() on a backend
 * switch; sounds (audio-engine-owned, survives that switch) use unloadSounds()
 * instead.
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

    /// Drops the texture/mesh cache (not the GPU resources themselves, which
    /// the renderer releases on cleanup). Call after switching backends.
    /// Leaves sounds alone; see unloadSounds().
    void unloadAll();

    /// Drops the sound cache and releases the clips from the audio engine,
    /// stopping any voice still playing one. Unlike unloadAll(), this actually
    /// frees the underlying PCM.
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
