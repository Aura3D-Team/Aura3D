#include "aura/Renderer/Vulkan/VkAura/VkCommandManager/VkCommandManager.h"

#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

namespace {

//! Source of the per-instance ids _threadPools()' cache is keyed by. Never
//! reused, unlike the object addresses it stands in for.
std::atomic<u64> g_nextManagerId{1};

} // namespace

VkCommandManager::VkCommandManager(VkDevice* device, u32 queueFamilyIndex)
    : _device(device),
      _queueFamilyIndex(queueFamilyIndex),
      _managerId(g_nextManagerId.fetch_add(1, std::memory_order_relaxed)) {
    // Command pools are created on demand, per thread, on first use.
}

VkCommandManager::~VkCommandManager() {
    std::lock_guard<std::mutex> lock(_poolMutex);

    for (auto& [threadId, pools] : _threadCommandPools) {
        if (pools->upload != VK_NULL_HANDLE)
            vkDestroyCommandPool(*_device, pools->upload, nullptr);

        //! Destroying a pool frees every command buffer allocated from it, so
        //! the cached secondaries need no separate vkFreeCommandBuffers.
        for (RenderPool& renderPool : pools->render) {
            if (renderPool.pool != VK_NULL_HANDLE)
                vkDestroyCommandPool(*_device, renderPool.pool, nullptr);
        }

        INK_DEBUG << "CommandPools for thread " << threadId << " were deleted.";
    }

    _threadCommandPools.clear();
    _device = nullptr;

    INK_DEBUG << "CommandPools destroyed.";
}

VkCommandPool VkCommandManager::_createPool(VkCommandPoolCreateFlags flags) const
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = flags;
    poolInfo.queueFamilyIndex = _queueFamilyIndex;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VK_RESULT_CHECK(vkCreateCommandPool(*_device, &poolInfo, nullptr, &commandPool));
    return commandPool;
}

VkCommandManager::ThreadPools& VkCommandManager::_threadPools()
{
    /*
     * Per-thread memo of the last resolved pool set, ahead of the map lookup.
     *
     * Every worker in a threaded drawMeshes() calls this once per chunk per
     * frame, and they all arrive at the same instant -- so the map lookup is
     * not merely a hash, it is N threads serialising on one mutex at exactly
     * the moment they were spun up to run concurrently. The memo removes both:
     * a hit touches no shared state at all.
     *
     * Keyed by _managerId rather than `this` because a destroyed manager's
     * address can be reused by a new one (switchBackend() does precisely
     * that), which would resurrect a pointer into freed pools. Ids come from a
     * monotonic counter and are never recycled, so a stale entry can only ever
     * miss -- never match the wrong manager.
     *
     * The cached reference stays valid for the manager's lifetime:
     * unordered_map does not invalidate references to existing elements on
     * rehash, and ThreadPools is separately heap-allocated on top of that.
     */
    thread_local u64 cachedManagerId = 0;
    thread_local ThreadPools* cachedPools = nullptr;

    if (cachedManagerId == _managerId && cachedPools != nullptr)
        return *cachedPools;

    const std::thread::id threadId = std::this_thread::get_id();

    std::lock_guard<std::mutex> lock(_poolMutex);
    std::unique_ptr<ThreadPools>& pools = _threadCommandPools[threadId];
    if (!pools)
        pools = std::make_unique<ThreadPools>();

    cachedManagerId = _managerId;
    cachedPools = pools.get();
    return *pools;
}

VkCommandPool VkCommandManager::getThreadCommandPool()
{
    ThreadPools& pools = _threadPools();
    if (pools.upload == VK_NULL_HANDLE)
        pools.upload = _createPool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

    return pools.upload;
}

VkFixedArray<VkCommandBuffer> VkCommandManager::createCommandBuffer()
{
    ThreadPools& pools = _threadPools();
    VkFixedArray<VkCommandBuffer> commandBuffers = {};

    /*
     * One allocation per frame, each from that frame's own pool, rather than
     * one batched allocation from a shared pool: resetRenderPools(frame) must
     * be able to recycle exactly one of these without disturbing the other,
     * which is only true if they come from different pools.
     */
    for (u32 frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        RenderPool& renderPool = pools.render[frame];
        if (renderPool.pool == VK_NULL_HANDLE)
            renderPool.pool = _createPool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = renderPool.pool;
        allocInfo.commandBufferCount = 1;

        VK_RESULT_CHECK(vkAllocateCommandBuffers(*_device, &allocInfo, &commandBuffers[frame]));
    }

    return commandBuffers;
}

