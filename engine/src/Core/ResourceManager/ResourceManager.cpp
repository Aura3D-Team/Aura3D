#include "aura/Core/ResourceManager/ResourceManager.h"

#include "aura/Core/MeshLoader/MeshLoader.h"

namespace aura3d {

ResourceManager::ResourceManager(IRenderer* renderer, AudioEngine* audio)
    : _renderer(renderer)
    , _audio(audio)
{
}

TextureHandle ResourceManager::loadTexture(const std::string& path)
{
    if (!_renderer) 
    {
        INK_ERROR << "ResourceManager: no renderer bound; cannot load " << path;
        return {};
    }

    auto it = _textureCache.find(path);
    if (it != _textureCache.end())
        return it->second;

    // createTextureFromFile already substitutes the checkerboard for a missing
    // file, so the result is always usable and always worth caching: a broken
    // path should not be retried on every frame.
    const TextureHandle handle = _renderer->createTextureFromFile(path);
    _textureCache.emplace(path, handle);

    INK_DEBUG << "ResourceManager: cached texture '" << path << "' as handle " << handle;
    return handle;
}

MeshHandle ResourceManager::loadMesh(const std::string& path)
{
    if (!_renderer) {
        INK_ERROR << "ResourceManager: no renderer bound; cannot load " << path;
        return {};
    }

    auto it = _meshCache.find(path);
    if (it != _meshCache.end()) {
        return it->second;
    }

    // loadOBJ falls back to a cube rather than failing, mirroring the texture path.
    //! Moved into the renderer: the cache stores the resulting handle, never
    //! the geometry, so nothing here needs the arrays after the upload.
    gfx::Mesh3D mesh = MeshLoader::loadOBJ(path);
    const MeshHandle handle = _renderer->createMesh(std::move(mesh));
    _meshCache.emplace(path, handle);

    INK_DEBUG << "ResourceManager: cached mesh '" << path << "' as handle " << handle;
    return handle;
}

AudioClipHandle ResourceManager::loadSound(const std::string& path, AudioClipMode mode)
{
    if (!_audio)
    {
        INK_ERROR << "ResourceManager: no audio engine bound; cannot load " << path;
        return {};
    }

    auto it = _soundCache.find(path);
    if (it != _soundCache.end())
        return it->second;

    // loadClip substitutes silence for a missing or undecodable file, so the
    // result is always usable and always worth caching -- re-decoding a broken
    // path every time a sound effect fires would be far worse than the silence.
    const AudioClipHandle handle = _audio->loadClip(path, mode);
    _soundCache.emplace(path, handle);

    INK_DEBUG << "ResourceManager: cached sound '" << path << "' as handle " << handle;
    return handle;
}

void ResourceManager::unloadAll()
{
    _textureCache.clear();
    _meshCache.clear();
}

void ResourceManager::unloadSounds()
{
    // Unlike the renderer caches above, this releases the underlying data as
    // well: the audio engine owns clips outright and has no cleanup step of its
    // own to reclaim them, so dropping only the cache would strand the PCM.
    if (_audio)
    {
        for (const auto& [path, handle] : _soundCache)
            _audio->unloadClip(handle);
    }

    _soundCache.clear();
}

void ResourceManager::setRenderer(IRenderer* renderer)
{
    // Handles are only meaningful to the renderer that issued them.
    unloadAll();
    _renderer = renderer;
}

void ResourceManager::setAudioEngine(AudioEngine* audio)
{
    // Clip handles belong to the engine that issued them. Released through the
    // outgoing engine while it is still around, not merely forgotten.
    unloadSounds();
    _audio = audio;
}

} // namespace aura3d
