#include "aura/Renderer/Vulkan/VkAura/VkDebugMode/VkTimestampQuery.h"

#include <array>

#include <ink/Inkogger.h>

namespace aura3d {
namespace vk {

namespace {

//! Two timestamps per frame slot: one before the frame's work, one after.
constexpr u32 kQueriesPerSlot = 2;

} // namespace

VkTimestampQuery::~VkTimestampQuery()
{
    destroy();
}

bool VkTimestampQuery::initialize(VkDevice device,
                                  VkPhysicalDevice physicalDevice,
                                  u32 queueFamilyIndex,
                                  u32 frameSlots) noexcept
{
    destroy();

    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || frameSlots == 0)
        return false;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    /*
     * timestampPeriod is the number of nanoseconds one tick represents, and a
     * device with no timestamp support reports it as zero. Checking it also
     * covers the case the limit exists but is meaningless, which is why this is
     * the gate rather than timestampComputeAndGraphics.
     */
    if (properties.limits.timestampPeriod <= 0.0f)
    {
        INK_INFO << "VkTimestampQuery: device reports no timestamp support; GPU timing disabled";
        return false;
    }

    /*
     * timestampValidBits is per queue family and may be fewer than 64. The bits
     * above it are undefined rather than zero, so an unmasked subtraction of
     * two timestamps on such a device produces noise. A family reporting zero
     * cannot write timestamps at all.
     */
    u32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);

    if (queueFamilyIndex >= familyCount)
        return false;

    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    const u32 validBits = families[queueFamilyIndex].timestampValidBits;
    if (validBits == 0)
    {
        INK_INFO << "VkTimestampQuery: queue family " << queueFamilyIndex
                 << " writes no timestamp bits; GPU timing disabled";
        return false;
    }

    //! Shifting by 64 is undefined, so the all-bits-valid case is spelled out.
    _validBitsMask = validBits >= 64 ? ~u64{0} : ((u64{1} << validBits) - 1);

    const VkQueryPoolCreateInfo poolInfo{
        .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType  = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = frameSlots * kQueriesPerSlot,
    };

    if (vkCreateQueryPool(device, &poolInfo, nullptr, &_queryPool) != VK_SUCCESS)
    {
        _queryPool = VK_NULL_HANDLE;
        INK_WARN << "VkTimestampQuery: vkCreateQueryPool failed; GPU timing disabled";
        return false;
    }

    _device       = device;
    _nanosPerTick = static_cast<f64>(properties.limits.timestampPeriod);
    _slotPending.assign(frameSlots, 0);

    _lastFrameMillis = 0.0;
    _resolvedSamples = 0;
    _droppedSamples  = 0;

    INK_INFO << "VkTimestampQuery: " << frameSlots << " slots, " << validBits
             << " valid bits, " << _nanosPerTick << " ns/tick";

    return true;
}

void VkTimestampQuery::destroy() noexcept
{
    if (_queryPool != VK_NULL_HANDLE && _device != VK_NULL_HANDLE)
        vkDestroyQueryPool(_device, _queryPool, nullptr);

    _queryPool = VK_NULL_HANDLE;
    _device    = VK_NULL_HANDLE;
    _slotPending.clear();
}

void VkTimestampQuery::writeBegin(VkCommandBuffer commandBuffer, u32 frameSlot) noexcept
{
    if (!isReady() || frameSlot >= _slotPending.size() || commandBuffer == VK_NULL_HANDLE)
        return;

    const u32 first = frameSlot * kQueriesPerSlot;

    /*
     * Reset inside the command buffer rather than through vkResetQueryPool: the
     * host-side entry point is Vulkan 1.2 core but gated on the hostQueryReset
     * feature actually being enabled at device creation, which this engine does
     * not request. The command-buffer reset has been core since 1.0 and needs
     * no feature at all.
     */
    vkCmdResetQueryPool(commandBuffer, _queryPool, first, kQueriesPerSlot);

    //! TOP_OF_PIPE: the timestamp is written as soon as the GPU reaches this
    //! command, which is the frame's true start on the device timeline.
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, _queryPool, first);

    _slotPending[frameSlot] = 1;
}

void VkTimestampQuery::writeEnd(VkCommandBuffer commandBuffer, u32 frameSlot) noexcept
{
    if (!isReady() || frameSlot >= _slotPending.size() || commandBuffer == VK_NULL_HANDLE)
        return;

    //! A slot whose begin was skipped (the frame bailed out before recording)
    //! must not get an end either: half a pair resolves to garbage.
    if (_slotPending[frameSlot] == 0)
        return;

    //! BOTTOM_OF_PIPE: written only once every preceding command has fully
    //! completed, which is what makes the difference the frame's GPU duration
    //! rather than the time to the last command being *issued*.
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _queryPool,
                        frameSlot * kQueriesPerSlot + 1);
}

void VkTimestampQuery::resolve(u32 frameSlot) noexcept
{
    if (!isReady() || frameSlot >= _slotPending.size())
        return;

    if (_slotPending[frameSlot] == 0)
        return;

    //! Consumed either way: a slot that could not be read is not retried, since
    //! the next frame is about to reset and overwrite the same two queries.
    _slotPending[frameSlot] = 0;

    std::array<u64, kQueriesPerSlot> ticks{};

    /*
     * No WAIT_BIT. The caller has already waited on this slot's fence, so the
     * results are there; asking Vulkan to block would only turn a driver hiccup
     * into a stall inside the profiler. VK_NOT_READY comes back instead, and is
     * counted rather than waited out.
     */
    const VkResult result = vkGetQueryPoolResults(
        _device, _queryPool, frameSlot * kQueriesPerSlot, kQueriesPerSlot,
        sizeof(ticks), ticks.data(), sizeof(u64),
        VK_QUERY_RESULT_64_BIT);

    if (result != VK_SUCCESS)
    {
        ++_droppedSamples;
        return;
    }

    const u64 begin = ticks[0] & _validBitsMask;
    const u64 end   = ticks[1] & _validBitsMask;

    //! Masked timestamps wrap, so end < begin is a wrap rather than an error.
    //! One frame's worth of samples is not worth reconstructing it for.
    if (end < begin)
    {
        ++_droppedSamples;
        return;
    }

    _lastFrameMillis = static_cast<f64>(end - begin) * _nanosPerTick / 1.0e6;
    ++_resolvedSamples;
}

GpuTimingStats VkTimestampQuery::stats() const noexcept
{
    GpuTimingStats timing;

    timing.available      = isReady() && _resolvedSamples > 0;
    timing.frameMillis    = _lastFrameMillis;
    timing.droppedSamples = _droppedSamples;

    return timing;
}

} // namespace vk
} // namespace aura3d
