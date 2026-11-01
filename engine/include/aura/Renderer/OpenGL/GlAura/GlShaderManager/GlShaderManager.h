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
#ifndef AURA_GLES
    //! GL 4.3; throws with the driver's log when it does not compile or link.
    [[nodiscard]] static GLuint createComputeProgram(const char *source);
#endif
};

} // namespace gl
} // namespace aura3d

#endif // GLBUFFERSMANAGER_H
