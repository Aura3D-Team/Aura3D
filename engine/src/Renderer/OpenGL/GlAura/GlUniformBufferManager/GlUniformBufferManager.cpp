#include "aura/Renderer/OpenGL/GlAura/GlUniformBufferManager/GlUniformBufferManager.h"

namespace aura3d
{
namespace gl
{

namespace
{

constexpr GLuint kCameraBinding = 0;
constexpr GLuint kLightBinding = 1;

//! Allocates a uniform buffer of @p size bytes and wires @p block of @p program to it.
GLuint createBlock(GLuint program, const char *block, GLuint binding, GLsizeiptr size)
{
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);

    const GLuint index = glGetUniformBlockIndex(program, block);
    if (index != GL_INVALID_INDEX)
        glUniformBlockBinding(program, index, binding);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
    return buffer;
}

} // namespace

GlUniformBufferManager::GlUniformBufferManager() = default;
GlUniformBufferManager::~GlUniformBufferManager()
{
    cleanup();
}

void GlUniformBufferManager::create(GLuint shaderProgram)
{
    //! Std140 packs the two mat4s back to back, which is how TransformUBO lays out view and proj.
    static_assert(offsetof(gfx::TransformUBO, proj) == offsetof(gfx::TransformUBO, view) + sizeof(glm::mat4));
    _cameraUbo = createBlock(shaderProgram, "Camera", kCameraBinding, 2 * sizeof(glm::mat4));
    _lightUbo = createBlock(shaderProgram, "LightBlock", kLightBinding, sizeof(gfx::LightUBO));
    _cameraValid = false;
}

void GlUniformBufferManager::updateCamera(const gfx::TransformUBO &ubo)
{
    if (!_cameraUbo || (_cameraValid && ubo.view == _view && ubo.proj == _proj))
        return;

    //! The binding point is indexed, so the general binding is free to be reused by any upload.
    glBindBuffer(GL_UNIFORM_BUFFER, _cameraUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 2 * sizeof(glm::mat4), &ubo.view);
    _view = ubo.view;
    _proj = ubo.proj;
    _cameraValid = true;
}

void GlUniformBufferManager::updateLight(const gfx::LightUBO &light)
{
    if (!_lightUbo)
        return;

    glBindBuffer(GL_UNIFORM_BUFFER, _lightUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(gfx::LightUBO), &light);
}

void GlUniformBufferManager::cleanup()
{
    for (GLuint *buffer : {&_cameraUbo, &_lightUbo})
        if (*buffer)
            glDeleteBuffers(1, buffer);
    _cameraUbo = _lightUbo = 0;
    _cameraValid = false;
}

} // namespace gl
} // namespace aura3d
