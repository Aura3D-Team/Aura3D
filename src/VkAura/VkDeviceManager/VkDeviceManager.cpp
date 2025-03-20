#include "VkDeviceManager.h"

#include <aura.hpp>
#include "AuraException/AuraException.h"
#include "AuraLogger/AuraLogger.h"

namespace aura3d {

VkDeviceManager::VkDeviceManager(VkHostAllocator* vkHostAllocator, VkInstance* vkInstance, VkDeviceData vkDeviceData)
    : vkHostAllocator(vkHostAllocator), _vkInstance(vkInstance), _physicaldeviceCount(0), _deviceInfo(),
    _physicalDevice(VK_NULL_HANDLE), _device(VK_NULL_HANDLE),
    _vkQueueManager(VkQueueManager()), _vkDeviceCreationData(std::move(vkDeviceData))
{
    _setBestDevice(*_vkInstance);
}

VkDeviceManager::~VkDeviceManager() {
    if (_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(_device);
        vkDestroyDevice(_device, vkHostAllocator->getCallbacks());
        _device = VK_NULL_HANDLE;
    }
    _vkInstance = nullptr;
    AURA_DEBUG << "VkDeviceManager deleted";
}

void VkDeviceManager::_setBestDevice(VkInstance vkInstance)
{
    // Enumerate physical devices
    VkResult result = vkEnumeratePhysicalDevices(vkInstance, &_physicaldeviceCount, nullptr);
    if (result != VK_SUCCESS || _physicaldeviceCount == 0) {
        throw AuraException(result != VK_SUCCESS ? result : VK_ERROR_INITIALIZATION_FAILED);
    }

    std::vector<VkPhysicalDevice> devices(_physicaldeviceCount);
    result = vkEnumeratePhysicalDevices(vkInstance, &_physicaldeviceCount, devices.data());
    VK_RESULT_CHECK(result);

    AURA_VERBOSE << "Found " << _physicaldeviceCount << " Vulkan physical device(s).";

    uint32_t bestScore = 0;

    // Select the best physical device
    for (const VkPhysicalDevice& device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

        AURA_VERBOSE << "-------------------------------------------------------------------------------";
        AURA_VERBOSE << "Device Name: " << deviceProperties.deviceName;
        AURA_VERBOSE << "API Version: " << VK_VERSION_MAJOR(deviceProperties.apiVersion) << "."
                  << VK_VERSION_MINOR(deviceProperties.apiVersion) << "."
                  << VK_VERSION_PATCH(deviceProperties.apiVersion);
        AURA_VERBOSE << "Driver Version: " << deviceProperties.driverVersion;
        AURA_VERBOSE << "Vendor ID: " << deviceProperties.vendorID;
        AURA_VERBOSE << "Device ID: " << deviceProperties.deviceID;
        AURA_VERBOSE << "Device Type: " << deviceProperties.deviceType;

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

        AURA_VERBOSE << "Score for this device: " << score;

        if (score > bestScore) {
            bestScore = score;
            _physicalDevice = device;
            _deviceProperties = deviceProperties;
            _deviceFeatures = deviceFeatures;
        }
    }

    if (_physicalDevice == VK_NULL_HANDLE) {
        throw AuraException("No suitable physical device found.");
    }

    AURA_VERBOSE << "-------------------------------------------------------------------------------";
    AURA_VERBOSE << "Choosed device score: " << bestScore;
    AURA_VERBOSE << "-------------------------------------------------------------------------------";

    result = _checkDeviceExtensionSupport(_vkDeviceCreationData.vkDeviceExtensions);
    VK_RESULT_CHECK(result);

    if (_vkDeviceCreationData.exclusiveQueueFlags != 0) {
        _vkQueueManager.pushQueueInfo(_physicalDevice, _vkDeviceCreationData.exclusiveQueueFlags, 1.0f);
    }

    float priority = 0.95f;
    for (const VkQueueFlags& f : _vkDeviceCreationData.concurrentQueueFlags) {
        _vkQueueManager.pushQueueInfo(_physicalDevice, f, priority);
        priority -= 0.05f;
    }

    const std::vector<VkDeviceQueueCreateInfo> vkDeviceQueueCreateInfos = _vkQueueManager.getDeviceQueueCreateInfos();
    _deviceInfo.pNext = nullptr;
    _deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    _deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(vkDeviceQueueCreateInfos.size());
    _deviceInfo.pQueueCreateInfos = vkDeviceQueueCreateInfos.data();
    _deviceInfo.enabledLayerCount = static_cast<uint32_t>(_vkDeviceCreationData.vkEnabledLayers.size());
    _deviceInfo.ppEnabledLayerNames = _vkDeviceCreationData.vkEnabledLayers.data();
    _deviceInfo.enabledExtensionCount = static_cast<uint32_t>(_vkDeviceCreationData.vkDeviceExtensions.size());
    _deviceInfo.ppEnabledExtensionNames = _vkDeviceCreationData.vkDeviceExtensions.data();
    _deviceInfo.pEnabledFeatures = &_deviceFeatures;

    result = vkCreateDevice(_physicalDevice, &_deviceInfo, vkHostAllocator->getCallbacks(), &_device);
    VK_RESULT_CHECK(result);

    uint32_t familyIndex = _vkQueueManager.findQueueFamilyIndex(_physicalDevice, _vkDeviceCreationData.exclusiveQueueFlags);
    _vkQueueManager.setupQueue(_device, familyIndex, _vkDeviceCreationData.exclusiveQueueFlags);
    for (const VkQueueFlags& f : _vkDeviceCreationData.concurrentQueueFlags) {
        uint32_t familyIndex = _vkQueueManager.findQueueFamilyIndex(_physicalDevice, f);
        _vkQueueManager.setupQueue(_device, familyIndex, f);
    }

    AURA_VERBOSE << "Logical Vulkan device created successfully.";
}

VkDevice* VkDeviceManager::getDevice()
{
    return &_device;
}

VkPhysicalDevice* VkDeviceManager::getPhysicalDevice()
{
    return &_physicalDevice;
}

VkDeviceData* VkDeviceManager::getDeviceCreationData()
{
    return &_vkDeviceCreationData;
}

VkQueueManager* VkDeviceManager::getQueueManager()
{
    return &_vkQueueManager;
}

VkBool32 VkDeviceManager::physicalDeviceHasQueueSurfaceSupport(VkSurfaceManager vkSurfaceManager, const VkQueueFlags flags) {
    return vkSurfaceManager.getQueuePhysicalDeviceSurfaceSupport(
        _physicalDevice, _vkQueueManager.getQueues(flags).front()->vkDeviceQueueCreateInfo.queueFamilyIndex);
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

}
