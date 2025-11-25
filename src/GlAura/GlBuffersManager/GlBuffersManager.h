#ifndef GLBUFFERSMANAGER_H
#define GLBUFFERSMANAGER_H

#pragma once

#include <GL/gl.h>

#include "aura.hpp"

namespace aura3d {
namespace gl {

class GlBuffersManager
{
public:
    GlBuffersManager();
    ~GlBuffersManager();


private:
    u32 _vbo;
};

}
}

#endif // GLBUFFERSMANAGER_H
