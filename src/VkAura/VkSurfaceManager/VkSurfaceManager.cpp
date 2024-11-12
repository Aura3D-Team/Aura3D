#include "VkSurfaceManager.h"
#include "VkAura/VkException/VkException.h"

#include <plog/Log.h>

VkSurfaceManager::VkSurfaceManager(VkInstance* vkInstance, GLFWwindow* window)
    : _vkInstance(vkInstance)
{
    VkResult result = glfwCreateWindowSurface(*_vkInstance, window, nullptr, &_vkSurface);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }
}

VkSurfaceManager::~VkSurfaceManager() {
    if (_vkSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(*_vkInstance, _vkSurface, nullptr);
        PLOG_DEBUG << "VkSurface deleted";
    }
    _vkInstance = nullptr;
}

VkSurfaceKHR* VkSurfaceManager::getSurface()
{
    return &_vkSurface;
}

VkBool32 VkSurfaceManager::getQueuePhysicalDeviceSurfaceSupport(VkPhysicalDevice physicalDevice, const int familyIndex) {
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex, _vkSurface, &presentSupport);
    return presentSupport;
}
