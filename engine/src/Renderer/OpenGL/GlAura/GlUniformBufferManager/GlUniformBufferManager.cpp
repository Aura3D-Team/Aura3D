#include "aura/Renderer/OpenGL/GlAura/GlUniformBufferManager/GlUniformBufferManager.h"

#include <glm/gtc/type_ptr.hpp>

namespace aura3d {
namespace gl {

GlUniformBufferManager::GlUniformBufferManager() = default;
GlUniformBufferManager::~GlUniformBufferManager() { cleanup(); }

void GlUniformBufferManager::create(GLuint shaderProgram)
{
    _uboSize = 3 * sizeof(glm::mat4);

    glGenBuffers(1, &_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, _ubo);
    glBufferData(GL_UNIFORM_BUFFER, _uboSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    GLuint blockIdx = glGetUniformBlockIndex(shaderProgram, "UniformBufferObject");
    if (blockIdx != GL_INVALID_INDEX) {
        glUniformBlockBinding(shaderProgram, blockIdx, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, _ubo);
    }

    // LightUBO is laid out for std140, so it uploads as one contiguous block.
    glGenBuffers(1, &_lightUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, _lightUbo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(gfx::LightUBO), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    GLuint lightIdx = glGetUniformBlockIndex(shaderProgram, "LightBlock");
    if (lightIdx != GL_INVALID_INDEX)
    {
        glUniformBlockBinding(shaderProgram, lightIdx, 1);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, _lightUbo);
    }

    //! Creation left the general binding point empty; start the cache there.
    _boundUniformBuffer = 0;
}

void GlUniformBufferManager::bindUniformBuffer(GLuint buffer)
{
    if (_boundUniformBuffer == buffer)
        return;

    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    _boundUniformBuffer = buffer;
}

void GlUniformBufferManager::updateLight(const gfx::LightUBO& light)
{
    if (!_lightUbo) return;

    bindUniformBuffer(_lightUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(gfx::LightUBO), &light);
}

void GlUniformBufferManager::update(const gfx::TransformUBO& ubo)
{
    if (!_ubo) return;

    /*
     * TransformUBO is three mat4s and nothing else, laid out back to back --
     * which is byte-for-byte the std140 block the shader declares. So the
     * whole thing goes up in one call, instead of the three consecutive
     * sub-range uploads (one per matrix) it used to take. Those three were
     * describing a contiguous range as if it were fragmented: same bytes, same
     * order, three times the driver-side bookkeeping. And this runs per
     * object, since setTransform() uploads on every draw.
     */
    static_assert(sizeof(gfx::TransformUBO) == 3 * sizeof(glm::mat4),
                  "TransformUBO must stay three tightly packed mat4s: the "
                  "single-call upload below writes it as one contiguous block, "
                  "and the shader's std140 layout expects exactly that.");
    static_assert(offsetof(gfx::TransformUBO, model) == 0);
    static_assert(offsetof(gfx::TransformUBO, view) == sizeof(glm::mat4));
    static_assert(offsetof(gfx::TransformUBO, proj) == 2 * sizeof(glm::mat4));

    bindUniformBuffer(_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(gfx::TransformUBO)), &ubo);
}

void GlUniformBufferManager::bind(GLuint shaderProgram)
{
    glUseProgram(shaderProgram);
}

void GlUniformBufferManager::cleanup()
{
    if (_ubo) 
        glDeleteBuffers(1, &_ubo);
    _ubo = 0;

    if (_lightUbo)
        glDeleteBuffers(1, &_lightUbo);
    _lightUbo = 0;

    //! Both names are gone, so whatever was cached about them is meaningless.
    _boundUniformBuffer = 0;
}

} // namespace gl
} // namespace aura3d
