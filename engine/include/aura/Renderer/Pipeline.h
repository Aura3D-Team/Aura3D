#ifndef AURA_RENDERER_PIPELINE_H
#define AURA_RENDERER_PIPELINE_H

#pragma once

#include <cstddef>
#include <span>
#include <string>

#include "aura/Renderer/PipelineState.h"
#include "aura/Renderer/RenderHandles.h"

namespace aura3d
{

/// Where a pipeline's vertices come from.
enum class VertexInput : u8
{
    Mesh, //!< A mesh's gfx::Vertex3D: location 0 position (vec3), 1 uv (vec2), 2 colour (vec4), 3 normal (vec3).
    None, //!< Nothing bound: the shader builds each vertex from gl_VertexIndex / gl_VertexID, as a full-screen pass
          //!< does.
};

/**
 * @brief Your own vertex and fragment shaders: the engine draws with them and decides nothing about the look.
 *
 * Give the code of each backend you target; one GLSL source serves both GPU backends when it branches on VULKAN,
 * which glslc defines (tests/shaders has examples). A shader reads a Constants block, textures texN and uniform
 * blocks BufferN, as many as you declare here within IRenderer::pipelineLimits(); docs/16-custom-pipelines.md has the
 * declarations for each backend. Clip space is y-up on every backend, with the depth range of
 * Camera::setClipSpace(); a render target's texture holds its top row first, so sample it at
 * `vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5)`, and multiply a projection by IRenderer::clipCorrection(target) when
 * drawing into a target.
 */
struct PipelineDesc
{
    std::span<const std::byte> vertexSpirv;
    std::span<const std::byte> fragmentSpirv;
    std::string vertexGlsl;
    std::string fragmentGlsl;
    PipelineState state = PipelineState::DepthTest | PipelineState::DepthWrite | PipelineState::CullBackFaces;
    VertexInput input = VertexInput::Mesh;
    u32 textures = 0;      //!< texN the shaders read, N from 0.
    u32 buffers = 0;       //!< BufferN the shaders read, N from 0.
    u32 constantBytes = 0; //!< Size of Constants: a multiple of 16.
};

/// What this device lets a pipeline use; createPipeline() refuses more.
struct PipelineLimits
{
    u32 textures = 0;
    u32 buffers = 0;
    u32 constantBytes = 0; //!< Past this, put the data in a buffer.
    usize bufferBytes = 0; //!< What one BufferN shows: the first bufferBytes of its buffer.
};

/// One draw with a pipeline: everything it reads, given here, so nothing is left bound for the next draw.
struct PipelineDraw
{
    PipelineHandle pipeline;
    MeshHandle mesh;     //!< VertexInput::Mesh.
    u32 vertexCount = 0; //!< VertexInput::None.
    u32 instanceCount = 1;
    std::span<const std::byte> constants;    //!< At most PipelineDesc::constantBytes; the rest reads as zero.
    std::span<const TextureHandle> textures; //!< One per texN; a missing or invalid one samples white.
    std::span<const BufferHandle> buffers;   //!< One per BufferN, from IRenderer::createBuffer().
};

} // namespace aura3d

#endif // AURA_RENDERER_PIPELINE_H
