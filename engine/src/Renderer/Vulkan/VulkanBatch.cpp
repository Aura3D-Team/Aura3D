#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <cstring>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/aura.h"

namespace aura3d
{
namespace vk
{

std::optional<std::pair<u32, u32>> VulkanRenderer::uploadBatch(std::span<const gfx::BatchVertex> vertices,
                                                               std::span<const u32> indices)
{
    const u32 frame = _currentFrame;
    // Every recorded batch keeps its own slice until the frame fence completes.
    const VkDeviceSize vertexOffset = _batchVertexUsed[frame];
    const VkDeviceSize indexOffset = _batchIndexUsed[frame];
    const VkDeviceSize vertexEnd = vertexOffset + vertices.size_bytes();
    const VkDeviceSize indexEnd = indexOffset + indices.size_bytes();

    growBatchBuffer(_batchVertexBuffers[frame], _batchVertexCapacity[frame], vertexEnd, 65536, kVertexRingUsage);
    growBatchBuffer(_batchIndexBuffers[frame], _batchIndexCapacity[frame], indexEnd, 16384,
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    AllocatedBuffer &vertexBuffer = _batchVertexBuffers[frame];
    AllocatedBuffer &indexBuffer = _batchIndexBuffers[frame];
    if (!vertexBuffer.mappedData || !indexBuffer.mappedData)
        return std::nullopt;

    std::memcpy(static_cast<u8 *>(vertexBuffer.mappedData) + vertexOffset, vertices.data(), vertices.size_bytes());
    std::memcpy(static_cast<u8 *>(indexBuffer.mappedData) + indexOffset, indices.data(), indices.size_bytes());

    VK_RESULT_CHECK(vmaFlushAllocation(_memoryManager->getAllocator(), vertexBuffer.allocation, vertexOffset,
                                       vertices.size_bytes()));
    VK_RESULT_CHECK(
        vmaFlushAllocation(_memoryManager->getAllocator(), indexBuffer.allocation, indexOffset, indices.size_bytes()));

    _batchVertexUsed[frame] = vertexEnd;
    _batchIndexUsed[frame] = indexEnd;
    //! Both offsets advance in whole elements, so they divide exactly.
    return std::pair{static_cast<u32>(vertexOffset / sizeof(gfx::BatchVertex)),
                     static_cast<u32>(indexOffset / sizeof(u32))};
}

void VulkanRenderer::growBatchBuffer(AllocatedBuffer &buffer, VkDeviceSize &capacity, VkDeviceSize needed,
                                     VkDeviceSize minimum, VkBufferUsageFlags usage)
{
    if (needed <= capacity)
        return;
    // Write-combined mapped memory avoids a staging copy for transient geometry. System RAM rather
    // than device-local BAR memory: the GPU reads each byte once during the draw, while a BAR copy
    // crosses PCIe as the CPU writes (measured 2.8x slower on a discrete GPU).
    constexpr VmaAllocationCreateFlags kDynamicFlags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    // Old draws retain the old buffer; only new batches use the replacement.
    auto &retired = _batchRetiredBuffers[_currentFrame];
    retired.reserve(retired.size() + 1);
    const VkDeviceSize newCapacity = std::max({needed, minimum, capacity * 2});
    const AllocatedBuffer replacement = _memoryManager->createBuffer(newCapacity, usage, VK_SHARING_MODE_EXCLUSIVE,
                                                                     VMA_MEMORY_USAGE_AUTO_PREFER_HOST, kDynamicFlags);
    if (buffer.buffer != VK_NULL_HANDLE)
        retired.push_back(buffer);
    buffer = replacement;
    capacity = newCapacity;
}

std::optional<VkDeviceSize> VulkanRenderer::uploadTransient(std::span<const std::byte> instances)
{
    const u32 frame = _currentFrame;
    //! Instances share the batch vertex ring. Batch draws address it in whole BatchVertex units, so the
    //! ring's end is left on such a boundary again after the instances.
    const VkDeviceSize offset = (_batchVertexUsed[frame] + 15) / 16 * 16;
    const VkDeviceSize end = offset + instances.size_bytes();
    growBatchBuffer(_batchVertexBuffers[frame], _batchVertexCapacity[frame], end, 65536, kVertexRingUsage);
    AllocatedBuffer &buffer = _batchVertexBuffers[frame];
    if (!buffer.mappedData)
        return std::nullopt;
    std::memcpy(static_cast<u8 *>(buffer.mappedData) + offset, instances.data(), instances.size_bytes());
    VK_RESULT_CHECK(
        vmaFlushAllocation(_memoryManager->getAllocator(), buffer.allocation, offset, instances.size_bytes()));
    constexpr VkDeviceSize kStride = sizeof(gfx::BatchVertex);
    _batchVertexUsed[frame] = (end + kStride - 1) / kStride * kStride;
    return offset;
}

bool VulkanRenderer::updateVertexBuffer(VertexBufferHandle handle, std::span<const gfx::Vertex3D> vertices)
{
    //! A transfer on the frame's primary buffer, so no pass may be open.
    const VertexBufferInfo *target = vertexBufferOf(handle);
    if (!target || _frameState != FrameState::Recording || !_pipelineReady || vertices.size() > target->vertexCount)
        return false;
    const std::optional<VkDeviceSize> offset = uploadTransient(std::as_bytes(vertices));
    if (!offset)
        return false;
    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    //! Earlier frames' vertex reads finish before the copy overwrites them; the copy lands before this frame's.
    const VkBufferMemoryBarrier beforeCopy{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = target->buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                         &beforeCopy, 0, nullptr);
    const VkBufferCopy region{*offset, 0, vertices.size_bytes()};
    vkCmdCopyBuffer(cmd, _batchVertexBuffers[_currentFrame].buffer, target->buffer, 1, &region);
    VkBufferMemoryBarrier afterCopy = beforeCopy;
    afterCopy.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    afterCopy.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1,
                         &afterCopy, 0, nullptr);
    return true;
}

void VulkanRenderer::destroyBatchBuffers()
{
    if (!_memoryManager || !_memoryManager->isInitialized())
        return;

    //! Bounded by the vector's own size rather than GetMaxFramesInFlight():
    //! this can run before createDescriptorSets() ever has (a teardown after
    //! a failed partial init), when these are still empty, and .size() is
    //! then correctly 0 rather than indexing off the end.
    for (u32 frame = 0; frame < _batchVertexBuffers.size(); ++frame)
    {
        if (_batchVertexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_batchVertexBuffers[frame]);
        if (_batchIndexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_batchIndexBuffers[frame]);

        //! Anything a mid-frame grow orphaned is still owed a free: teardown is
        //! past every fence, so this is the last and safest chance to take it.
        for (AllocatedBuffer &retired : _batchRetiredBuffers[frame])
            _memoryManager->destroyBuffer(retired);

        _batchRetiredBuffers[frame].clear();

        _batchVertexBuffers[frame] = {};
        _batchIndexBuffers[frame] = {};
        _batchVertexCapacity[frame] = 0;
        _batchIndexCapacity[frame] = 0;
        _batchVertexUsed[frame] = 0;
        _batchIndexUsed[frame] = 0;
    }
}

void VulkanRenderer::drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                               TextureHandle texture, gfx::BatchSpace space)
{
    AURA_FRAME_SCOPE(space == gfx::BatchSpace::Screen ? FramePhase::RecordOverlay : FramePhase::RecordScene);

    if (_frameState != FrameState::InPass || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE ||
        _bindlessTextureSet == VK_NULL_HANDLE || vertices.empty() || indices.empty())
        return;

    const auto first = uploadBatch(vertices, indices);
    if (!first)
        return;

    VkCommandBuffer cmd = _sceneCmd;
    const VkPipeline pipeline = _vkBatchPipelineManager->getPipeline();
    if (_recorded.pipeline != pipeline)
    {
        _vkBatchPipelineManager->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
        _vkBatchPipelineManager->cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 1, &_bindlessTextureSet,
                                                       0, nullptr);
        _recorded.pipeline = pipeline;
        _recorded.staticSetsBound = false;
    }
    if (!_recorded.viewportSet)
    {
        VkGraphicsPipelineManager::cmdSetViewportAndScissor(cmd, _passExtent);
        _recorded.viewportSet = true;
    }

    const BatchPushConstants pushConstants{batchTransform(space), textureArrayIndexOf(texture)};
    _vkBatchPipelineManager->cmdPushConstants(cmd, &pushConstants);

    //! Bound once per run of batches: each draw addresses its own slice by offset.
    const VkBuffer vertexBuffer = _batchVertexBuffers[_currentFrame].buffer;
    const VkBuffer indexBuffer = _batchIndexBuffers[_currentFrame].buffer;
    if (_recorded.vertexBuffer != vertexBuffer)
    {
        constexpr VkDeviceSize kOrigin = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &kOrigin);
        _recorded.vertexBuffer = vertexBuffer;
    }
    if (_recorded.indexBuffer != indexBuffer || _recorded.indexType != VK_INDEX_TYPE_UINT32)
    {
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        _recorded.indexBuffer = indexBuffer;
        _recorded.indexType = VK_INDEX_TYPE_UINT32;
    }

    vkCmdDrawIndexed(cmd, static_cast<u32>(indices.size()), 1, first->second, static_cast<i32>(first->first), 0);
}

} // namespace vk
} // namespace aura3d
