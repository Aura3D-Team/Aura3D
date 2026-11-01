#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/Renderer/OpenGL/GlAura/GlShaderManager/GlShaderManager.h"
#include "aura/aura.h"

namespace aura3d
{
namespace gl
{

PipelineLimits OpenGLRenderer::pipelineLimits() const noexcept
{
    if (!_isInitialized)
        return {};
    GLint units = 0, bindings = 0, blockBytes = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &bindings);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &blockBytes);
    //! Binding points are shared by every program, and Constants takes the first free one.
    const u32 freeBindings = static_cast<u32>(std::max(bindings, 0)) > kPipelineConstantsBinding + 1
                                 ? static_cast<u32>(bindings) - kPipelineConstantsBinding - 1
                                 : 0u;
#ifdef AURA_GLES
    constexpr u32 kBuffers = 0; //! GLES and WebGL have no createBuffer().
#else
    const u32 kBuffers = freeBindings;
#endif
    return {static_cast<u32>(std::max(units, 0)), kBuffers, static_cast<u32>(blockBytes) & ~15u,
            static_cast<usize>(blockBytes)};
}

PipelineHandle OpenGLRenderer::createPipeline(const PipelineDesc &desc)
{
    if (!_isInitialized || desc.vertexGlsl.empty() || desc.fragmentGlsl.empty())
    {
        INK_ERROR << "OpenGLRenderer: createPipeline needs an initialised renderer and PipelineDesc::vertexGlsl and "
                     "fragmentGlsl";
        return {};
    }
    const PipelineLimits limits = pipelineLimits();
    if (desc.textures > limits.textures || desc.buffers > limits.buffers || desc.constantBytes > limits.constantBytes ||
        desc.constantBytes % 16 != 0)
    {
        INK_ERROR << "OpenGLRenderer: this context lets a pipeline read " << limits.textures << " textures, "
                  << limits.buffers << " uniform buffers and " << limits.constantBytes
                  << " bytes of constants (a multiple of 16)";
        return {};
    }
    CustomPipeline pipeline;
    pipeline.textures = desc.textures;
    pipeline.buffers = desc.buffers;
    pipeline.constantBytes = desc.constantBytes;
    pipeline.input = desc.input;
    pipeline.state = desc.state;
    try
    {
        pipeline.program = GlShaderManager::createProgram(desc.vertexGlsl.c_str(), desc.fragmentGlsl.c_str());
    }
    catch (const std::exception &error)
    {
        INK_ERROR << "OpenGLRenderer: pipeline rejected: " << error.what();
        return {};
    }

    //! Sampler units and block bindings are program state, so they are set once. A name the shaders do not use is
    //! simply not found.
    glUseProgram(pipeline.program);
    for (u32 i = 0; i < desc.textures; ++i)
        glUniform1i(glGetUniformLocation(pipeline.program, ("tex" + std::to_string(i)).c_str()), static_cast<GLint>(i));
    if (const GLuint block = glGetUniformBlockIndex(pipeline.program, "Constants"); block != GL_INVALID_INDEX)
        glUniformBlockBinding(pipeline.program, block, kPipelineConstantsBinding);
    for (u32 i = 0; i < desc.buffers; ++i)
        if (const GLuint block = glGetUniformBlockIndex(pipeline.program, ("Buffer" + std::to_string(i)).c_str());
            block != GL_INVALID_INDEX)
            glUniformBlockBinding(pipeline.program, block, kPipelineConstantsBinding + 1 + i);

    //! One block for every pipeline's constants, as large as the largest asks.
    if (!_pipelineConstants)
        glGenBuffers(1, &_pipelineConstants);
    if (desc.constantBytes > _pipelineConstantsSize)
    {
        glBindBuffer(GL_UNIFORM_BUFFER, _pipelineConstants);
        glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(desc.constantBytes), nullptr, GL_DYNAMIC_DRAW);
        _pipelineConstantsSize = desc.constantBytes;
    }
    //! Core profiles draw nothing without a VAO, even one with no attributes.
    if (!_emptyVao)
        glGenVertexArrays(1, &_emptyVao);
    //! The program in use changed: the scene's comes back with its next draw.
    leaveSceneState();
    _customPipelines.push_back(pipeline);
    return PipelineHandle{static_cast<u32>(_customPipelines.size())};
}

