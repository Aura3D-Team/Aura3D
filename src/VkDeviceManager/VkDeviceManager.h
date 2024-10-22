#ifndef VKDEVICEMANAGER_H
#define VKDEVICEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "../VkQueueManager/VkQueueManager.h"

/**
 * @class VkDeviceManager
 *
 * @brief Manages Vulkan physical and logical devices.
 *
 * This class is responsible for selecting the best available physical device,
 * creating a logical device, and managing device resources. It integrates with
 * Vulkan by taking a `VkInstance` and working with it to enumerate and choose
 * the best physical device available.
 */
class VkDeviceManager
{
public:
    /**
     * @brief Constructor that initializes the device manager with a Vulkan instance.
     *
     * @param vkInstance The Vulkan instance to use for device management.
     */
    VkDeviceManager();

    /**
     * @brief Destructor that cleans up the logical device.
     *
     * Cleans up and destroys the Vulkan logical device created by the manager.
     */
    ~VkDeviceManager();

    /**
     * @brief Sets the best physical and logical device.
     *
     * This function enumerates the physical devices available and selects the best one
     * based on the required criteria (e.g., queue family support). It then creates a logical
     * device from the selected physical device.
     *
     * Throws a `VkException` if any Vulkan operation fails during device selection or creation.
     */
    void setBestDevice(VkInstance vkInstance);

    /**
     * @brief Retrieves the Vulkan logical device.
     *
     * This method returns the Vulkan logical device (`VkDevice`) created by the manager,
     * which is used to perform operations such as drawing, computing, and rendering.
     * The logical device is configured with the specific queues needed for these operations.
     *
     * @return VkDevice The Vulkan logical device that has been created with specific queue configurations.
     */
        VkDevice getDevice();

    /**
     * @brief Retrieves the Vulkan physical device.
     *
     * This method returns the Vulkan physical device (`VkPhysicalDevice`) that was selected and used
     * to create the logical device. The physical device represents the actual GPU or hardware
     * selected based on the criteria defined in the `setBestDevice` method.
     *
     * @return VkPhysicalDevice The Vulkan physical device that was chosen to create the logical device with specific queues.
     */
    VkPhysicalDevice getPhysicalDevice();


private:
    VkDeviceCreateInfo _deviceInfo;  ///< Information required to create the logical device
    VkDevice _device;  ///< Handle to the Vulkan logical device
    VkPhysicalDevice _physicalDevice;  ///< Handle to the selected Vulkan physical device
    uint32_t _physicaldeviceCount;  ///< Number of available physical devices

    VkQueueManager _vkQueues; ///< Queue manager to create queues in a organized way.
};

#endif // VKDEVICEMANAGER_H
