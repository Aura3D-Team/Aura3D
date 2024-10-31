#include "VkInstanceManager.h"
#include <set>
#include <plog/Log.h>

#include "VkException/VkException.h"


VkInstanceManager::VkInstanceManager(VkInstanceData vkInstanceData)
    : _appInfo({}), _instanceInfo({}), _vkInstance(VK_NULL_HANDLE),
    _vkInstanceExtensions(vkInstanceData.vkInstanceExtensions), _vkValidationLayers(vkInstanceData.vkValidationLayers)
{
    VkResult result = _checkInstanceExtensionSupport(vkInstanceData.vkInstanceExtensions);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    // App Info
    _appInfo.pNext = nullptr;
    _appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    _appInfo.pApplicationName = vkInstanceData.appName;
    _appInfo.applicationVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
    _appInfo.pEngineName = vkInstanceData.engineName;
    _appInfo.engineVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
    _appInfo.apiVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);

    // Instance Info config
    _instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    _instanceInfo.pApplicationInfo = &_appInfo;

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

    // Debugger and validation layers
    if (enableValidationLayers) {
        _vkDebugger = std::make_unique<VkDebugger>(&_vkInstance);
        _appInfo.pNext = _vkDebugger->getVkDebugMessenger();

        _vkInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        _instanceInfo.enabledLayerCount = static_cast<uint32_t>(_vkValidationLayers.size());
        _instanceInfo.ppEnabledLayerNames = _vkValidationLayers.data();

        VkResult result = _vkDebugger->createDebugUtilsMessengerEXT(nullptr);
        if (result != VK_SUCCESS) {
            throw VkException(result);
        }
    } else {
        _instanceInfo.enabledLayerCount = 0;
    }

    // Extensions
    _instanceInfo.enabledExtensionCount = static_cast<uint32_t>(_vkInstanceExtensions.size());
    _instanceInfo.ppEnabledExtensionNames = _vkInstanceExtensions.data();

    _checkValidationLayerSupport(_vkValidationLayers);

    VkResult vkResult = vkCreateInstance(&_instanceInfo, nullptr, &_vkInstance);
    if (vkResult != VK_SUCCESS) throw VkException(vkResult);
}

VkInstance* VkInstanceManager::getVkInstance()
{
    return &_vkInstance;
}

VkApplicationInfo* VkInstanceManager::getAppInfo()
{
    return &_appInfo;
}

std::unique_ptr<VkDebugger>* VkInstanceManager::getVkDebugger()
{
    return &_vkDebugger;
}

bool VkInstanceManager::_checkValidationLayerSupport(const std::vector<const char*>& validationLayers) const
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    uint32_t layerRequiredCount=0;
    for (const char* layerName : validationLayers) {
        for (const VkLayerProperties& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerRequiredCount++;
            }
        }
    }

    return layerRequiredCount == validationLayers.size();
}

VkInstanceManager::~VkInstanceManager()
{
    if (_vkInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(_vkInstance, nullptr);
        _vkInstance = VK_NULL_HANDLE;
    }
}

VkResult VkInstanceManager::_checkInstanceExtensionSupport(const std::vector<const char*>& exts) const {
    uint32_t extensionCount = 0;

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
    std::set<std::string> requiredExtensions(exts.begin(), exts.end());

    for (const VkExtensionProperties& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty() ? VK_SUCCESS : VK_ERROR_EXTENSION_NOT_PRESENT;
}
