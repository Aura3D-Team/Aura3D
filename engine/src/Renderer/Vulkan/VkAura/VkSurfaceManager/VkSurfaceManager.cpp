#include "aura/Renderer/Vulkan/VkAura/VkSurfaceManager/VkSurfaceManager.h"

#include <ink/ink.hpp>
#include <wma/core/BuildConfig.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkSurfaceManager::VkSurfaceManager(VkInstance* vkInstance, wma::WindowBackend windowBackend, void* window)
    : _vkInstance(vkInstance), _windowBackend(windowBackend)
{
    if (!window)
        throw AuraException("VkSurfaceManager: window handle is null");

    switch (windowBackend) {
#if WMA_HAS_GLFW
    case wma::WindowBackend::GLFW:
        VK_RESULT_CHECK(glfwCreateWindowSurface(*_vkInstance, (GLFWwindow*)window, nullptr, &_vkSurface));
        break;
#endif
#if WMA_HAS_SDL
    case wma::WindowBackend::SDL3:
        if (!SDL_Vulkan_CreateSurface((SDL_Window*)window, *_vkInstance, nullptr, &_vkSurface)) {
            throw AuraException("Fail to create SDL Window surface! SDL Error: " + std::string(SDL_GetError()));
        }
        break;
#endif
    default:
        break;
    }
}

VkSurfaceManager::~VkSurfaceManager() {
    if (_vkSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(*_vkInstance, _vkSurface, nullptr);
        INK_DEBUG << "VkSurface deleted";
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

}
}
