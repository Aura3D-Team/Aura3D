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
}

void GlUniformBufferManager::updateLight(const gfx::LightUBO& light)
{
    if (!_lightUbo) return;

    glBindBuffer(GL_UNIFORM_BUFFER, _lightUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(gfx::LightUBO), &light);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void GlUniformBufferManager::update(const gfx::TransformUBO& ubo)
{
    glBindBuffer(GL_UNIFORM_BUFFER, _ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0,             sizeof(glm::mat4), glm::value_ptr(ubo.model));
    glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(ubo.view));
    glBufferSubData(GL_UNIFORM_BUFFER, 2*sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(ubo.proj));
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
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
}

} // namespace gl
} // namespace aura3d
