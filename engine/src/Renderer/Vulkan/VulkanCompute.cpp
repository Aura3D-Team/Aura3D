#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <array>
#include <cstring>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/aura.h"

namespace aura3d
{
namespace vk
{

namespace
{

constexpr u32 kComputeConstantsBytes = 128;
constexpr u32 kComputeSetsPerFrame = 256;
constexpr u32 kComputeBuffersPerFrame = 2048;

//! Everything a buffer can be read as after a kernel or a copy wrote it.
// NOLINTBEGIN(bugprone-signed-bitwise): Vulkan flag bits are C enums.
constexpr VkPipelineStageFlags kBufferReaders =
    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
    VK_PIPELINE_STAGE_HOST_BIT;
constexpr VkAccessFlags kBufferReads = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                                       VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                                       VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
// NOLINTEND(bugprone-signed-bitwise)

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags from, VkAccessFlags written, VkPipelineStageFlags to,
             VkAccessFlags accessed)
{
    const VkMemoryBarrier memory{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = written, .dstAccessMask = accessed};
    vkCmdPipelineBarrier(cmd, from, to, 0, 1, &memory, 0, nullptr, 0, nullptr);
}

} // namespace

const VulkanRenderer::ComputeBuffer *VulkanRenderer::computeBufferOf(BufferHandle handle) const noexcept
{
    return isValidHandle(handle) && handle.value() <= _computeBuffers.size() ? &_computeBuffers[handle.value() - 1]
                                                                             : nullptr;
}

BufferHandle VulkanRenderer::createBuffer(usize bytes, std::span<const std::byte> initial)
{
    if (!_isInitialized || bytes == 0 || initial.size() > bytes)
        return {};
    //! Host-visible and mapped, so readBuffer() is a copy; the GPU-side writers order themselves with barriers.
    // NOLINTNEXTLINE(bugprone-signed-bitwise): Vulkan flag bits are C enums.
    constexpr VkBufferUsageFlags kUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    ComputeBuffer buffer{
        _memoryManager->createBuffer(bytes, kUsage, VK_SHARING_MODE_EXCLUSIVE, VMA_MEMORY_USAGE_AUTO,
                                     VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT),
        bytes};
    if (!buffer.buffer.mappedData)
    {
        _memoryManager->destroyBuffer(buffer.buffer);
        return {};
    }
    std::memset(buffer.buffer.mappedData, 0, bytes);
    std::memcpy(buffer.buffer.mappedData, initial.data(), initial.size());
    VK_RESULT_CHECK(vmaFlushAllocation(_memoryManager->getAllocator(), buffer.buffer.allocation, 0, VK_WHOLE_SIZE));
    _computeBuffers.push_back(buffer);
    return BufferHandle{static_cast<u32>(_computeBuffers.size())};
}

bool VulkanRenderer::updateBuffer(BufferHandle handle, usize offset, std::span<const std::byte> bytes)
{
    const ComputeBuffer *buffer = computeBufferOf(handle);
    if (!buffer || offset > buffer->size || bytes.size() > buffer->size - offset || _frameState == FrameState::InPass)
        return false;
    if (_frameState == FrameState::Idle)
    {
        //! Outside a frame: earlier frames may still read it, so they finish first.
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
        std::memcpy(static_cast<std::byte *>(buffer->buffer.mappedData) + offset, bytes.data(), bytes.size());
        VK_RESULT_CHECK(
            vmaFlushAllocation(_memoryManager->getAllocator(), buffer->buffer.allocation, offset, bytes.size()));
        return true;
    }
    if (!_pipelineReady)
        return false;
    const std::optional<VkDeviceSize> staged = uploadTransient(bytes);
    if (!staged)
        return false;
    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    barrier(cmd, kBufferReaders | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    const VkBufferCopy region{*staged, offset, bytes.size()};
    vkCmdCopyBuffer(cmd, _batchVertexBuffers[_currentFrame].buffer, buffer->buffer.buffer, 1, &region);
    barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kBufferReaders,
            kBufferReads | VK_ACCESS_SHADER_WRITE_BIT);
    return true;
}

bool VulkanRenderer::readBuffer(BufferHandle handle, usize offset, std::span<std::byte> out)
{
    const ComputeBuffer *buffer = computeBufferOf(handle);
    if (!buffer || offset > buffer->size || out.size() > buffer->size - offset)
        return false;
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    VK_RESULT_CHECK(
        vmaInvalidateAllocation(_memoryManager->getAllocator(), buffer->buffer.allocation, offset, out.size()));
    std::memcpy(out.data(), static_cast<const std::byte *>(buffer->buffer.mappedData) + offset, out.size());
    return true;
}

ComputeHandle VulkanRenderer::createCompute(const ComputeDesc &desc)
{
    constexpr u32 kSpirvMagic = 0x07230203u;
    u32 magic = 0;
    if (desc.spirv.size() >= sizeof(magic))
        std::memcpy(&magic, desc.spirv.data(), sizeof(magic));
    if (!_isInitialized || magic != kSpirvMagic || desc.spirv.size() % 4 != 0 || desc.buffers == 0)
    {
        INK_ERROR << "VulkanRenderer: createCompute needs ComputeDesc::spirv and at least one buffer";
        return {};
    }
    VkDevice device = *_vkDeviceManager->getDevice();
    ComputeKernel kernel{.buffers = desc.buffers};
    VkShaderModule module = VK_NULL_HANDLE;
    try
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings(desc.buffers);
        for (u32 i = 0; i < desc.buffers; ++i)
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        const VkDescriptorSetLayoutCreateInfo setInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                      .bindingCount = desc.buffers,
                                                      .pBindings = bindings.data()};
        VK_RESULT_CHECK(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &kernel.setLayout));
        const VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT, 0, kComputeConstantsBytes};
        const VkPipelineLayoutCreateInfo layoutInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                    .setLayoutCount = 1,
                                                    .pSetLayouts = &kernel.setLayout,
                                                    .pushConstantRangeCount = 1,
                                                    .pPushConstantRanges = &constants};
        VK_RESULT_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &kernel.layout));
        const VkShaderModuleCreateInfo moduleInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                                  .codeSize = desc.spirv.size(),
                                                  .pCode = reinterpret_cast<const u32 *>(desc.spirv.data())};
        VK_RESULT_CHECK(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));
        const VkComputePipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                      .module = module,
                      .pName = "main"},
            .layout = kernel.layout,
        };
        VK_RESULT_CHECK(
            vkCreateComputePipelines(device, _pipelineCache->handle(), 1, &pipelineInfo, nullptr, &kernel.pipeline));
    }
    catch (const std::exception &error)
    {
        INK_ERROR << "VulkanRenderer: compute kernel rejected: " << error.what();
        destroyKernel(kernel);
        if (module != VK_NULL_HANDLE)
            vkDestroyShaderModule(device, module, nullptr);
        return {};
    }
    vkDestroyShaderModule(device, module, nullptr);
    _computeKernels.push_back(kernel);
    return ComputeHandle{static_cast<u32>(_computeKernels.size())};
}

