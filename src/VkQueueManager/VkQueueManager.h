#ifndef VKQUEUEMANAGER_H
#define VKQUEUEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>

/**
 * @class VkQueueManager
 *
 * @brief Manages Vulkan device queues, specifically graphics and potentially other types of queues.
 *        It stores queues in a map indexed by their capabilities (e.g., graphics, compute, transfer).
 */
class VkQueueManager
{
public:
    /**
     * @brief Constructor for the queue manager.
     *        Does not initialize any queues yet; requires a physical device and device to set them up.
     */
    VkQueueManager();

    /**
     * @brief Destructor for cleaning up any Vulkan resources (if necessary).
     */
    ~VkQueueManager();

    /**
     * @brief Sets up the graphics queue for the provided physical device and logical device.
     *
     * @param physicalDevice The Vulkan physical device to query for queue families.
     * @param device The Vulkan logical device used to retrieve the queue.
     */
    void setupQueue(VkPhysicalDevice physicalDevice, VkDevice device, const VkQueueFlags flags);

    /**
     * @brief Sets up the graphics queue for the provided physical device and logical device.
     *
     * @param physicalDevice The Vulkan physical device to query for queue families.
     * @param flags The Vulkan flags used to queue capatibility.
     * @param queuePriority The Vulkan priority distributed for each queue.
     * @return uint32_t Family index of the queue, that represents it's capatibility.
     */
    uint32_t pushQueueInfo(VkPhysicalDevice physicalDevice, const VkQueueFlags flags, const float queuePriority);

    /**
     * @brief Retrieves the Vulkan queue based on the provided queue flag (e.g., VK_QUEUE_GRAPHICS_BIT).
     *
     * @param flags The Vulkan queue flags (e.g., VK_QUEUE_GRAPHICS_BIT).
     * @return VkQueue The Vulkan queue associated with the given flags.
     */
    VkQueue getQueue(VkQueueFlags flags) const;

    /**
     * @brief Retrieves the Vulkan queues infos created in this application
     *
     * @return std::vector<VkDeviceQueueCreateInfo>& The Vulkan queues infos vector.
     */
    std::vector<VkDeviceQueueCreateInfo>& getVkDeviceQueueCreateInfos();

private:
    /**
     * @brief Finds a queue family that supports the required queue operations (e.g., graphics).
     *
     * @param physicalDevice The physical device to inspect.
     * @param flags The required queue capabilities (e.g., VK_QUEUE_GRAPHICS_BIT).
     * @return uint32_t The index of the queue family that supports the requested operations, or -1 if none found.
     */
    uint32_t findQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags flags);

    /**
     * @brief Holds information about a Vulkan queue.
     *
     * This struct stores data related to a Vulkan queue, including the queue itself,
     * the index of the queue family it belongs to, the number of queues requested from the family,
     * and the priority of the queue.
     */
    struct QueueData {
        VkQueue queue;
        uint32_t familyIndex;
        uint32_t queueCount;
        float queuePriority;
    };

    std::unordered_map<VkQueueFlags, QueueData> _mapVkQueues; ///< Stores Vulkan queues data by their capatibilities.

    std::vector<VkDeviceQueueCreateInfo> _vkDeviceQueueCreateInfos; ///< Stores Vulkan queues infos.
};

#endif // VKQUEUEMANAGER_H
