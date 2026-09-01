#include "aura/Renderer/Vulkan/VkAura/VkInstanceManager/VkInstanceManager.h"

#include <set>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkDebugMode/VkCountingAllocator.h"

namespace aura3d {
namespace vk {

VkInstanceManager::VkInstanceManager(VkInstanceData vkInstanceData,
                                     bool enableValidationLayers)
    : _vkInstance(VK_NULL_HANDLE), _appInfo({}),
    _instanceInfo({}), _vkDebugger(nullptr),
    _vkValidationLayers(std::move(vkInstanceData.vkValidationLayers)), _vkInstanceExtensions(std::move(vkInstanceData.vkInstanceExtensions))
{
    initializeAppInfo(vkInstanceData);
    initializeInstanceInfo();

    VkResult vkDebuggerPreparationResult = prepareVkDebugger(enableValidationLayers);

    validateInstanceExtensions();
    validateValidationLayers(enableValidationLayers);

    //! Paired with the vkDestroyInstance in the destructor; see
    //! hostAllocationCallbacks() for why neither site is #ifdef'd.
    VK_RESULT_CHECK(vkCreateInstance(&_instanceInfo, hostAllocationCallbacks(), &_vkInstance));

    if (vkDebuggerPreparationResult == VK_SUCCESS) {
        createDebuggerInstance(&_debugCreateInfo);
    }
}

VkInstanceManager::~VkInstanceManager()
{
    _vkDebugger.reset();
    if (_vkInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(_vkInstance, hostAllocationCallbacks());
        _vkInstance = VK_NULL_HANDLE;
        INK_DEBUG << "VkInstance deleted";
    }
}

void VkInstanceManager::initializeAppInfo(const VkInstanceData& vkInstanceData) {
    _appInfo = {};
    _appInfo.pNext = nullptr;
    _appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    _appInfo.pApplicationName = vkInstanceData.appName;
    _appInfo.applicationVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
    _appInfo.pEngineName = vkInstanceData.engineName;
    _appInfo.engineVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
    _appInfo.apiVersion = kVulkanApiVersion;
}

void VkInstanceManager::initializeInstanceInfo() {
    _instanceInfo = {};
    _instanceInfo.pNext = nullptr;
    _instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    _instanceInfo.pApplicationInfo = &_appInfo;
}

void VkInstanceManager::validateInstanceExtensions() {
    VkResult result = _checkInstanceExtensionSupport(_vkInstanceExtensions);
    VK_RESULT_CHECK(result);

    _instanceInfo.enabledExtensionCount = static_cast<u32>(_vkInstanceExtensions.size());
    _instanceInfo.ppEnabledExtensionNames = _vkInstanceExtensions.data();
}

void VkInstanceManager::validateValidationLayers(bool enableValidationLayers) {
    if (!enableValidationLayers) 
    {
        _vkValidationLayers.clear();
        return;
    }

    VkResult result = _checkValidationLayerSupport(_vkValidationLayers);
    if (result != VK_SUCCESS) 
    {
        INK_WARN << "Requested validation layer(s) not present on this device "
                    "(VK_LAYER_KHRONOS_validation must be specially bundled into "
                    "an Android APK -- it isn't present system-wide like it is with "
                    "a desktop Vulkan SDK install); continuing without validation.";
        _vkValidationLayers.clear();
        return;
    }

    _instanceInfo.enabledLayerCount = static_cast<u32>(_vkValidationLayers.size());
    _instanceInfo.ppEnabledLayerNames = _vkValidationLayers.data();
}

VkResult VkInstanceManager::prepareVkDebugger(const bool enableValidationLayers)
{
    VkResult result = VK_NOT_READY;
    if (enableValidationLayers) {
        result = _checkInstanceExtensionSupport({VK_EXT_DEBUG_UTILS_EXTENSION_NAME});
        if (result == VK_SUCCESS) {
            _vkInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            _debugCreateInfo = VkDebugger::setupDebugMessenger();
            _instanceInfo.pNext = &_debugCreateInfo;
        } else {
            INK_WARN << "Warning: VK_EXT_DEBUG_UTILS_EXTENSION_NAME is not supported and will not be used.";
        }
    }
    return result;
}

void VkInstanceManager::createDebuggerInstance(VkDebugUtilsMessengerCreateInfoEXT* debugCreateInfo) {
    _vkDebugger = std::make_unique<VkDebugger>(&_vkInstance);
    VkResult result = _vkDebugger->createDebugUtilsMessengerEXT(debugCreateInfo, nullptr);
    VK_RESULT_CHECK(result);
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

VkResult VkInstanceManager::_checkValidationLayerSupport(const std::vector<const char*>& validationLayers) const
{
    u32 layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    u32 layerRequiredCount=0;
    for (const char* layerName : validationLayers) {
        for (const VkLayerProperties& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerRequiredCount++;
            }
        }
    }

    return layerRequiredCount == validationLayers.size() ? VK_SUCCESS : VK_ERROR_LAYER_NOT_PRESENT;
}

VkResult VkInstanceManager::_checkInstanceExtensionSupport(const std::vector<const char*>& exts) const {
    u32 extensionCount = 0;

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
    std::set<std::string> requiredExtensions(exts.begin(), exts.end());

    for (const VkExtensionProperties& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty() ? VK_SUCCESS : VK_ERROR_EXTENSION_NOT_PRESENT;
}

}
}
