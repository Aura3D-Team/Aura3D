#include "VkDeviceManager.h"
#include "plog/Log.h"

#include "VkException/VkException.h"

VkDeviceManager::VkDeviceManager(VkInstance* vkInstance, VkDeviceData vkDeviceData)
    : _vkInstance(vkInstance), _physicaldeviceCount(0), _deviceInfo(),
    _physicalDevice(VK_NULL_HANDLE), _device(VK_NULL_HANDLE),
    _vkQueueManager(VkQueueManager())
{
    _setBestDevice(*_vkInstance, vkDeviceData);
}

VkDeviceManager::~VkDeviceManager() {
    if (_device != VK_NULL_HANDLE) {
        vkDestroyDevice(_device, nullptr);
        _device = VK_NULL_HANDLE;
    }
    _vkInstance = nullptr;
}

void VkDeviceManager::_setBestDevice(VkInstance vkInstance, VkDeviceData vkDeviceData)
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

    uint32_t bestScore = 0;

    // Select the best physical device
    for (const VkPhysicalDevice& device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

        PLOG_INFO << "-------------------------------------------------------------------------------";
        PLOG_INFO << "Device Name: " << deviceProperties.deviceName;
        PLOG_INFO << "API Version: " << VK_VERSION_MAJOR(deviceProperties.apiVersion) << "."
                  << VK_VERSION_MINOR(deviceProperties.apiVersion) << "."
                  << VK_VERSION_PATCH(deviceProperties.apiVersion);
        PLOG_INFO << "Driver Version: " << deviceProperties.driverVersion;
        PLOG_INFO << "Vendor ID: " << deviceProperties.vendorID;
        PLOG_INFO << "Device ID: " << deviceProperties.deviceID;
        PLOG_INFO << "Device Type: " << deviceProperties.deviceType;

        // Scoring the device
        uint32_t score = 0;

        // Prefer discrete GPUs (dedicated graphics cards)
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        if (deviceFeatures.geometryShader) {
            score += 1000;
        }

        score += deviceProperties.apiVersion;

        score += deviceProperties.limits.maxImageDimension1D;
        score += deviceProperties.limits.maxImageDimension2D;
        score += deviceProperties.limits.maxImageDimension3D;

        PLOG_INFO << "Score for this device: " << score;

        if (score > bestScore) {
            bestScore = score;
            _physicalDevice = device;
            _deviceProperties = deviceProperties;
            _deviceFeatures = deviceFeatures;
        }
    }

    if (_physicalDevice == VK_NULL_HANDLE) {
        throw VkException("No suitable physical device found.");
    }

    PLOG_INFO << "-------------------------------------------------------------------------------";
    PLOG_INFO << "Choosed device score: " << bestScore;
    PLOG_INFO << "-------------------------------------------------------------------------------";

    result = _checkDeviceExtensionSupport(vkDeviceData.vkDeviceExtensions);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    uint32_t graphicsQueueFamilyIndex = _vkQueueManager.pushQueueInfo(_physicalDevice, VK_QUEUE_GRAPHICS_BIT, 1.0);

    const std::vector<VkDeviceQueueCreateInfo> vkDeviceQueueCreateInfos = _vkQueueManager.getDeviceQueueCreateInfos();
    _deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    _deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(vkDeviceQueueCreateInfos.size());
    _deviceInfo.pQueueCreateInfos = vkDeviceQueueCreateInfos.data();
    _deviceInfo.enabledLayerCount = static_cast<uint32_t>(vkDeviceData.vkEnabledLayers.size());
    _deviceInfo.ppEnabledLayerNames = vkDeviceData.vkEnabledLayers.data();
    _deviceInfo.enabledExtensionCount = static_cast<uint32_t>(vkDeviceData.vkDeviceExtensions.size());
    _deviceInfo.ppEnabledExtensionNames = vkDeviceData.vkDeviceExtensions.data();
    _deviceInfo.pEnabledFeatures = &_deviceFeatures;

    result = vkCreateDevice(_physicalDevice, &_deviceInfo, nullptr, &_device);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    _vkQueueManager.setupQueue(_physicalDevice, _device, VK_QUEUE_GRAPHICS_BIT);

    PLOG_INFO << "Logical Vulkan device created successfully.";
}

VkDevice VkDeviceManager::getDevice()
{
    return _device;
}

VkPhysicalDevice* VkDeviceManager::getPhysicalDevice()
{
    return &_physicalDevice;
}

VkBool32 VkDeviceManager::physicalDeviceHasQueueSurfaceSupport(VkSurfaceManager vkSurfaceManager, const VkQueueFlags flags) {
    return vkSurfaceManager.getQueuePhysicalDeviceSurfaceSupport(
        _physicalDevice, _vkQueueManager.getQueueData(flags)->vkDeviceQueueCreateInfo.queueFamilyIndex);
}

VkResult VkDeviceManager::_checkDeviceExtensionSupport(std::vector<const char*> exts) const {
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(exts.begin(), exts.end());

    for (const VkExtensionProperties& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty() ? VK_SUCCESS : VK_ERROR_EXTENSION_NOT_PRESENT;
}
