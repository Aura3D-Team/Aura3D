#include "VkQueueManager.h"

#include <aura.hpp>
#include "AuraException/AuraException.h"

#include <ink/ink.hpp>

namespace aura3d {

VkQueueManager::VkQueueManager()
    : _mapVkQueues()
{
    // No additional initialization required.
}

VkQueueManager::~VkQueueManager()
{
    // No explicit cleanup required.
}

void VkQueueManager::setupQueue(VkDevice device, u32 queueFamilyIndex, const VkQueueFlags flags)
{
    std::pair<u32, VkQueueFlags> key = std::make_pair(queueFamilyIndex, flags);
    auto it = _mapVkQueues.find(key);
    if (it != _mapVkQueues.end()) {
        QueueData& queueData = it->second;

        for (u32 i = 0; i < queueData.vkDeviceQueueCreateInfo.queueCount; ++i) {
            VkQueue queue;
            vkGetDeviceQueue(device, queueData.vkDeviceQueueCreateInfo.queueFamilyIndex, i, &queue);
            queueData.queues.push_back(queue);

            INK_DEBUG << "Queue " << i << " set up successfully for family index "
                      << queueFamilyIndex << " with flags " << flags;
        }
    }
    else {
        throw AuraException("There is no QueueData registered for the given queue family index and flags.");
    }
}

u32 VkQueueManager::pushQueueInfo(VkPhysicalDevice physicalDevice, const VkQueueFlags flags, const f32 queuePriority)
{
    u32 queueFamilyIndex = findQueueFamilyIndex(physicalDevice, flags);
    if (queueFamilyIndex == UINT32_MAX) {
        throw AuraException("No suitable queue family found for the requested capabilities.");
    }

    std::pair<u32, VkQueueFlags> key = std::make_pair(queueFamilyIndex, flags);
    if (_mapVkQueues.find(key) == _mapVkQueues.end()) {
        QueueData& queueData = _mapVkQueues[key];
        queueData.queuePriorities.push_back(queuePriority);

        queueData.vkDeviceQueueCreateInfo = {};
        queueData.vkDeviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueData.vkDeviceQueueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueData.vkDeviceQueueCreateInfo.queueCount = 1;
        queueData.vkDeviceQueueCreateInfo.pQueuePriorities = queueData.queuePriorities.data();
    } else {
        QueueData& queueData = _mapVkQueues[key];
        queueData.queuePriorities.push_back(queuePriority);
        queueData.vkDeviceQueueCreateInfo.queueCount = static_cast<u32>(queueData.queuePriorities.size());
        queueData.vkDeviceQueueCreateInfo.pQueuePriorities = queueData.queuePriorities.data();
    }

    return queueFamilyIndex;
}

std::vector<QueueData*> VkQueueManager::getQueues(VkQueueFlags flags)
{
    std::vector<QueueData*> result;
    for (auto& [key, queueData] : _mapVkQueues) {
        if ((key.second & flags) == flags) {
            result.push_back(&queueData);
        }
    }
    return result;
}

u32 VkQueueManager::getQueueMapSize() const
{
    return static_cast<u32>(_mapVkQueues.size());
}

u32 VkQueueManager::findQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags flags, VkSurfaceKHR surface)
{
    std::vector<VkQueueFamilyProperties> queueFamilies = findQueueFamilies(physicalDevice);

    u32 bestIndex = UINT32_MAX;
    for (u32 i = 0; i < static_cast<u32>(queueFamilies.size()); i++) {
        if ((queueFamilies[i].queueFlags & flags) == flags) {
            if (surface != VK_NULL_HANDLE && !isPresentQueueSupported(physicalDevice, i, surface)) {
                continue;
            }
            if (queueFamilies[i].queueFlags == flags) {
                return i;
            }
            if (bestIndex == UINT32_MAX) {
                bestIndex = i;
            }
        }
    }

    return bestIndex;
}

std::vector<u32> VkQueueManager::findQueueFamilyIndices(VkPhysicalDevice physicalDevice, VkQueueFlags flags, VkSurfaceKHR surface)
{
    std::vector<u32> matchingIndices;
    std::vector<VkQueueFamilyProperties> queueFamilies = findQueueFamilies(physicalDevice);

    for (u32 i = 0; i < static_cast<u32>(queueFamilies.size()); i++) {
        if ((queueFamilies[i].queueFlags & flags) == flags) {
            if (surface != VK_NULL_HANDLE) {
                if (isPresentQueueSupported(physicalDevice, i, surface)) {
                    matchingIndices.push_back(i);
                }
            } else {
                matchingIndices.push_back(i);
            }
        }
    }

    return matchingIndices;
}

std::vector<VkQueueFamilyProperties> VkQueueManager::findQueueFamilies(VkPhysicalDevice physicalDevice)
{
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    return queueFamilies;
}

bool VkQueueManager::isPresentQueueSupported(VkPhysicalDevice physicalDevice, u32 queueFamilyIndex, VkSurfaceKHR surface)
{
    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &presentSupport);
    return presentSupport == VK_TRUE;
}

std::vector<VkDeviceQueueCreateInfo> VkQueueManager::getDeviceQueueCreateInfos() const
{
    std::vector<VkDeviceQueueCreateInfo> deviceQueueCreateInfos;

    for (const auto& pair : _mapVkQueues) {
        deviceQueueCreateInfos.push_back(pair.second.vkDeviceQueueCreateInfo);
    }

    return deviceQueueCreateInfos;
}

void VkQueueManager::submitCmdIntoQueue(VkQueue queue,
                                        VkCommandBuffer* commandBuffer,
                                        VkSemaphore* imageAvailableSemaphore,
                                        VkSemaphore* renderFinishedSemaphore,
                                        VkFence fence)
{
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandBuffer;

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = renderFinishedSemaphore;

    VK_RESULT_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));

    VK_RESULT_CHECK(vkQueueWaitIdle(queue));
}

}
