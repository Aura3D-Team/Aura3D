#ifndef VKCOMMANDRECORDINGCONTEXT_H
#define VKCOMMANDRECORDINGCONTEXT_H

#pragma once

#include <array>
#include <span>

#include <vulkan/vulkan.h>

#include "aura/Core/AuraMath.h"
#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h"

namespace aura3d
{
namespace vk
{

/*
 * The inverse-transpose used for normals lives in aura/Core/AuraMath.h, since
 * the Metal backend needs the identical matrix and could not reach a definition
 * sitting behind <vulkan/vulkan.h>. Pulled into this namespace so every existing
 * unqualified normalMatrixOf() call site here and in VulkanRenderer keeps
 * resolving.
 */
using gfx::normalMatrixOf;

/**
 * @struct ResolvedDraw
 * @brief One draw with every handle already resolved to the Vulkan objects it
 *        needs, and nothing left to look up.
 *
 * Handle resolution (mesh -> buffer pair, material -> texture slot) happens on
 * the submitting thread, before any worker starts. That keeps the recording
 * threads off the renderer's handle tables entirely -- they never read a
 * container the main thread might be growing -- and turns the inner recording
 * loop into a linear walk over a compact POD array instead of a chain of
 * pointer hops.
 */
struct ResolvedDraw
{
    glm::mat4 model{1.0f};
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    u32 indexCount = 0;
    //! Slot in the bindless texture table; see VulkanRenderer::textureArrayIndexOf().
    u32 textureIndex = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
};

/**
 * @struct SceneBindings
 * @brief The per-frame state every draw in a pass shares.
 *
 * Read-only for the whole duration of recording, which is what makes it safe
 * to hand the same instance to every worker thread: all of it is created or
 * updated between frames, never while a command buffer is open.
 */
struct SceneBindings
{
    VkGraphicsPipelineManager *pipeline = nullptr;
    std::array<VkDescriptorSet, 3> descriptorSets{}; //!< Transform, textures, light.
    VkExtent2D extent{};
};

/**
 * @struct RecordedState
 * @brief What is already bound in one specific command buffer.
 *
 * Vulkan binds are sticky: a pipeline, viewport, descriptor set or buffer
 * stays bound until something replaces it. Re-issuing an identical bind is
 * therefore pure driver overhead, and the naive per-draw sequence (pipeline +
 * 3 sets + viewport + scissor + 2 buffers) is mostly that. Each field records
 * what the last vkCmd* actually left bound so the draw path can skip the
 * redundant ones.
 *
 * Strictly one instance per command buffer, never shared: a freshly begun
 * buffer -- secondary buffers especially, which inherit nothing but render
 * pass state -- starts with everything unbound, so a cache carried over from
 * another buffer would skip binds that this one still needs.
 */
struct RecordedState
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    //! set 1 (the bindless texture table) is bound once per pipeline bind
    //! rather than tracked per texture, so it folds in here with sets 0 and 2.
    bool staticSetsBound = false;
    bool viewportSet = false; //!< dynamic viewport + scissor

    void reset() noexcept
    {
        *this = RecordedState{};
    }
};

/**
 * @brief Records everything @p draw needs bound, skipping whatever @p state
 *        says is already bound in @p cmd, and updates @p state to match.
 *
 * Shared by the immediate-mode draw path and the batched/threaded one so the
 * two cannot drift apart: a redundancy filter that disagrees with what was
 * actually recorded is exactly how a skipped bind turns into a rendering bug.
 * Issuing the draw command itself is left to the caller, which is the only
 * part that genuinely differs between them.
 */
void bindDrawState(VkCommandBuffer cmd, RecordedState &state, const SceneBindings &bindings, const ResolvedDraw &draw);

/// Records a secondary buffer from its caller's thread-local command pool.
void recordChunk(VkCommandBuffer cmd, VkRenderPass renderPass, VkFramebuffer framebuffer, const SceneBindings &bindings,
                 std::span<const ResolvedDraw> draws);

} // namespace vk
} // namespace aura3d

#endif // VKCOMMANDRECORDINGCONTEXT_H
