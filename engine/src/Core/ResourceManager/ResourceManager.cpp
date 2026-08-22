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
    if (_audio)
    {
        for (const auto& [path, handle] : _soundCache)
            _audio->unloadClip(handle);
    }

    _soundCache.clear();
}

void ResourceManager::setRenderer(IRenderer* renderer)
{
    unloadAll();
    _renderer = renderer;
}

void ResourceManager::setAudioEngine(AudioEngine* audio)
{
    unloadSounds();
    _audio = audio;
}

} // namespace aura3d
