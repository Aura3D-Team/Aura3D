#ifndef GLUNIFORMBUFFERMANAGER_H
#define GLUNIFORMBUFFERMANAGER_H

#pragma once

#include <glad/glad.h>

#include "aura/Core/AuraCore.h"

namespace aura3d
{
namespace gl
{

/// The camera (binding 0), lighting (binding 1) and material (binding 2) blocks of the scene programs.
class GlUniformBufferManager
{
  public:
    GlUniformBufferManager();
    ~GlUniformBufferManager();

    void create(GLuint shaderProgram);
    //! Points another program's Camera, LightingBlock and MaterialBlock at the buffers create() made.
    static void bindBlocks(GLuint shaderProgram);

    /// Uploads view and projection, only when they differ from the last upload.
    void updateCamera(const gfx::TransformUBO &ubo);
    void updateLight(const gfx::LightingUBO &lighting);
    void updateMaterial(const gfx::MaterialUniforms &material);

    void cleanup();

  private:
    GLuint _cameraUbo = 0;
    GLuint _lightUbo = 0;
    GLuint _materialUbo = 0;
    glm::mat4 _view{0.0f};
    glm::mat4 _proj{0.0f};
    bool _cameraValid = false;
};

} // namespace gl
} // namespace aura3d

#endif // GLUNIFORMBUFFERMANAGER_H
