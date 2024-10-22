#include "VkInstanceManager.h"
#include "VkException/VkException.h"

VkInstanceManager::VkInstanceManager(const char* appName, const char* engineName)
    : _appInfo({}), _instanceInfo({}), _vkInstance(VK_NULL_HANDLE)
{
    // App Info
    _appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    _appInfo.pApplicationName = appName;
    _appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    _appInfo.pEngineName = engineName;
    _appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    _appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

    // Instance Info config
    _instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    _instanceInfo.pApplicationInfo = &_appInfo;

    // Used extensions
    _vkInstanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME
    };

    _instanceInfo.enabledExtensionCount = static_cast<uint32_t>(_vkInstanceExtensions.size());
    _instanceInfo.ppEnabledExtensionNames = _vkInstanceExtensions.data();

    // Used validation layers
    _vkValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    _instanceInfo.enabledLayerCount = static_cast<uint32_t>(_vkValidationLayers.size());
    _instanceInfo.ppEnabledLayerNames = _vkValidationLayers.data();

    VkResult vkResult = vkCreateInstance(&_instanceInfo, nullptr, &_vkInstance);
    if (vkResult != VK_SUCCESS) throw VkException(vkResult);
}

VkInstance VkInstanceManager::getVkInstance()
{
    return _vkInstance;
}

VkInstanceManager::~VkInstanceManager()
{
    if (_vkInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(_vkInstance, nullptr);
    }
}
