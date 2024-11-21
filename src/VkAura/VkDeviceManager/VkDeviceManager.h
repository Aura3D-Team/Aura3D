#ifndef VKDEVICEMANAGER_H
#define VKDEVICEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

#include "VkAura/VkQueueManager/VkQueueManager.h"
#include "VkAura/VkSurfaceManager/VkSurfaceManager.h"

/**
 * @brief This struct represents Important data for VkDevice creation
 *
 * Obs: for more details, it can have more parameters in the future
 */
struct VkDeviceData {
    std::vector<const char*> vkDeviceExtensions;
    std::vector<const char*> vkEnabledLayers;

    std::vector<VkQueueFlags> concurrentQueueFlags;
    std::vector<VkQueueFlags> exclusiveQueueFlags;
};

/**
 * @class VkDeviceManager
 *
 * @brief Manages Vulkan physical and logical devices.
 *
 * This class is responsible for selecting the best available physical device,
 * creating a logical device, and managing device resources. It integrates with
 * Vulkan instance by taking a `VkInstance` and working with it to enumerate and choose
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
    VkDeviceManager(VkInstance* vkInstance, VkDeviceData vkDeviceData);

    /**
     * @brief Destructor that cleans up the logical device.
     *
     * Cleans up and destroys the Vulkan logical device created by the manager.
     */
    ~VkDeviceManager();

    /**
     * @brief Retrieves a pointer to Vulkan logical device.
     *
     * This method returns the Vulkan logical device (`VkDevice`) created by the manager,
     * which is used to perform operations such as drawing, computing, and rendering.
     * The logical device is configured with the specific queues needed for these operations.
     *
     * @return VkDevice The Vulkan logical device that has been created with specific queue configurations.
     */
    VkDevice* getDevice();

    /**
     * @brief Retrieves a pointer to the Vulkan physical device.
     *
     * This method returns the Vulkan physical device (`VkPhysicalDevice`) that was selected and used
     * to create the logical device. The physical device represents the actual GPU or hardware
     * selected based on the criteria defined in the `setBestDevice` method.
     *
     * @return VkPhysicalDevice The Vulkan physical device that was chosen to create the logical device with specific queues.
     */
    VkPhysicalDevice* getPhysicalDevice();

    VkDeviceData* getDeviceCreationData();

    /**
     * @brief See if physical device supports Vulkan surface operations
     * @param vkSurfaceManager The VkSurfaceManager
     * @param flags The VkQueueFlags
     * @return VkBool32 true if physical device supports Vulkan surface operations
     */
    VkBool32 physicalDeviceHasQueueSurfaceSupport(VkSurfaceManager vkSurfaceManager, const VkQueueFlags flags);

    VkQueueManager* getQueueManager();

private:
    VkInstance* _vkInstance; ///< Vulkan instance pointer used bind the best device

    VkDeviceData _vkDeviceCreationData; ///< This struct represents Important data for VkDevice creation
    VkDeviceCreateInfo _deviceInfo;  ///< Information required to create the logical device
    VkDevice _device;  ///< Handle to the Vulkan logical device

    VkPhysicalDevice _physicalDevice;  ///< Handle to the selected Vulkan physical device
    VkPhysicalDeviceProperties _deviceProperties; ///< Store best physical device properties
    VkPhysicalDeviceFeatures _deviceFeatures; ///< Store best physical device features
    uint32_t _physicaldeviceCount;  ///< Number of available physical devices

    VkQueueManager _vkQueueManager; ///< Queue manager to create queues in a organized way.

    /**
     * @brief Sets the best physical and logical device.
     *
     * This function enumerates the physical devices available and selects the best one
     * based on the required criteria (e.g., queue family support). It then creates a logical
     * device from the selected physical device.
     *
     * @param vkInstance Vulkan instance to set the device
     *
     * Throws a `VkException` if any Vulkan operation fails during device selection or creation.
     */
    void _setBestDevice(VkInstance vkInstance);

    /**
     * @brief Check extension support for the the physical device.
     *
     * @param exts Extensions supposed to be used for the final logic device.
     * @return VkResult Result format for vulkan error code.
     */
    VkResult _checkDeviceExtensionSupport(std::vector<const char*> exts) const;
};

#endif // VKDEVICEMANAGER_H
