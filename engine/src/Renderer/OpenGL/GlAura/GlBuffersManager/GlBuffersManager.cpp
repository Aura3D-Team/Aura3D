#include "aura/Renderer/OpenGL/GlAura/GlBuffersManager/GlBuffersManager.h"

#include <glad/glad.h>

namespace aura3d {
namespace gl {

GlBuffersManager::GlBuffersManager() = default;

GlBuffersManager::~GlBuffersManager()
{
    cleanup();
}

void GlBuffersManager::cleanup()
{
    for (GLBuffers& buffers : _glBuffers) 
    {
        if (buffers._VAO) 
            glDeleteVertexArrays(1, &buffers._VAO);
        
        if (buffers._VBO)
            glDeleteBuffers(1, &buffers._VBO);
        
        if (buffers._EBO)
            glDeleteBuffers(1, &buffers._EBO);
        
        if (buffers._UBO)
            glDeleteBuffers(1, &buffers._UBO);
        
        buffers = {};
    }
    _glBuffers.clear();
}

} // namespace gl
} // namespace aura3d
