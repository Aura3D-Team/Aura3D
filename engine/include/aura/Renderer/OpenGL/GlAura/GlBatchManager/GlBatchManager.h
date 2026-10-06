#ifndef GLBATCHMANAGER_H
#define GLBATCHMANAGER_H

#pragma once

#include <span>
#include <vector>

#include <glad/glad.h>

#include "aura/Core/AuraCore.h"
#include "aura/Renderer/OpenGL/GlAura/GlTextureManager/GlTextureManager.h"

namespace aura3d
{
namespace gl
{

/// Unlit batches. Small ones are staged and drawn together, one upload and one draw per run of
/// the same texture and transform; large ones go straight to the driver.
class GlBatchManager
{
  public:
    GlBatchManager();
    ~GlBatchManager();

    /// @p textures must outlive this; @p fallback is sampled for an invalid texture.
    void create(GlTextureManager &textures, TextureHandle fallback);

    /// Both return whether they drew, which leaves the batch program, VAO and blend state current.
    [[nodiscard]] bool draw(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                            TextureHandle texture, const glm::mat4 &transform);
    bool flush();

    void cleanup() noexcept;

  private:
    struct Run
    {
        glm::mat4 transform;
        TextureHandle texture;
        u32 firstIndex;
        u32 indexCount;
    };

    void submit(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices, std::span<const Run> runs);

    GlTextureManager *_textures = nullptr;
    TextureHandle _fallback;
    GLuint _program = 0;
    GLint _transformLoc = -1;
    GLint _coverageLoc = -1;
    //! Last coverageOnly value given to the program; -1 before the first.
    GLint _coverage = -1;
    GLuint _vao = 0;
    GLuint _vbo = 0;
    GLuint _ebo = 0;
    GLsizeiptr _vboBytes = 0;
    GLsizeiptr _eboBytes = 0;

    std::vector<gfx::BatchVertex> _vertices;
    //! Rebased to the staged vertices, so one set of attribute pointers serves every run.
    std::vector<u32> _indices;
    std::vector<Run> _runs;
};

} // namespace gl
} // namespace aura3d

#endif // GLBATCHMANAGER_H
