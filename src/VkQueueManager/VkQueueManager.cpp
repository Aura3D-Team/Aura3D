#include "VkQueueManager.h"
#include "VkException/VkException.h"
#include "plog/Log.h"

VkQueueManager::VkQueueManager()
    : _mapVkQueues(), _vkDeviceQueueCreateInfos()
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
        vkGetDeviceQueue(device, queueData.familyIndex, queueData.queueCount-1, &queue);

        queueData.queue = queue;

        PLOG_INFO << "Queue set up successfully.";
    }
    else {
        throw VkException("There is no QueueData for this queue.");
    }
}

uint32_t VkQueueManager::pushQueueInfo(VkPhysicalDevice physicalDevice, const VkQueueFlags flags,  const float queuePriority) {
    if (_mapVkQueues.find(flags) == _mapVkQueues.end()) {
        uint32_t graphicsQueueFamilyIndex = findQueueFamilyIndex(physicalDevice, flags);
        if (graphicsQueueFamilyIndex == UINT32_MAX) {
            throw VkException("No suitable queue family found for graphics.");
        }

        _mapVkQueues[flags] = { .queue = VK_NULL_HANDLE, .familyIndex = graphicsQueueFamilyIndex, .queueCount = 1, .queuePriority = queuePriority};

        QueueData& queueData = _mapVkQueues[flags];

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueData.familyIndex;
        queueCreateInfo.queueCount = queueData.queueCount;
        queueCreateInfo.pQueuePriorities = &queueData.queuePriority;

        _vkDeviceQueueCreateInfos.push_back(queueCreateInfo);

        return graphicsQueueFamilyIndex;
    } else {
        QueueData& queueData = _mapVkQueues[flags];
        queueData.queueCount++;

        for (uint32_t i = 0; i < _vkDeviceQueueCreateInfos.size(); i++) {
            if (_vkDeviceQueueCreateInfos[i].queueFamilyIndex == queueData.familyIndex) {
                _vkDeviceQueueCreateInfos[i].queueCount = queueData.queueCount;
                break;
            }
        }
    }

    return _mapVkQueues[flags].familyIndex;
}

std::vector<VkDeviceQueueCreateInfo>& VkQueueManager::getVkDeviceQueueCreateInfos()
{
    return _vkDeviceQueueCreateInfos;
}

VkQueue VkQueueManager::getQueue(VkQueueFlags flags) const
{
    const std::unordered_map<uint32_t, QueueData>::const_iterator& it = _mapVkQueues.find(flags);
    if (it != _mapVkQueues.end()) {
        return it->second.queue;
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