VkCommandBuffer VkCommandManager::acquireSecondaryCommandBuffer(u32 frameIndex)
{
    if (frameIndex >= MAX_FRAMES_IN_FLIGHT)
        throw AuraException("acquireSecondaryCommandBuffer: frame index out of range");

    ThreadPools& pools = _threadPools();
    RenderPool& renderPool = pools.render[frameIndex];

    if (renderPool.pool == VK_NULL_HANDLE)
        renderPool.pool = _createPool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

    //! Steady state: the buffer this frame needs was allocated on an earlier
    //! frame and returned to the initial state by resetRenderPools().
    if (renderPool.handedOut < renderPool.secondaries.size())
        return renderPool.secondaries[renderPool.handedOut++];

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocInfo.commandPool = renderPool.pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VK_RESULT_CHECK(vkAllocateCommandBuffers(*_device, &allocInfo, &commandBuffer));

    renderPool.secondaries.push_back(commandBuffer);
    ++renderPool.handedOut;
    return commandBuffer;
}

void VkCommandManager::resetRenderPools(u32 frameIndex)
{
    if (frameIndex >= MAX_FRAMES_IN_FLIGHT)
        throw AuraException("resetRenderPools: frame index out of range");

    /*
     * Holds the lock across the resets rather than copying the pool handles
     * out first: this runs once per frame with no recording in flight, so
     * there is nothing to contend with, and holding it rules out a worker
     * thread registering a new pool set midway through the sweep.
     */
    std::lock_guard<std::mutex> lock(_poolMutex);

    for (auto& [threadId, pools] : _threadCommandPools) {
        RenderPool& renderPool = pools->render[frameIndex];
        if (renderPool.pool == VK_NULL_HANDLE)
            continue;

        //! Returns every buffer allocated from the pool to the initial state;
        //! the handles stay valid, which is what makes the recycling above work.
        VK_RESULT_CHECK(vkResetCommandPool(*_device, renderPool.pool, 0));
        renderPool.handedOut = 0;
    }
}

void VkCommandManager::resetCommandBuffer(VkCommandBuffer commandBuffer)
{
    VK_RESULT_CHECK(vkResetCommandBuffer(commandBuffer, 0));
}

void VkCommandManager::beginCommandBuffer(VkCommandBuffer commandBuffer)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    VK_RESULT_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
}

void VkCommandManager::beginSecondaryCommandBuffer(VkCommandBuffer commandBuffer,
                                                   VkRenderPass renderPass,
                                                   VkFramebuffer framebuffer)
{
    /*
     * subpass 0 because VkRenderPassManager builds a single-subpass render
     * pass; framebuffer is optional per spec but naming it lets tiled GPUs
     * keep the recorded work attached to the right tile memory instead of
     * conservatively assuming it may target any framebuffer.
     */
    VkCommandBufferInheritanceInfo inheritanceInfo{};
    inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritanceInfo.renderPass = renderPass;
    inheritanceInfo.subpass = 0;
    inheritanceInfo.framebuffer = framebuffer;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    //! RENDER_PASS_CONTINUE is what makes this buffer legal as the target of a
    //! vkCmdExecuteCommands inside an already-begun render pass.
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
                     | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    beginInfo.pInheritanceInfo = &inheritanceInfo;

    VK_RESULT_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
}

void VkCommandManager::endCommandBuffer(VkCommandBuffer commandBuffer) {
    VK_RESULT_CHECK(vkEndCommandBuffer(commandBuffer));
}

void VkCommandManager::freeCmdBuffer(VkCommandBuffer* commandBuffer)
{
    VkCommandPool commandPool = getThreadCommandPool();
    vkFreeCommandBuffers(*_device, commandPool, 1, commandBuffer);
}

}
}
