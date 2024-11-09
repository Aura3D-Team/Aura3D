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
        QueueData& queueData = _mapVkQueues[flags];

        for (uint32_t i = 0; i < queueData.vkDeviceQueueCreateInfo.queueCount; ++i) {
            VkQueue queue;
            vkGetDeviceQueue(device, queueData.vkDeviceQueueCreateInfo.queueFamilyIndex, i, &queue);
            queueData.queues.push_back(queue);

            PLOG_INFO << "Queue " << i << " set up successfully for flags " << flags;
        }
    }
    else {
        throw VkException("There is no QueueData for this queue.");
    }
}

uint32_t VkQueueManager::pushQueueInfo(VkPhysicalDevice physicalDevice, const VkQueueFlags flags,  const float queuePriority) {
    uint32_t queueFamilyIndex = findQueueFamilyIndex(physicalDevice, flags);
    if (queueFamilyIndex == UINT32_MAX) {
        throw VkException("No suitable queue family found for graphics.");
    }

    if (_mapVkQueues.find(flags) == _mapVkQueues.end()) {
        QueueData& queueData = _mapVkQueues[flags];
        queueData.queuesPriorities.push_back(queuePriority);

        queueData.vkDeviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueData.vkDeviceQueueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueData.vkDeviceQueueCreateInfo.queueCount = 1;
        queueData.vkDeviceQueueCreateInfo.pQueuePriorities = queueData.queuesPriorities.data();
    } else {
        QueueData& queueData = _mapVkQueues[flags];
        queueData.queuesPriorities.push_back(queuePriority);
        queueData.vkDeviceQueueCreateInfo.queueCount = queueData.queuesPriorities.size();
        queueData.vkDeviceQueueCreateInfo.pQueuePriorities = queueData.queuesPriorities.data();
    }

    return queueFamilyIndex;
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

uint32_t VkQueueManager::getQueueMapSize() const
{
    return _mapVkQueues.size();
}

uint32_t VkQueueManager::findQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags flags, VkSurfaceKHR surface) {
    std::vector<VkQueueFamilyProperties> queueFamilies = findQueueFamilies(physicalDevice);

    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if ((queueFamilies[i].queueFlags & flags) == flags) {
            if (surface != VK_NULL_HANDLE) {
                if (isPresentQueueSupported(physicalDevice, i, surface)) {
                    return i;
                }
            } else {
                return i;
            }
        }
    }

    return UINT32_MAX;
}

std::vector<VkQueueFamilyProperties> VkQueueManager::findQueueFamilies(VkPhysicalDevice physicalDevice)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    return queueFamilies;
}

bool VkQueueManager::isPresentQueueSupported(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface) {
    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &presentSupport);
    return presentSupport == VK_TRUE;
}

std::vector<VkDeviceQueueCreateInfo> VkQueueManager::getDeviceQueueCreateInfos() const
{
    std::vector<VkDeviceQueueCreateInfo> deviceQueueCreateInfos;

    for (const std::pair<VkQueueFlags, QueueData>& p : _mapVkQueues) {
        deviceQueueCreateInfos.push_back(p.second.vkDeviceQueueCreateInfo);
    }

    return deviceQueueCreateInfos;
}
