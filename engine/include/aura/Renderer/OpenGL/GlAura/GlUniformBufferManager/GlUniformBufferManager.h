#ifndef GLUNIFORMBUFFERMANAGER_H
#define GLUNIFORMBUFFERMANAGER_H

#pragma once

#include <glad/glad.h>

#include "aura/Core/AuraCore.h"

namespace aura3d
{
namespace gl
{

/// The camera (binding 0) and light (binding 1) blocks of the scene program.
class GlUniformBufferManager
{
  public:
    GlUniformBufferManager();
    ~GlUniformBufferManager();

    void create(GLuint shaderProgram);

    /// Uploads view and projection, only when they differ from the last upload.
    void updateCamera(const gfx::TransformUBO &ubo);
    void updateLight(const gfx::LightUBO &light);

    void cleanup();

  private:
    GLuint _cameraUbo = 0;
    GLuint _lightUbo = 0;
    glm::mat4 _view{0.0f};
    glm::mat4 _proj{0.0f};
    bool _cameraValid = false;
};

} // namespace gl
} // namespace aura3d

#endif // GLUNIFORMBUFFERMANAGER_H
