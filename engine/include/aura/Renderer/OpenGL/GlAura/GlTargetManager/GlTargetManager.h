#ifndef GLTARGETMANAGER_H
#define GLTARGETMANAGER_H

#pragma once

#include <glad/glad.h>

#include "aura/Core/AuraCore.h"

namespace aura3d
{
namespace gl
{

/// The pass target: an sRGB color texture and a depth renderbuffer, so blending is linear and
/// storage encoded on every GL flavour, as on Vulkan's sRGB swapchain. WebGL has no sRGB
/// default framebuffer, and drivers misreport desktop ones, so resolve() encodes explicitly.
class GlTargetManager
{
  public:
    GlTargetManager();
    ~GlTargetManager();

    void create();
    /// Sized in framebuffer pixels; reallocates only when the size changes. Leaves the target
    /// bound and, like resolve(), changes the bound 2D texture.
    void resize(glm::uvec2 size);
    /// Makes the target what the pass draws into.
    void bind();
    /// Encodes the target onto the window's framebuffer; reads stay on the target.
    void resolve();

    [[nodiscard]] glm::uvec2 size() const noexcept
    {
        return _size;
    }

    void cleanup() noexcept;

  private:
    GLuint _fbo = 0;
    GLuint _color = 0;
    GLuint _depth = 0;
    glm::uvec2 _size{0};
    GLuint _resolveProgram = 0;
    //! Core profiles draw nothing without a VAO bound, even one with no attributes.
    GLuint _resolveVao = 0;
};

} // namespace gl
} // namespace aura3d

#endif // GLTARGETMANAGER_H
