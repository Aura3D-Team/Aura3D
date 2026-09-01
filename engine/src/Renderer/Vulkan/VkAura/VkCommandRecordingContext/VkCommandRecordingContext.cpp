#include "aura/Renderer/Vulkan/VkAura/VkCommandRecordingContext/VkCommandRecordingContext.h"

#include <bit>

#include "aura/aura.h"
#include "aura/Renderer/Vulkan/VkAura/VkCommandManager/VkCommandManager.h"

namespace aura3d {
namespace vk {

void bindDrawState(VkCommandBuffer cmd,
                   RecordedState& state,
                   const SceneBindings& bindings,
                   const ResolvedDraw& draw)
{
    if (!bindings.pipeline)
        return;

    /*
     * Everything below is a *sticky* Vulkan bind: it survives until something
     * replaces it. Only the push constant genuinely differs from draw to draw;
     * the pipeline, the viewport and sets 0/1/2 are identical for every object
     * in the pass -- set 1 is a single bindless texture table bound once, not
     * one set per texture, so a texture change is a push-constant write rather
     * than a rebind. Filtering the repeats here is what turns the per-draw
     * cost from eight vkCmd* calls into two for a run of objects.
     */
    const VkPipeline pipeline = bindings.pipeline->getPipeline();
    if (state.pipeline != pipeline)
    {
        bindings.pipeline->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
        state.pipeline = pipeline;
        /*
         * A different pipeline may use a different layout, and switching layout
         * disturbs previously bound sets, so the set cache cannot be trusted
         * across a pipeline change. Dynamic state (viewport/scissor) is not
         * layout-bound and does survive, so it is deliberately not reset here.
         */
        state.staticSetsBound = false;
    }

    if (!state.viewportSet)
    {
        VkGraphicsPipelineManager::cmdSetViewportAndScissor(cmd, bindings.extent);
        state.viewportSet = true;
    }

    //! sets 0 (view/projection), 1 (bindless textures) and 2 (light): fixed
    //! for the whole pass.
    if (!state.staticSetsBound)
    {
        if (bindings.transformSet != VK_NULL_HANDLE)
        {
            bindings.pipeline->cmdBindDescriptorSets(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 1, &bindings.transformSet, 0, nullptr);
        }

        if (bindings.textureTable != VK_NULL_HANDLE)
        {
            bindings.pipeline->cmdBindDescriptorSets(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 1, &bindings.textureTable, 0, nullptr);
        }

        if (bindings.lightSet != VK_NULL_HANDLE)
        {
            bindings.pipeline->cmdBindDescriptorSets(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 2, 1, &bindings.lightSet, 0, nullptr);
        }

        state.staticSetsBound = true;
    }

    /*
     * Per-draw transform. The model matrix travels as a push constant rather
     * than in the UBO, because the UBO is only written once per frame: pushing
     * here is what lets several objects with different transforms share a
     * single render pass.
     *
     * normalMatrix's 4th column is always (0,0,0,1) -- normalMatrixOf()
     * returns a mat3, widened to mat4 only to match std430/push-constant
     * padding rules -- so normalMatrix[3].x is dead data, reused here to carry
     * the bindless texture index (bit-cast, since push constants have no
     * separate integer lane). The vertex shader decodes it with
     * floatBitsToUint and forwards it as a flat varying; see vk_shader3d.*.
     */
    PushConstantBlock pushConstants;
    pushConstants.model = draw.model;
    pushConstants.normalMatrix = glm::mat4(normalMatrixOf(draw.model));
    pushConstants.normalMatrix[3].x = std::bit_cast<f32>(draw.textureIndex);
    bindings.pipeline->cmdPushConstants(cmd, &pushConstants);

    if (draw.vertexBuffer != VK_NULL_HANDLE && state.vertexBuffer != draw.vertexBuffer)
    {
        // Each mesh owns its own dedicated VkBuffer, so the bind offset is
        // always 0 -- VMA's suballocation offset is inside the device memory
        // block, not inside the buffer.
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &draw.vertexBuffer, &vertexOffset);
        state.vertexBuffer = draw.vertexBuffer;
    }

    if (draw.indexBuffer != VK_NULL_HANDLE &&
        (state.indexBuffer != draw.indexBuffer || state.indexType != draw.indexType))
    {
        vkCmdBindIndexBuffer(cmd, draw.indexBuffer, 0, draw.indexType);
        state.indexBuffer = draw.indexBuffer;
        state.indexType = draw.indexType;
    }
}

void VkCommandRecordingContext::recordChunk(VkCommandBuffer cmd,
                                            VkRenderPass renderPass,
                                            VkFramebuffer framebuffer,
                                            const SceneBindings& bindings,
                                            std::span<const ResolvedDraw> draws)
{
    VkCommandManager::beginSecondaryCommandBuffer(cmd, renderPass, framebuffer);

    //! A freshly begun secondary buffer inherits render pass state and nothing
    //! else -- no pipeline, no sets, no viewport -- so the cache starts empty
    //! regardless of what this context recorded on a previous frame or chunk.
    _recorded.reset();

    for (const ResolvedDraw& draw : draws)
    {
        if (draw.vertexBuffer == VK_NULL_HANDLE)
            continue;

        bindDrawState(cmd, _recorded, bindings, draw);

        //! Viewport and scissor are already recorded for this buffer by
        //! bindDrawState(), so the draw is issued directly rather than through
        //! cmdIndexedDraw()/cmdDraw(), which would re-set them every time.
        if (draw.indexBuffer != VK_NULL_HANDLE)
            vkCmdDrawIndexed(cmd, draw.indexCount, 1, 0, 0, 0);
        else if (draw.vertexCount > 0)
            vkCmdDraw(cmd, draw.vertexCount, 1, 0, 0);
    }

    VkCommandManager::endCommandBuffer(cmd);
}

} // namespace vk
} // namespace aura3d
