#include "aura/Renderer/OpenGL/GlAura/GlTargetManager/GlTargetManager.h"

#include <array>
#include <stdexcept>

#include "aura/Renderer/OpenGL/EmbeddedGlsl.h"
#include "aura/Renderer/OpenGL/GlAura/GlShaderManager/GlShaderManager.h"

namespace aura3d
{
namespace gl
{

GlTargetManager::GlTargetManager() = default;
GlTargetManager::~GlTargetManager()
{
    cleanup();
}

void GlTargetManager::create()
{
    _resolveProgram = GlShaderManager::createProgram(gl_resolve_vert, gl_resolve_frag);
    glUseProgram(_resolveProgram);
    glUniform1i(glGetUniformLocation(_resolveProgram, "target"), 0);
    glGenVertexArrays(1, &_resolveVao);
}

void GlTargetManager::resize(glm::uvec2 size)
{
    if (_fbo && size == _size)
        return;
    if (!_fbo)
    {
        glGenFramebuffers(1, &_fbo);
        glGenTextures(1, &_color);
        glGenRenderbuffers(1, &_depth);
    }
    const auto width = static_cast<GLsizei>(size.x);
    const auto height = static_cast<GLsizei>(size.y);

    glBindTexture(GL_TEXTURE_2D, _color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindRenderbuffer(GL_RENDERBUFFER, _depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("GlTargetManager: sRGB render target is incomplete");
    //! Only a complete target counts as this size, so a failed resize is retried.
    _size = size;
    glViewport(0, 0, width, height);
}

void GlTargetManager::bind()
{
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
#ifndef AURA_GLES
    //! GLES encodes to an sRGB target unconditionally; desktop GL only when asked.
    glEnable(GL_FRAMEBUFFER_SRGB);
#endif
}

void GlTargetManager::resolve()
{
#ifdef AURA_GLES
    //! Tilers then neither store the target's depth nor load the window's old contents.
    const GLenum depthStencil = GL_DEPTH_STENCIL_ATTACHMENT;
    glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, &depthStencil);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    const std::array<GLenum, 3> window{GL_COLOR, GL_DEPTH, GL_STENCIL};
    glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLsizei>(window.size()), window.data());
#else
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    glUseProgram(_resolveProgram);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(_resolveVao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void GlTargetManager::cleanup() noexcept
{
    if (_fbo)
        glDeleteFramebuffers(1, &_fbo);
    if (_color)
        glDeleteTextures(1, &_color);
    if (_depth)
        glDeleteRenderbuffers(1, &_depth);
    if (_resolveProgram)
        glDeleteProgram(_resolveProgram);
    if (_resolveVao)
        glDeleteVertexArrays(1, &_resolveVao);
    _fbo = _color = _depth = _resolveProgram = _resolveVao = 0;
    _size = glm::uvec2{0};
}

} // namespace gl
} // namespace aura3d
