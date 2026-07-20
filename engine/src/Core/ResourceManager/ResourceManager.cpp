#include "aura/Core/ResourceManager/ResourceManager.h"

#include "aura/Core/MeshLoader/MeshLoader.h"

namespace aura3d {

ResourceManager::ResourceManager(IRenderer* renderer)
    : _renderer(renderer)
{
}

TextureHandle ResourceManager::loadTexture(const std::string& path)
{
    if (!_renderer) 
    {
        INK_ERROR << "ResourceManager: no renderer bound; cannot load " << path;
        return INVALID_HANDLE;
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
        return INVALID_HANDLE;
    }

    auto it = _meshCache.find(path);
    if (it != _meshCache.end()) {
        return it->second;
    }

    // loadOBJ falls back to a cube rather than failing, mirroring the texture path.
    const gfx::Mesh3D mesh = MeshLoader::loadOBJ(path);
    const MeshHandle handle = _renderer->createMesh(mesh);
    _meshCache.emplace(path, handle);

    INK_DEBUG << "ResourceManager: cached mesh '" << path << "' as handle " << handle;
    return handle;
}

void ResourceManager::unloadAll()
{
    _textureCache.clear();
    _meshCache.clear();
}

void ResourceManager::setRenderer(IRenderer* renderer)
{
    // Handles are only meaningful to the renderer that issued them.
    unloadAll();
    _renderer = renderer;
}

} // namespace aura3d
