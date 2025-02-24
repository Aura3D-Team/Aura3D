#ifndef GLBUFFERSMANAGER_H
#define GLBUFFERSMANAGER_H

#pragma once

#include <GL/gl.h>

namespace aura3d {

class GlBuffersManager
{
public:
    GlBuffersManager();
    ~GlBuffersManager();


private:
    uint32_t _vbo;
};

}

#endif // GLBUFFERSMANAGER_H
