#include "VkDeviceManager.h"
#include "VkException/VkException.h"
#include "plog/Log.h"

VkDeviceManager::VkDeviceManager()
    : _physicaldeviceCount(0), _deviceInfo(),
    _physicalDevice(VK_NULL_HANDLE), _device(VK_NULL_HANDLE),
    _vkQueues(VkQueueManager())
{
    _deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
}

VkDeviceManager::~VkDeviceManager() {
    if (_device != VK_NULL_HANDLE) {
        vkDestroyDevice(_device, nullptr);
        PLOG_INFO << "Vulkan device destroyed.";
    }
}

void VkDeviceManager::setBestDevice(VkInstance vkInstance)
{
    // Enumerate physical devices
    VkResult result = vkEnumeratePhysicalDevices(vkInstance, &_physicaldeviceCount, nullptr);
    if (result != VK_SUCCESS || _physicaldeviceCount == 0) {
        throw VkException(result != VK_SUCCESS ? result : VK_ERROR_INITIALIZATION_FAILED);
    }

    std::vector<VkPhysicalDevice> devices(_physicaldeviceCount);
    result = vkEnumeratePhysicalDevices(vkInstance, &_physicaldeviceCount, devices.data());
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    PLOG_INFO << "Found " << _physicaldeviceCount << " Vulkan physical device(s).";

    int bestScore = -1;

    // Select the best physical device
    for (const VkPhysicalDevice& device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        PLOG_INFO << "Device Name: " << deviceProperties.deviceName;
        PLOG_INFO << "API Version: " << VK_VERSION_MAJOR(deviceProperties.apiVersion) << "."
                  << VK_VERSION_MINOR(deviceProperties.apiVersion) << "."
                  << VK_VERSION_PATCH(deviceProperties.apiVersion);
        PLOG_INFO << "Driver Version: " << deviceProperties.driverVersion;
        PLOG_INFO << "Vendor ID: " << deviceProperties.vendorID;
        PLOG_INFO << "Device ID: " << deviceProperties.deviceID;
        PLOG_INFO << "Device Type: " << deviceProperties.deviceType;

        // Scoring the device
        int score = 0;

        // Prefer discrete GPUs (dedicated graphics cards)
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        score += deviceProperties.apiVersion;

        score += deviceProperties.limits.maxImageDimension2D;

        PLOG_INFO << "Score for this device: " << score;

        if (score > bestScore) {
            bestScore = score;
            _physicalDevice = device;
        }
    }

    if (_physicalDevice == VK_NULL_HANDLE) {
        throw VkException("No suitable physical device found.");
    }

    uint32_t graphicsQueueFamilyIndex = _vkQueues.pushQueueInfo(_physicalDevice, VK_QUEUE_GRAPHICS_BIT, 1.0);

    _deviceInfo.queueCreateInfoCount = _vkQueues.getVkDeviceQueueCreateInfos().size();
    _deviceInfo.pQueueCreateInfos = _vkQueues.getVkDeviceQueueCreateInfos().data();

    result = vkCreateDevice(_physicalDevice, &_deviceInfo, nullptr, &_device);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    _vkQueues.setupQueue(_physicalDevice, _device, VK_QUEUE_GRAPHICS_BIT);

    PLOG_INFO << "Logical Vulkan device created successfully.";
}

VkDevice VkDeviceManager::getDevice()
{
    return _device;
}

VkPhysicalDevice VkDeviceManager::getPhysicalDevice()
{
    return _physicalDevice;
}
