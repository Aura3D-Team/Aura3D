#ifndef VKQUEUEMANAGER_H
#define VKQUEUEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <set>
#include <cstdint>

// Custom hash function for std::pair<uint32_t, VkQueueFlags>
// so that it can be used as a key in std::unordered_map.
namespace std {
    template <>
    struct hash<std::pair<uint32_t, VkQueueFlags>> {
        std::size_t operator()(const std::pair<uint32_t, VkQueueFlags>& p) const noexcept {
            return std::hash<uint32_t>{}(p.first) ^ (std::hash<VkQueueFlags>{}(p.second) << 1);
        }
    };
}

namespace aura3d {

/**
 * @brief Holds information about a Vulkan queue.
 *
 * This struct stores data related to a Vulkan queue, including the queues themselves,
 * a map of queue priorities per queue family index, and the corresponding
 * VkDeviceQueueCreateInfo structure.
 */
struct QueueData {
    std::vector<VkQueue> queues;
    std::vector<float> queuePriorities;
    VkDeviceQueueCreateInfo vkDeviceQueueCreateInfo{};
};

/**
 * @class VkQueueManager
 *
 * @brief Manages Vulkan device queues.
 *
 * This class is responsible for managing Vulkan queues by storing information in a map keyed by
 * a pair of (queue family index, queue flags). It allows you to register queue requirements,
 * retrieve queue creation information, and later set up the actual VkQueue handles.
 */
class VkQueueManager
{
public:
    /**
     * @brief Constructor for the queue manager.
     *
     * The manager is initialized empty. Queue registration must be done via pushQueueInfo()
     * before calling setupQueue().
     */
    VkQueueManager();

    /**
     * @brief Destructor.
     *
     * Cleans up any resources if necessary.
     */
    ~VkQueueManager();

    /**
     * @brief Sets up queues for a specific queue family.
     *
     * This method retrieves VkQueue handles from the logical device based on the
     * provided queue family index and queue flags. It populates the corresponding QueueData.
     *
     * @param device The Vulkan logical device used to retrieve the queues.
     * @param queueFamilyIndex The family index for which the queues were registered.
     * @param flags The Vulkan queue flags (e.g. VK_QUEUE_GRAPHICS_BIT) corresponding to the queue.
     *
     * @throws VkException if no QueueData is registered for the specified key.
     */
    void setupQueue(VkDevice device, uint32_t queueFamilyIndex, const VkQueueFlags flags);

    /**
     * @brief Registers a queue requirement.
     *
     * This function registers a queue requirement for the given physical device,
     * queue flags, and queue priority. If a QueueData for the (queueFamilyIndex, flags)
     * combination does not exist, it is created. Otherwise, the priority is added to
     * the existing QueueData.
     *
     * @param physicalDevice The Vulkan physical device to query for queue families.
     * @param flags The Vulkan queue capability flags (e.g. VK_QUEUE_GRAPHICS_BIT).
     * @param queuePriority The priority value for the queue (between 0.0 and 1.0).
     *
     * @return uint32_t The queue family index selected for the requested capabilities.
     *
     * @throws VkException if no suitable queue family is found.
     */
    uint32_t pushQueueInfo(VkPhysicalDevice physicalDevice, const VkQueueFlags flags, const float queuePriority);

    /**
     * @brief Finds a queue family index that supports the specified queue operations.
     *
     * The function inspects the available queue families of the provided physical device and
     * returns the index of the first queue family that meets the specified requirements.
     * If a presentation surface is provided, the queue family must also support presentation.
     *
     * @param physicalDevice The Vulkan physical device to inspect.
     * @param flags The required queue capabilities (e.g. VK_QUEUE_GRAPHICS_BIT).
     * @param surface (Optional) A Vulkan surface. If not VK_NULL_HANDLE, the queue family must support presentation.
     *
     * @return uint32_t The index of a suitable queue family, or UINT32_MAX if none is found.
     */
    static uint32_t findQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags flags, VkSurfaceKHR surface = VK_NULL_HANDLE);

    /**
     * @brief Finds all queue family indices that support the specified operations.
     *
     * This function returns a vector of all queue family indices that support the given
     * queue capabilities. If a presentation surface is provided, only indices that also
     * support presentation are returned.
     *
     * @param physicalDevice The Vulkan physical device to inspect.
     * @param flags The required queue capabilities.
     * @param surface (Optional) A Vulkan surface.
     *
     * @return std::vector<uint32_t> A vector of matching queue family indices.
     */
    static std::vector<uint32_t> findQueueFamilyIndices(VkPhysicalDevice physicalDevice, VkQueueFlags flags, VkSurfaceKHR surface = VK_NULL_HANDLE);

    /**
     * @brief Retrieves the properties of all queue families for a physical device.
     *
     * This function queries the Vulkan API to obtain properties of all queue families supported
     * by the provided physical device.
     *
     * @param physicalDevice The Vulkan physical device to query.
     * @return std::vector<VkQueueFamilyProperties> A vector containing properties for each queue family.
     */
    static std::vector<VkQueueFamilyProperties> findQueueFamilies(VkPhysicalDevice physicalDevice);

    /**
     * @brief Checks whether a specific queue family supports presentation.
     *
     * This function queries if the given queue family index supports presentation to the specified surface.
     *
     * @param physicalDevice The Vulkan physical device.
     * @param queueFamilyIndex The index of the queue family.
     * @param surface The Vulkan surface.
     *
     * @return true if presentation is supported; false otherwise.
     */
    static bool isPresentQueueSupported(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface);

    /**
     * @brief Retrieves the registered QueueData pointers that match the specified queue flags.
     *
     * This function returns pointers to the QueueData entries whose queue flags match
     * (bitwise) the specified flags.
     *
     * @param flags The Vulkan queue flags to match.
     * @return std::vector<QueueData*> A vector of pointers to matching QueueData.
     */
    std::vector<QueueData*> getQueues(VkQueueFlags flags);

    /**
     * @brief Gets the number of registered queue groups.
     *
     * @return uint32_t The number of entries in the queue map.
     */
    uint32_t getQueueMapSize() const;

    /**
     * @brief Retrieves the vector of VkDeviceQueueCreateInfo structures.
     *
     * These structures are used during logical device creation.
     *
     * @return std::vector<VkDeviceQueueCreateInfo> A vector of VkDeviceQueueCreateInfo.
     */
    std::vector<VkDeviceQueueCreateInfo> getDeviceQueueCreateInfos() const;

    static void submitCmdIntoQueue(VkQueue queue,
                                   VkCommandBuffer* commandBuffer,
                                   VkSemaphore* imageAvailableSemaphore,
                                   VkSemaphore* renderFinishedSemaphore,
                                   VkFence fence);

private:
    // The key is a pair consisting of (queueFamilyIndex, VkQueueFlags).
    // This allows multiple queue families (with different indices) that satisfy the same flag requirement.
    std::unordered_map<std::pair<uint32_t, VkQueueFlags>, QueueData> _mapVkQueues;
};

}

#endif // VKQUEUEMANAGER_H
