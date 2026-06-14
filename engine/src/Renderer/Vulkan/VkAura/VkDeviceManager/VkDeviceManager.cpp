#include "aura/Renderer/Vulkan/VkAura/VkDeviceManager/VkDeviceManager.h"

#include <set>

#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkDeviceManager::VkDeviceManager(VkInstance* vkInstance, VkDeviceData vkDeviceData)
    : _vkInstance(vkInstance), _vkDeviceCreationData(std::move(vkDeviceData)), _deviceInfo(),
    _device(VK_NULL_HANDLE), _physicalDevice(VK_NULL_HANDLE),
    _physicaldeviceCount(0), _vkQueueManager(VkQueueManager())
{
    _setBestDevice(*_vkInstance);
}

VkDeviceManager::~VkDeviceManager() {
    if (_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(_device);
        vkDestroyDevice(_device, nullptr);
        _device = VK_NULL_HANDLE;
    }
    _vkInstance = nullptr;
    INK_DEBUG << "VkDeviceManager deleted";
}

void VkDeviceManager::_setBestDevice(VkInstance vkInstance)
{
    VkResult result = vkEnumeratePhysicalDevices(vkInstance, &_physicaldeviceCount, nullptr);
    if (result != VK_SUCCESS || _physicaldeviceCount == 0) {
        throw AuraException(result != VK_SUCCESS ? result : VK_ERROR_INITIALIZATION_FAILED);
    }

    std::vector<VkPhysicalDevice> devices(_physicaldeviceCount);
    result = vkEnumeratePhysicalDevices(vkInstance, &_physicaldeviceCount, devices.data());
    VK_RESULT_CHECK(result);

    INK_VERBOSE << "Found " << _physicaldeviceCount << " Vulkan physical device(s).";

    u32 bestScore = 0;

    for (const VkPhysicalDevice& device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

        INK_VERBOSE << "-------------------------------------------------------------------------------";
        INK_VERBOSE << "Device Name: " << deviceProperties.deviceName;
        INK_VERBOSE << "API Version: " << VK_VERSION_MAJOR(deviceProperties.apiVersion) << "."
                  << VK_VERSION_MINOR(deviceProperties.apiVersion) << "."
                  << VK_VERSION_PATCH(deviceProperties.apiVersion);
        INK_VERBOSE << "Driver Version: " << deviceProperties.driverVersion;
        INK_VERBOSE << "Vendor ID: " << deviceProperties.vendorID;
        INK_VERBOSE << "Device ID: " << deviceProperties.deviceID;
        INK_VERBOSE << "Device Type: " << deviceProperties.deviceType;

        // Scoring the device
        u32 score = 0;

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

        INK_VERBOSE << "Score for this device: " << score;

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

    INK_VERBOSE << "-------------------------------------------------------------------------------";
    INK_VERBOSE << "Choosed device score: " << bestScore;
    INK_VERBOSE << "-------------------------------------------------------------------------------";

    result = _checkDeviceExtensionSupport(_vkDeviceCreationData.vkDeviceExtensions);
    VK_RESULT_CHECK(result);

    if (_vkDeviceCreationData.exclusiveQueueFlags != 0) {
        _vkQueueManager.pushQueueInfo(_physicalDevice, _vkDeviceCreationData.exclusiveQueueFlags, 1.0f);
    }

    f32 priority = 0.95f;
    for (const VkQueueFlags& f : _vkDeviceCreationData.concurrentQueueFlags) {
        _vkQueueManager.pushQueueInfo(_physicalDevice, f, priority);
        priority -= 0.05f;
    }

    const std::vector<VkDeviceQueueCreateInfo> vkDeviceQueueCreateInfos = _vkQueueManager.getDeviceQueueCreateInfos();
    _deviceInfo.pNext = nullptr;
    _deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    _deviceInfo.queueCreateInfoCount = static_cast<u32>(vkDeviceQueueCreateInfos.size());
    _deviceInfo.pQueueCreateInfos = vkDeviceQueueCreateInfos.data();
    _deviceInfo.enabledLayerCount = static_cast<u32>(_vkDeviceCreationData.vkEnabledLayers.size());
    _deviceInfo.ppEnabledLayerNames = _vkDeviceCreationData.vkEnabledLayers.data();
    _deviceInfo.enabledExtensionCount = static_cast<u32>(_vkDeviceCreationData.vkDeviceExtensions.size());
    _deviceInfo.ppEnabledExtensionNames = _vkDeviceCreationData.vkDeviceExtensions.data();
    _deviceInfo.pEnabledFeatures = &_deviceFeatures;

    result = vkCreateDevice(_physicalDevice, &_deviceInfo, nullptr, &_device);
    VK_RESULT_CHECK(result);

    u32 familyIndex = _vkQueueManager.findQueueFamilyIndex(_physicalDevice, _vkDeviceCreationData.exclusiveQueueFlags);
    _vkQueueManager.setupQueue(_device, familyIndex, _vkDeviceCreationData.exclusiveQueueFlags);
    for (const VkQueueFlags& f : _vkDeviceCreationData.concurrentQueueFlags) {
        u32 familyIndex = _vkQueueManager.findQueueFamilyIndex(_physicalDevice, f);
        _vkQueueManager.setupQueue(_device, familyIndex, f);
    }

    INK_VERBOSE << "Logical Vulkan device created successfully.";
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
    u32 extensionCount = 0;
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
}
