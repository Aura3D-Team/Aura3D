#ifndef GLSHADERMANAGER_H
#define GLSHADERMANAGER_H

#pragma once

#include <string>

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
};

} // namespace gl
} // namespace aura3d

#endif // GLBUFFERSMANAGER_H
