#ifndef AURA_RESOURCE_MANAGER_H
#define AURA_RESOURCE_MANAGER_H

#pragma once

#include <string>
#include <unordered_map>

#include "aura/Renderer/IRenderer.h"

namespace aura3d {

/**
 * @class ResourceManager
 * @brief Path-keyed cache in front of the renderer's asset entry points.
 *
 * Loading the same file twice returns the same handle instead of uploading a
 * second copy to the GPU. Handles stay valid for as long as the renderer that
 * produced them.
 *
 * @note Not thread-safe; call from the thread that owns the renderer.
 */
class ResourceManager {
public:
    explicit ResourceManager(IRenderer* renderer);

    //! Loads (or returns the cached) texture for @p path. Falls back to the
    //! checkerboard when the file is unusable, exactly like the renderer does.
    TextureHandle loadTexture(const std::string& path);

    //! Loads (or returns the cached) mesh for @p path. Falls back to a cube
    //! when the file is unusable.
    MeshHandle loadMesh(const std::string& path);

    /**
     * @brief Forgets every cached handle.
     *
     * Only drops the cache: the GPU resources belong to the renderer and are
     * released when it is cleaned up. Call this after switching backends, since
     * handles from the old renderer no longer mean anything to the new one.
     */
    void unloadAll();

    //! Repoints the cache at @p renderer and drops stale handles.
    void setRenderer(IRenderer* renderer);

    size_t cachedTextureCount() const { return _textureCache.size(); }
    size_t cachedMeshCount()    const { return _meshCache.size(); }

private:
    std::unordered_map<std::string, TextureHandle> _textureCache;
    std::unordered_map<std::string, MeshHandle> _meshCache;
    IRenderer* _renderer;
};

} // namespace aura3d

#endif // AURA_RESOURCE_MANAGER_H
