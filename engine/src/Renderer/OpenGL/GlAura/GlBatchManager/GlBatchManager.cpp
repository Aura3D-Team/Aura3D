#include "aura/Renderer/OpenGL/GlAura/GlBatchManager/GlBatchManager.h"

#include <algorithm>
#include <cstring>

#include <glm/gtc/type_ptr.hpp>

#include "aura/Renderer/OpenGL/EmbeddedGlsl.h"
#include "aura/Renderer/OpenGL/GlAura/GlShaderManager/GlShaderManager.h"

namespace aura3d
{
namespace gl
{

namespace
{

//! From this size, copying into staging costs more than the draw call a merge would save.
constexpr usize kDirectBytes = usize{64} << 10;

//! Orphans the store bound at @p target, so the copy never waits on draws still reading it.
//! The store keeps the largest size seen, which spares the driver a reallocation per size.
void orphanUpload(GLenum target, GLsizeiptr &capacity, std::span<const std::byte> bytes)
{
    const auto size = static_cast<GLsizeiptr>(bytes.size());
    capacity = std::max(capacity, size);
    glBufferData(target, capacity, nullptr, GL_STREAM_DRAW);
#ifndef __EMSCRIPTEN__
    //! Writing into the store itself saves the driver's staging copy; WebGL2 cannot map.
    if (void *store = glMapBufferRange(target, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT))
    {
        std::memcpy(store, bytes.data(), bytes.size());
        if (glUnmapBuffer(target))
            return;
    }
#endif
    glBufferSubData(target, 0, size, bytes.data());
}

} // namespace

GlBatchManager::GlBatchManager() = default;
GlBatchManager::~GlBatchManager()
{
    cleanup();
}

void GlBatchManager::create(GlTextureManager &textures, TextureHandle fallback)
{
    _textures = &textures;
    _fallback = fallback;
    _program = GlShaderManager::createProgram(gl_batch_vert, gl_batch_frag);
    _transformLoc = glGetUniformLocation(_program, "uTransform");
    _coverageLoc = glGetUniformLocation(_program, "coverageOnly");
    glUseProgram(_program);
    glUniform1i(glGetUniformLocation(_program, "textureSampler"), 0);

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ebo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);

    // OpenGL requires byte offsets encoded as pointers when a VBO is bound.
    // NOLINTBEGIN(performance-no-int-to-ptr)
    constexpr auto stride = static_cast<GLsizei>(sizeof(gfx::BatchVertex));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(gfx::BatchVertex, pos)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void *>(offsetof(gfx::BatchVertex, texCoord)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void *>(offsetof(gfx::BatchVertex, color)));
    // NOLINTEND(performance-no-int-to-ptr)
    for (GLuint attribute : {0u, 1u, 2u})
        glEnableVertexAttribArray(attribute);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
}

bool GlBatchManager::draw(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                          TextureHandle texture, const glm::mat4 &transform)
{
    const auto count = static_cast<u32>(indices.size());
    if (vertices.size_bytes() + indices.size_bytes() >= kDirectBytes)
    {
        flush();
        const Run run{transform, texture, 0, count};
        submit(vertices, indices, std::span(&run, 1));
        return true;
    }

    const auto base = static_cast<u32>(_vertices.size());
    const auto first = static_cast<u32>(_indices.size());
    _vertices.insert(_vertices.end(), vertices.begin(), vertices.end());
    _indices.resize(first + count);
    std::ranges::transform(indices, _indices.begin() + first,
                           [base](u32 index)
                           {
                               return base + index;
                           });

    if (!_runs.empty() && _runs.back().texture == texture && _runs.back().transform == transform)
        _runs.back().indexCount += count;
    else
        _runs.push_back({transform, texture, first, count});
    return false;
}

bool GlBatchManager::flush()
{
    if (_runs.empty())
        return false;
    submit(_vertices, _indices, _runs);
    _vertices.clear();
    _indices.clear();
    _runs.clear();
    return true;
}

void GlBatchManager::submit(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                            std::span<const Run> runs)
{
    glUseProgram(_program);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    orphanUpload(GL_ARRAY_BUFFER, _vboBytes, std::as_bytes(vertices));
    orphanUpload(GL_ELEMENT_ARRAY_BUFFER, _eboBytes, std::as_bytes(indices));

    const glm::mat4 *uploaded = nullptr;
    for (const Run &run : runs)
    {
        if (!uploaded || *uploaded != run.transform)
        {
            glUniformMatrix4fv(_transformLoc, 1, GL_FALSE, glm::value_ptr(run.transform));
            uploaded = &run.transform;
        }
        const GlTextureData *data = _textures->bind(run.texture, 0);
        if (!data)
            data = _textures->bind(_fallback, 0);
        const GLint coverage = data && data->coverageOnly ? 1 : 0;
        if (_coverage != coverage)
        {
            glUniform1i(_coverageLoc, coverage);
            _coverage = coverage;
        }
        // NOLINTBEGIN(performance-no-int-to-ptr)
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(run.indexCount), GL_UNSIGNED_INT,
                       reinterpret_cast<void *>(usize{run.firstIndex} * sizeof(u32)));
        // NOLINTEND(performance-no-int-to-ptr)
    }
}

void GlBatchManager::cleanup() noexcept
{
    if (_program)
        glDeleteProgram(_program);
    if (_vao)
        glDeleteVertexArrays(1, &_vao);
    for (GLuint *buffer : {&_vbo, &_ebo})
        if (*buffer)
            glDeleteBuffers(1, buffer);
    _program = _vao = _vbo = _ebo = 0;
    _vboBytes = _eboBytes = 0;
    _coverage = -1;
    _textures = nullptr;
    _vertices.clear();
    _indices.clear();
    _runs.clear();
}

} // namespace gl
} // namespace aura3d