void OpenGLRenderer::drawPipeline(const PipelineDraw &draw)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    if (!_isInitialized || !_vertexMgr || !isValidHandle(draw.pipeline) ||
        draw.pipeline.value() > _customPipelines.size())
        return;
    const CustomPipeline &pipeline = _customPipelines[draw.pipeline.value() - 1];
    if (draw.constants.size() > pipeline.constantBytes)
    {
        INK_ERROR << "OpenGLRenderer: drawPipeline given " << draw.constants.size()
                  << " bytes of constants; the pipeline has " << pipeline.constantBytes;
        return;
    }
    const MeshRecord *record = nullptr;
    const GlIndexBufferData *indices = nullptr;
    if (pipeline.input == VertexInput::Mesh)
    {
        record = getMesh(draw.mesh);
        indices = record ? _indexMgr->get(record->indexBuffer) : nullptr;
        if (!indices)
            return;
    }
#ifndef AURA_GLES
    std::vector<const GlBuffer *> buffers(pipeline.buffers);
    for (u32 i = 0; i < pipeline.buffers; ++i)
    {
        buffers[i] = i < draw.buffers.size() ? bufferOf(draw.buffers[i]) : nullptr;
        if (!buffers[i])
        {
            INK_ERROR << "OpenGLRenderer: drawPipeline needs buffer " << i << " (createBuffer)";
            return;
        }
    }
#endif

    //! What earlier batches staged draws first.
    flushBatches();
    glUseProgram(pipeline.program);

    const PipelineState state = pipeline.state;
    if (INK_FLAG_CHECK(state, PipelineState::DepthTest))
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
    }
    else
        glDisable(GL_DEPTH_TEST);
    glDepthMask(INK_FLAG_CHECK(state, PipelineState::DepthTest | PipelineState::DepthWrite) ? GL_TRUE : GL_FALSE);
    const bool blend = INK_FLAG_CHECK(state, PipelineState::AlphaBlend);
    if (blend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    const bool cull = INK_FLAG_CHECK(state, PipelineState::CullBackFaces);
    if (cull)
    {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        //! A target pass flips Y (see clipCorrection()), which turns every triangle around.
        glFrontFace(_passTarget != 0 ? GL_CW : GL_CCW);
    }

    for (u32 i = 0; i < pipeline.textures; ++i)
        if (i >= draw.textures.size() || !_textureMgr->bind(draw.textures[i], i))
            _textureMgr->bind(_whiteTexture, i);
    if (pipeline.constantBytes > 0)
    {
        _pipelineScratch.assign(pipeline.constantBytes, std::byte{0});
        std::memcpy(_pipelineScratch.data(), draw.constants.data(), draw.constants.size());
        glBindBuffer(GL_UNIFORM_BUFFER, _pipelineConstants);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(pipeline.constantBytes), _pipelineScratch.data());
        glBindBufferBase(GL_UNIFORM_BUFFER, kPipelineConstantsBinding, _pipelineConstants);
    }
#ifndef AURA_GLES
    const PipelineLimits limits = pipelineLimits();
    for (u32 i = 0; i < pipeline.buffers; ++i)
        glBindBufferRange(GL_UNIFORM_BUFFER, kPipelineConstantsBinding + 1 + i, buffers[i]->id, 0,
                          static_cast<GLsizeiptr>(std::min(buffers[i]->size, limits.bufferBytes)));
#endif

    if (pipeline.input == VertexInput::Mesh)
    {
        //! The same pair bindVertexBuffer() and bindIndexBuffer() make, without disturbing what the caller has bound.
        if (_vertexMgr->bind(record->vertexBuffer))
            _indexMgr->invalidateBinding();
        _indexMgr->bind(record->indexBuffer);
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(record->indexCount), indices->type, nullptr,
                                static_cast<GLsizei>(draw.instanceCount));
    }
    else
    {
        glBindVertexArray(_emptyVao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, static_cast<GLsizei>(draw.vertexCount),
                              static_cast<GLsizei>(draw.instanceCount));
    }

    if (cull)
        glDisable(GL_CULL_FACE);
    if (blend)
        glDisable(GL_BLEND);
    //! The scene's program, VAO, depth state and bindings come back with its next draw, and the textures this one
    //! put on units 1 to 3 were its material's maps.
    leaveSceneState();
    _materialUploaded = false;
}

void OpenGLRenderer::destroyCustomPipelines() noexcept
{
    for (const CustomPipeline &pipeline : _customPipelines)
        glDeleteProgram(pipeline.program);
    _customPipelines.clear();
    if (_pipelineConstants)
        glDeleteBuffers(1, &_pipelineConstants);
    if (_emptyVao)
        glDeleteVertexArrays(1, &_emptyVao);
    _pipelineConstants = _emptyVao = 0;
    _pipelineConstantsSize = 0;
}

} // namespace gl
} // namespace aura3d
