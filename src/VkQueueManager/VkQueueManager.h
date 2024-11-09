#ifndef VKQUEUEMANAGER_H
#define VKQUEUEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <set>

/**
 * @brief Holds information about a Vulkan queue.
 *
 * This struct stores data related to a Vulkan queue, including the queue itself,
 * the index of the queue family it belongs to, the number of queues requested from the family,
 * and the priority of the queue.
 */
struct QueueData {
    std::vector<VkQueue> queues;
    std::vector<float> queuesPriorities;
    VkDeviceQueueCreateInfo vkDeviceQueueCreateInfo;
};

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
     * @brief Finds a queue family index that supports the specified queue operations (e.g., graphics).
     *
     * This function inspects the available queue families of the given physical device and returns
     * the index of the first queue family that meets the specified requirements (based on flags).
     *
     * @param physicalDevice The Vulkan physical device to inspect for supported queue families.
     * @param flags The required queue capabilities (e.g., VK_QUEUE_GRAPHICS_BIT).
     * @param surface To check if a queue family supports presentation
     *
     * @return uint32_t The index of the queue family that supports the requested operations, or UINT32_MAX if none are found.
     */
    static uint32_t findQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags flags, VkSurfaceKHR surface = VK_NULL_HANDLE);

    /**
     * @brief Retrieves the properties of all queue families for a given physical device.
     *
     * This function queries the Vulkan API to get the number and properties of all queue families
     * supported by the specified physical device.
     *
     * @param physicalDevice The Vulkan physical device to inspect.
     * @return std::vector<VkQueueFamilyProperties> A vector containing properties for each queue family supported by the physical device.
     */
    static std::vector<VkQueueFamilyProperties> findQueueFamilies(VkPhysicalDevice physicalDevice);

    static bool isPresentQueueSupported(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface);

    /**
     * @brief Retrieves the Vulkan queue reference based on the provided queue flag (e.g., VK_QUEUE_GRAPHICS_BIT).
     *
     * @param flags The Vulkan queue flags (e.g., VK_QUEUE_GRAPHICS_BIT).
     * @return VkQueue The Vulkan queue associated with the given flags.
     */
    QueueData* getQueueData(VkQueueFlags flags);

    uint32_t getQueueMapSize() const;

    /**
     * @brief Retrieves the Vulkan queues infos created in this application
     *
     * @return std::vector<VkDeviceQueueCreateInfo> The Vulkan queues infos vector.
     */
    std::vector<VkDeviceQueueCreateInfo> getDeviceQueueCreateInfos() const;

private:
    std::unordered_map<VkQueueFlags, QueueData> _mapVkQueues; ///< Stores Vulkan queues data by their capatibilities.
};

#endif // VKQUEUEMANAGER_H
