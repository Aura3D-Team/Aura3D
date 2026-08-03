#ifndef VKCOMMANDRECORDINGCONTEXT_H
#define VKCOMMANDRECORDINGCONTEXT_H

#pragma once

#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h"

namespace aura3d {
namespace vk {

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
struct ResolvedDraw {
    glm::mat4 model{1.0f};
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    //! VK_NULL_HANDLE for a non-indexed draw, where vertexCount is used instead.
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    u32 indexCount = 0;
    u32 vertexCount = 0;
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
struct SceneBindings {
    VkGraphicsPipelineManager* pipeline = nullptr;
    VkDescriptorSet transformSet = VK_NULL_HANDLE;  //!< set 0
    VkDescriptorSet textureTable = VK_NULL_HANDLE;  //!< set 1, the bindless table
    VkDescriptorSet lightSet = VK_NULL_HANDLE;      //!< set 2
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
struct RecordedState {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    //! set 1 (the bindless texture table) is bound once per pipeline bind
    //! rather than tracked per texture, so it folds in here with sets 0 and 2.
    bool staticSetsBound = false;
    bool viewportSet = false; //!< dynamic viewport + scissor

    void reset() noexcept { *this = RecordedState{}; }
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
void bindDrawState(VkCommandBuffer cmd,
                   RecordedState& state,
                   const SceneBindings& bindings,
                   const ResolvedDraw& draw);

/**
 * @class VkCommandRecordingContext
 * @brief One thread's slot for recording a chunk of a frame's draws into its
 *        own secondary command buffer.
 *
 * Exists because the two things a recording thread must not share are exactly
 * a command buffer (pools require external synchronization) and a bind cache
 * (RecordedState describes one buffer). Bundling them means "give each worker
 * a context" is the whole of the thread-safety argument: everything else a
 * worker touches -- SceneBindings, the pipeline manager's cached handles, the
 * ResolvedDraw array -- is read-only for the duration.
 *
 * Contexts are owned by VulkanRenderer and reused across frames, so a
 * steady-state frame allocates nothing here.
 */
class VkCommandRecordingContext
{
public:
    /**
     * @brief Records @p draws into @p cmd as a complete secondary command buffer.
     *
     * Begins @p cmd with inheritance from @p renderPass / @p framebuffer,
     * records every draw, and ends it. The caller replays the result with
     * vkCmdExecuteCommands.
     *
     * Safe to call concurrently on different contexts, provided each was given
     * a command buffer from its own thread's pool.
     */
    void recordChunk(VkCommandBuffer cmd,
                     VkRenderPass renderPass,
                     VkFramebuffer framebuffer,
                     const SceneBindings& bindings,
                     std::span<const ResolvedDraw> draws);

private:
    //! This context's private view of what is bound in the buffer it is
    //! currently recording. Reset at the start of every chunk.
    RecordedState _recorded;
};

} // namespace vk
} // namespace aura3d

#endif // VKCOMMANDRECORDINGCONTEXT_H