void VulkanRenderer::dispatch(ComputeHandle handle, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                              std::span<const std::byte> constants)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    //! On the frame's primary buffer, so no pass may be open.
    if (_frameState != FrameState::Recording || !_pipelineReady || !isValidHandle(handle) ||
        handle.value() > _computeKernels.size() || constants.size() > kComputeConstantsBytes)
        return;
    const ComputeKernel &kernel = _computeKernels[handle.value() - 1];
    if (buffers.size() < kernel.buffers)
        return;
    VkDevice device = *_vkDeviceManager->getDevice();
    if (_computePools.empty())
    {
        _computePools.resize(_cmdBuffers.size(), VK_NULL_HANDLE);
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kComputeBuffersPerFrame};
        const VkDescriptorPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                                  .maxSets = kComputeSetsPerFrame,
                                                  .poolSizeCount = 1,
                                                  .pPoolSizes = &size};
        for (VkDescriptorPool &pool : _computePools)
            VK_RESULT_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
    }
    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkDescriptorSetAllocateInfo allocation{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                 .descriptorPool = _computePools[_currentFrame],
                                                 .descriptorSetCount = 1,
                                                 .pSetLayouts = &kernel.setLayout};
    if (vkAllocateDescriptorSets(device, &allocation, &set) != VK_SUCCESS)
    {
        INK_ERROR << "VulkanRenderer: more than " << kComputeSetsPerFrame << " dispatches in one frame";
        return;
    }
    std::vector<VkDescriptorBufferInfo> infos(kernel.buffers);
    std::vector<VkWriteDescriptorSet> writes(kernel.buffers);
    for (u32 i = 0; i < kernel.buffers; ++i)
    {
        const ComputeBuffer *buffer = computeBufferOf(buffers[i]);
        if (!buffer)
            return;
        infos[i] = {buffer->buffer.buffer, 0, VK_WHOLE_SIZE};
        writes[i] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                     .dstSet = set,
                     .dstBinding = i,
                     .descriptorCount = 1,
                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                     .pBufferInfo = &infos[i]};
    }
    vkUpdateDescriptorSets(device, kernel.buffers, writes.data(), 0, nullptr);

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    //! After earlier writers (copies, kernels) and readers (draws of the previous use) of the same buffers.
    barrier(cmd, kBufferReaders | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, kernel.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, kernel.layout, 0, 1, &set, 0, nullptr);
    std::array<std::byte, kComputeConstantsBytes> block{};
    std::memcpy(block.data(), constants.data(), constants.size());
    vkCmdPushConstants(cmd, kernel.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, kComputeConstantsBytes, block.data());
    vkCmdDispatch(cmd, groups.x, groups.y, groups.z);
    barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, kBufferReaders,
            kBufferReads | VK_ACCESS_SHADER_WRITE_BIT);
}

