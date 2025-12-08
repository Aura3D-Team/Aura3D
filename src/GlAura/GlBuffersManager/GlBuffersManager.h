#ifndef GLBUFFERSMANAGER_H
#define GLBUFFERSMANAGER_H

#pragma once

#include <GL/gl.h>

#include "aura.hpp"

namespace aura3d {
namespace gl {

struct GLBuffers {
    u32 _VAO = 0; // Vertex Array Object
    u32 _VBO = 0; // Vertex Buffer Object
    u32 _EBO = 0; // Element Buffer Object
    u32 _UBO = 0; // Uniform Buffer Object
};

class GlBuffersManager
{
public:
    GlBuffersManager();
    ~GlBuffersManager();

    // createGlBuffer();

private:
    std::vector<GLBuffers> _glBuffers;
};

}
}

#endif // GLBUFFERSMANAGER_H
