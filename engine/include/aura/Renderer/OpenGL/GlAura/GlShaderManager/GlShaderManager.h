#ifndef GLSHADERMANAGER_H
#define GLSHADERMANAGER_H

#pragma once

#include <string>

#include <glad/glad.h>

namespace aura3d
{
namespace gl
{

class GlShaderManager
{
  public:
    GlShaderManager();
    ~GlShaderManager();

    static std::string readShaderSource_GL(const std::string &filename);

    //! Compiles and links a vertex/fragment pair. Throws with the driver's log on failure.
    [[nodiscard]] static GLuint createProgram(const char *vertexSource, const char *fragmentSource);
};

} // namespace gl
} // namespace aura3d

#endif // GLBUFFERSMANAGER_H
