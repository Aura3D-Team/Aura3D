#ifndef VKCOMMANDMANAGER_H
#define VKCOMMANDMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <vector>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"

namespace aura3d {
namespace vk {

/**
 * @brief Manages Vulkan command pools and command buffers in a multi-threaded environment.
 *
 * Two distinct families of pool, because they have incompatible lifetimes:
 *
 *  - **Upload pools** (getThreadCommandPool()): one per thread, never reset by
 *    this class. Serve the one-off transfer buffers that vertex/index/texture
 *    creation allocates and frees itself.
 *
 *  - **Render pools** (getRenderCommandPool()): one per thread *per frame in
 *    flight*, reset as a group by resetRenderPools() at the top of each frame.
 *    The per-frame split is what makes the reset safe: vkResetCommandPool
 *    recycles every buffer in the pool, so a single pool shared across frames
 *    would recycle the other frame-in-flight's buffer while the GPU could
 *    still be executing it -- the renderer only ever waits on the fence for
 *    the frame it is about to record. Keying by frame means a reset only
 *    touches buffers whose fence has just been waited on.
 *
 * Recording several command buffers concurrently is the reason for the
 * per-thread split: a VkCommandPool must be externally synchronized, so
 * threads cannot share one.
 */
class VkCommandManager
{
public:
    /**
     * @brief Constructs a `VkCommandManager` with a logical device and queue family index.
     *
     * This constructor initializes the `VkCommandManager` with a given Vulkan logical device (`VkDevice`)
     * and a specific queue family index. The manager will use these to allocate and manage command pools
     * and command buffers for Vulkan operations.
     *
     * @param device Pointer to a Vulkan logical device.
     * @param queueFamilyIndex Queue family index used for creating command pools.
     */
    VkCommandManager(VkDevice* device, u32 queueFamilyIndex);

    /**
     * @brief Destroys the `VkCommandManager` and releases resources.
     *
     * The destructor ensures that all command pools managed by the `VkCommandManager` are properly
     * released, freeing any Vulkan resources allocated.
     */
    ~VkCommandManager();

    /**
     * @brief Retrieves the upload command pool associated with the current thread.
     *
     * Ensures that each thread has its own command pool for safe multi-threaded command buffer allocation.
     * Never reset by this class -- callers allocating from it are expected to
     * free their own buffers (see VkBufferManager / VkTextureManager).
     *
     * @return VkCommandPool The thread-local upload command pool.
     */
    VkCommandPool getThreadCommandPool();

    /**
     * @brief Allocates the primary command buffers, one per frame in flight.
     *
     * Each is allocated from the calling thread's render pool for its own
     * frame index, so resetRenderPools(frame) recycles exactly one of them.
     */
    VkFixedArray<VkCommandBuffer> createCommandBuffer();

    /**
     * @brief Hands out a secondary command buffer for @p frameIndex, for
     *        recording draws that a primary buffer will vkCmdExecuteCommands.
     *
     * Allocated from the calling thread's render pool for that frame, so
     * concurrent callers on different threads never touch the same pool.
     * Buffers are recycled rather than reallocated: resetRenderPools() returns
     * the frame's buffers to the pool and rewinds the hand-out cursor, so a
     * steady-state frame stops allocating entirely after the first few.
     *
     * @param frameIndex Frame in flight being recorded, < MAX_FRAMES_IN_FLIGHT.
     * @return A secondary command buffer in the initial state, ready to begin.
     */
    VkCommandBuffer acquireSecondaryCommandBuffer(u32 frameIndex);

    /**
     * @brief Begins @p commandBuffer as a secondary buffer that will execute
     *        inside @p renderPass / @p framebuffer.
     *
     * Sets RENDER_PASS_CONTINUE, which is what makes the buffer legal inside a
     * render pass begun with VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS.
     */
    static void beginSecondaryCommandBuffer(VkCommandBuffer commandBuffer,
                                            VkRenderPass renderPass,
                                            VkFramebuffer framebuffer);

    static void resetCommandBuffer(VkCommandBuffer commandBuffer);

    /**
     * @brief Resets every thread's render pool for @p frameIndex.
     *
     * Called once per frame from the render thread, before any recording for
     * that frame begins. Resetting other threads' pools from here is safe
     * precisely because it happens at that point: vkResetCommandPool requires
     * external synchronization only against concurrent *use* of the pool, and
     * no worker is recording between frames.
     *
     * @param frameIndex Frame in flight whose fence the caller has just waited on.
     */
    void resetRenderPools(u32 frameIndex);

    /**
     * @brief Begins one or more command buffers for recording commands.
     *
     * This method allocates and begins recording on a specified number of command buffers.
     * The command buffers returned are ready for recording Vulkan commands.
     *
     * @param commandBuffer The command buffer to begin recording.
     */
    static void beginCommandBuffer(VkCommandBuffer commandBuffer);

    /**
     * @brief Ends recording of a command buffer array and submits it to a Vulkan queue.
     *
     * This method finalizes the recording of the specified command buffer array and submits it to the
     * provided Vulkan queue for execution.
     *
     * @param commandBuffer The command buffer to end.
     */
    static void endCommandBuffer(VkCommandBuffer commandBuffer);

    void freeCmdBuffer(VkCommandBuffer* commandBuffer);

private:
    /**
     * @brief One thread's render pool for one frame in flight, plus the
     *        secondary buffers recycled from it.
     */
    struct RenderPool {
        VkCommandPool pool = VK_NULL_HANDLE;
        //! Allocated on demand and reused every frame thereafter.
        std::vector<VkCommandBuffer> secondaries;
        //! How many of `secondaries` have been handed out this frame; rewound
        //! to 0 by resetRenderPools().
        size_t handedOut = 0;
    };

    //! Every pool belonging to one thread: the frame-agnostic upload pool plus
    //! one render pool per frame in flight.
    struct ThreadPools {
        VkCommandPool upload = VK_NULL_HANDLE;
        VkFixedArray<RenderPool> render{};
    };

    /**
     * @brief Returns the calling thread's pool set, creating it on first use.
     *
     * The returned pointer stays valid for this object's lifetime:
     * unordered_map never invalidates references to existing elements on
     * rehash, and the mapped type is separately heap-allocated on top of that.
     * That is what lets callers drop @c _poolMutex before touching their own
     * entry -- only the map lookup needs to be serialized, not the recording
     * that follows it.
     */
    ThreadPools& _threadPools();

    VkCommandPool _createPool(VkCommandPoolCreateFlags flags) const;

    VkDevice* _device;
    u32 _queueFamilyIndex; /**< The queue family index for command pool allocation. */

    /*
     * Per-instance, not static: a static map would be shared by every
     * VkCommandManager ever constructed, so a second renderer (or a renderer
     * recreated after teardown) would inherit pools belonging to a destroyed
     * VkDevice.
     */
    std::unordered_map<std::thread::id, std::unique_ptr<ThreadPools>> _threadCommandPools;
    mutable std::mutex _poolMutex; /**< Mutex to protect access to the command pool map. */
};

}
}

#endif // VKCOMMANDMANAGER_H
