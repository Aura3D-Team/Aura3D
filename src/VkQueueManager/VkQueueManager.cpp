#include "VkQueueManager.h"
#include "VkException/VkException.h"
#include "plog/Log.h"

VkQueueManager::VkQueueManager()
    : _mapVkQueues()
{
  // Empty
}

VkQueueManager::~VkQueueManager()
{
    // Empty
}

void VkQueueManager::setupQueue(VkPhysicalDevice physicalDevice, VkDevice device, const VkQueueFlags flags)
{
    if (_mapVkQueues.find(flags) != _mapVkQueues.end()) {
        VkQueue queue;
        QueueData& queueData = _mapVkQueues[flags];
        vkGetDeviceQueue(device, queueData.vkDeviceQueueCreateInfo.queueFamilyIndex, queueData.vkDeviceQueueCreateInfo.queueCount-1, &queue);

        queueData.queue = queue;

        PLOG_INFO << "Queue set up successfully.";
    }
    else {
        throw VkException("There is no QueueData for this queue.");
    }
}

uint32_t VkQueueManager::pushQueueInfo(VkPhysicalDevice physicalDevice, const VkQueueFlags flags,  const float queuePriority) {
    if (_mapVkQueues.find(flags) == _mapVkQueues.end()) {
        uint32_t queueFamilyIndex = findQueueFamilyIndex(physicalDevice, flags);
        if (queueFamilyIndex == UINT32_MAX) {
            throw VkException("No suitable queue family found for graphics.");
        }

        QueueData& queueData = _mapVkQueues[flags];
        queueData.queue = VK_NULL_HANDLE;
        queueData.queuePriority = queuePriority;

        queueData.vkDeviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueData.vkDeviceQueueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueData.vkDeviceQueueCreateInfo.queueCount = 1;
        queueData.vkDeviceQueueCreateInfo.pQueuePriorities = &queueData.queuePriority;

        return queueFamilyIndex;
    } else {
        QueueData& queueData = _mapVkQueues[flags];
        queueData.vkDeviceQueueCreateInfo.queueCount++;
    }

    return _mapVkQueues[flags].vkDeviceQueueCreateInfo.queueFamilyIndex;
}

QueueData* VkQueueManager::getQueueData(VkQueueFlags flags)
{
    std::unordered_map<VkQueueFlags, QueueData>::iterator it = _mapVkQueues.find(flags);
    if (it != _mapVkQueues.end()) {
        return &it->second;
    } else {
        throw VkException("Requested queue not found.");
    }
}

uint32_t VkQueueManager::findQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags flags)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & flags) {
            return i;
        }
    }

    return UINT32_MAX;
}

std::vector<VkDeviceQueueCreateInfo> VkQueueManager::getDeviceQueueCreateInfos() const
{
    std::vector<VkDeviceQueueCreateInfo> deviceQueueCreateInfos;

    for (const std::pair<VkQueueFlags, QueueData>& p : _mapVkQueues) {
        deviceQueueCreateInfos.push_back(p.second.vkDeviceQueueCreateInfo);
    }

    return deviceQueueCreateInfos;
}
