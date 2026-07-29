#ifndef GLBUFFERSMANAGER_H
#define GLBUFFERSMANAGER_H

#pragma once

#include <vector>
#include "aura/aura.h"

namespace aura3d {
namespace gl {

struct GLBuffers {
    u32 _VAO = 0; // Vertex Array Object
    u32 _VBO = 0; // Vertex Buffer Object
    u32 _EBO = 0; // Element Buffer Object
    u32 _UBO = 0; // Uniform Buffer Object
};

/**
 * @class GlBuffersManager
 * @brief Owns grouped VAO/VBO/EBO/UBO sets and releases them on destruction.
 *
 * @note The live OpenGL path uses the dedicated GlVertexBufferManager /
 *       GlIndexBufferManager / GlUniformBufferManager instead. This type is
 *       retained for the grouped-allocation use case; it is safe to destroy,
 *       and deletes any GL names it holds.
 */
class GlBuffersManager
{
public:
    GlBuffersManager();
    ~GlBuffersManager();

    /// Deletes every GL object held and empties the registry.
    void cleanup();

private:
    std::vector<GLBuffers> _glBuffers;
};

}
}

#endif // GLBUFFERSMANAGER_H
