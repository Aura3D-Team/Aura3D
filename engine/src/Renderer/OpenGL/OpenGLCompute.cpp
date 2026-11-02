#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

// Compute and indirect drawing need GL 4.3, which neither GLES 3.0 nor WebGL 2 has.
#ifndef AURA_GLES

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <vector>

#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/Renderer/OpenGL/GlAura/GlShaderManager/GlShaderManager.h"
#include "aura/aura.h"

namespace aura3d
{
namespace gl
{

namespace
{
//! ComputeDesc's constants block; a uniform binding the scene's three blocks leave free.
constexpr GLuint kComputeConstantsBinding = 3;
constexpr usize kComputeConstantsBytes = 128;
} // namespace

bool OpenGLRenderer::hasCompute() const noexcept
{
    //! A 3.3 core request is usually answered with the newest core version, so this is a runtime question.
    return GLVersion.major > 4 || (GLVersion.major == 4 && GLVersion.minor >= 3);
}

const OpenGLRenderer::GlBuffer *OpenGLRenderer::bufferOf(BufferHandle handle) const noexcept
{
    return isValidHandle(handle) && handle.value() <= _buffers.size() ? &_buffers[handle.value() - 1] : nullptr;
}

BufferHandle OpenGLRenderer::createBuffer(usize bytes, std::span<const std::byte> initial)
{
    if (bytes == 0 || initial.size() > bytes)
        return {};
    std::vector<std::byte> contents(bytes, std::byte{0});
    std::ranges::copy(initial, contents.begin());
    GlBuffer buffer{0, bytes};
    glGenBuffers(1, &buffer.id);
    //! COPY_WRITE, not ARRAY or ELEMENT_ARRAY: those are vertex state the scene's caches track.
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer.id);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(bytes), contents.data(), GL_DYNAMIC_DRAW);
    _buffers.push_back(buffer);
    return BufferHandle{static_cast<u32>(_buffers.size())};
}

bool OpenGLRenderer::updateBuffer(BufferHandle handle, usize offset, std::span<const std::byte> bytes)
{
    const GlBuffer *buffer = bufferOf(handle);
    if (!buffer || offset > buffer->size || bytes.size() > buffer->size - offset)
        return false;
    //! GL orders the write after every earlier use of the buffer.
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer->id);
    glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes.size()),
                    bytes.data());
    return true;
}

bool OpenGLRenderer::readBuffer(BufferHandle handle, usize offset, std::span<std::byte> out)
{
    const GlBuffer *buffer = bufferOf(handle);
    if (!buffer || offset > buffer->size || out.size() > buffer->size - offset)
        return false;
    glBindBuffer(GL_COPY_READ_BUFFER, buffer->id);
    glGetBufferSubData(GL_COPY_READ_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(out.size()),
                       out.data());
    return true;
}

ComputeHandle OpenGLRenderer::createCompute(const ComputeDesc &desc)
{
    if (!hasCompute() || desc.glsl.empty())
    {
        INK_ERROR << "OpenGLRenderer: compute needs GL 4.3 and ComputeDesc::glsl";
        return {};
    }
    try
    {
        _computePrograms.push_back(GlShaderManager::createComputeProgram(desc.glsl.c_str()));
    }
    catch (const std::exception &error)
    {
        INK_ERROR << "OpenGLRenderer: " << error.what();
        return {};
    }
    const GLuint program = _computePrograms.back();
    if (const GLuint block = glGetUniformBlockIndex(program, "Constants"); block != GL_INVALID_INDEX)
        glUniformBlockBinding(program, block, kComputeConstantsBinding);
    if (!_computeConstants)
    {
        glGenBuffers(1, &_computeConstants);
        glBindBuffer(GL_UNIFORM_BUFFER, _computeConstants);
        glBufferData(GL_UNIFORM_BUFFER, kComputeConstantsBytes, nullptr, GL_DYNAMIC_DRAW);
    }
    return ComputeHandle{static_cast<u32>(_computePrograms.size())};
}

void OpenGLRenderer::dispatch(ComputeHandle kernel, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                              std::span<const std::byte> constants)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    if (!isValidHandle(kernel) || kernel.value() > _computePrograms.size() || constants.size() > kComputeConstantsBytes)
        return;
    //! Before the program changes, so a bad handle leaves the scene's state alone.
    if (!std::ranges::all_of(buffers,
                             [this](BufferHandle handle)
                             {
                                 return bufferOf(handle) != nullptr;
                             }))
        return;
    flushBatches();
    glUseProgram(_computePrograms[kernel.value() - 1]);
    for (GLuint i = 0; i < buffers.size(); ++i)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, bufferOf(buffers[i])->id);
    //! The whole block, so what this dispatch did not give reads as zero, as on Vulkan.
    std::array<std::byte, kComputeConstantsBytes> block{};
    std::ranges::copy(constants, block.begin());
    glBindBuffer(GL_UNIFORM_BUFFER, _computeConstants);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(block.size()), block.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, kComputeConstantsBinding, _computeConstants);
    glDispatchCompute(groups.x, groups.y, groups.z);
    //! What the kernel wrote, as every later reader takes it: storage, instance attributes, draw arguments, copies.
    // NOLINTNEXTLINE(bugprone-signed-bitwise): GL bitfields are C macros.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_COMMAND_BARRIER_BIT |
                    GL_BUFFER_UPDATE_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT);
    //! The scene's program and bindings come back with its next draw.
    leaveSceneState();
}

void OpenGLRenderer::drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                               BufferHandle arguments, usize argumentOffset)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    const GlBuffer *instanceBuffer = bufferOf(instances);
    const GlBuffer *argumentBuffer = bufferOf(arguments);
    const MeshRecord *record = getMesh(mesh);
    if (!_vertexMgr || !instanceBuffer || !argumentBuffer || !record ||
        argumentOffset + sizeof(DrawIndexedIndirect) > argumentBuffer->size || argumentOffset % 4 != 0)
        return;
    if (isValidHandle(material))
        bindMaterial(material);
    bindVertexBuffer(record->vertexBuffer);
    bindIndexBuffer(record->indexBuffer);
    const auto *indices = _indexMgr->get(record->indexBuffer);
    if (!indices)
        return;
    useSceneState();
    uploadMaterial();
    glUseProgram(_instancedProgram);
    glUniform1i(_instancedCoverageLoc, _coverage);
    attachInstances(instanceBuffer->id);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, argumentBuffer->id);
    // NOLINTBEGIN(performance-no-int-to-ptr): GL takes buffer offsets as pointers.
    glDrawElementsIndirect(GL_TRIANGLES, indices->type, reinterpret_cast<const void *>(argumentOffset));
    // NOLINTEND(performance-no-int-to-ptr)
    detachInstances();
}

} // namespace gl
} // namespace aura3d

#endif // AURA_GLES
