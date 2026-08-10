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
    /**
     * @brief Makes @p buffer current on GL_UNIFORM_BUFFER, skipping the call
     *        when it already is.
     *
     * The uploads used to bind their buffer and then bind 0 again, so a frame's
     * worth of per-object transform updates re-bound the same buffer once per
     * draw. Nothing here relies on the target being left empty -- the blocks
     * are wired to their binding points once, in create(), with
     * glBindBufferBase, which is independent of this general binding.
     */
    void bindUniformBuffer(GLuint buffer);

    GLuint _ubo = 0;
    GLuint _uboSize = 0;
    GLuint _lightUbo = 0;
    //! Buffer currently bound to GL_UNIFORM_BUFFER; 0 means none.
    GLuint _boundUniformBuffer = 0;
};

} // namespace gl
} // namespace aura3d

#endif // GLUNIFORMBUFFERMANAGER_H