void VulkanRenderer::drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                               BufferHandle arguments, usize argumentOffset)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    if (_frameState != FrameState::InPass || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE)
        return;
    const ComputeBuffer *instanceBuffer = computeBufferOf(instances);
    const ComputeBuffer *argumentBuffer = computeBufferOf(arguments);
    const MeshRecord *record = getMesh(mesh);
    const VertexBufferInfo *vb = record ? vertexBufferOf(record->vertexBuffer) : nullptr;
    const IndexBufferInfo *ib = record ? indexBufferOf(record->indexBuffer) : nullptr;
    if (!instanceBuffer || !argumentBuffer || !vb || !ib || argumentOffset % 4 != 0 ||
        argumentOffset + sizeof(DrawIndexedIndirect) > argumentBuffer->size)
        return;
    if (isValidHandle(material))
        bindMaterial(material);

    ResolvedDraw draw;
    draw.vertexBuffer = vb->buffer;
    draw.indexBuffer = ib->buffer;
    draw.indexType = ib->indexType;
    draw.indexCount = record->indexCount;
    draw.textureIndex = textureArrayIndexOf(_currentTexture);
    draw.materialIndex = materialIndexOf(_currentMaterialHandle);
    SceneBindings bindings = sceneBindings();
    bindings.pipeline = _vkInstancedPipelineManager.get();
    aura3d::vk::bindDrawState(_sceneCmd, _recorded, bindings, draw);
    constexpr VkDeviceSize kOrigin = 0;
    vkCmdBindVertexBuffers(_sceneCmd, 1, 1, &instanceBuffer->buffer.buffer, &kOrigin);
    vkCmdDrawIndexedIndirect(_sceneCmd, argumentBuffer->buffer.buffer, argumentOffset, 1, sizeof(DrawIndexedIndirect));
}

void VulkanRenderer::resetComputePool()
{
    if (_currentFrame < _computePools.size() && _computePools[_currentFrame] != VK_NULL_HANDLE)
        VK_RESULT_CHECK(vkResetDescriptorPool(*_vkDeviceManager->getDevice(), _computePools[_currentFrame], 0));
}

void VulkanRenderer::destroyKernel(ComputeKernel &kernel) noexcept
{
    VkDevice device = *_vkDeviceManager->getDevice();
    if (kernel.pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, kernel.pipeline, nullptr);
    if (kernel.layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, kernel.layout, nullptr);
    if (kernel.setLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, kernel.setLayout, nullptr);
    kernel = {};
}

void VulkanRenderer::destroyCompute() noexcept
{
    if (!_vkDeviceManager || !_vkDeviceManager->getDevice())
        return;
    for (ComputeBuffer &buffer : _computeBuffers)
        _memoryManager->destroyBuffer(buffer.buffer);
    _computeBuffers.clear();
    for (ComputeKernel &kernel : _computeKernels)
        destroyKernel(kernel);
    _computeKernels.clear();
    for (const VkDescriptorPool pool : _computePools)
        if (pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(*_vkDeviceManager->getDevice(), pool, nullptr);
    _computePools.clear();
}

} // namespace vk
} // namespace aura3d
