#ifndef GLUNIFORMBUFFERMANAGER_H
#define GLUNIFORMBUFFERMANAGER_H

#pragma once

#include <glad/glad.h>

#include "aura/Core/AuraCore.h"

namespace aura3d {
namespace gl {

class GlUniformBufferManager {
public:
    GlUniformBufferManager();
    ~GlUniformBufferManager();

    //! Allocates the transform (binding 0) and light (binding 1) blocks and
    //! wires them to @p shaderProgram.
    void create(GLuint shaderProgram);

    void update(const gfx::TransformUBO& ubo);

    //! Uploads the directional light consumed by the fragment stage.
    void updateLight(const gfx::LightUBO& light);

    void bind(GLuint shaderProgram);
    void cleanup();

private:
    GLuint _ubo = 0;
    GLuint _uboSize = 0;
    GLuint _lightUbo = 0;
};

} // namespace gl
} // namespace aura3d

#endif // GLUNIFORMBUFFERMANAGER_H
