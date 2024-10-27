#include "VkSurfaceManager.h"
#include <GLFW/glfw3.h>

#include "VkException/VkException.h"

VkSurfaceManager::VkSurfaceManager(VkInstance* vkInstance)
    : _vkInstance(vkInstance)
{
    // eMPTY
}

VkSurfaceManager::~VkSurfaceManager() {
    vkDestroySurfaceKHR(*_vkInstance, _vkSurface, nullptr);
}

VkSurfaceKHR* VkSurfaceManager::getSurface()
{
    return &_vkSurface;
}
