#ifndef GLTEXTUREMANAGER_H
#define GLTEXTUREMANAGER_H

#pragma once

#include <glad/glad.h>
#include <unordered_map>
#include <string>

#include "aura/Renderer/RenderHandles.h"

namespace aura3d {
namespace gl {

struct GlTextureData {
    GLuint texture = 0;
    u32 width = 0;
    u32 height = 0;
};

class GlTextureManager {
public:
    GlTextureManager();
    ~GlTextureManager();

    TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255);

    void bind(TextureHandle handle, GLuint unit = 0);
    GlTextureData* get(TextureHandle handle);
    void cleanup();

private:
    std::unordered_map<TextureHandle, GlTextureData> _textures;
    TextureHandle _nextHandle = 1;
};

} // namespace gl
} // namespace aura3d

#endif // GLTEXTUREMANAGER_H