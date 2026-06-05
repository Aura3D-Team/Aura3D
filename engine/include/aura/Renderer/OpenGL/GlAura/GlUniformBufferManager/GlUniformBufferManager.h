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

    void create(GLuint shaderProgram);
    void update(const gfx::TransformUBO& ubo);
    void bind(GLuint shaderProgram);
    void cleanup();

private:
    GLuint _ubo = 0;
    GLuint _uboSize = 0;
};

} // namespace gl
} // namespace aura3d

#endif // GLUNIFORMBUFFERMANAGER_H
