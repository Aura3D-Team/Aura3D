#include "VkSurfaceManager.h"

#include <aura.hpp>
#include "AuraException/AuraException.h"

#include <ink/ink.hpp>

namespace aura3d {

VkSurfaceManager::VkSurfaceManager(VkHostAllocator* vkHostAllocator, VkInstance* vkInstance, wma::WindowBackend windowBackend, void* window)
    : _vkHostAllocator(vkHostAllocator), _vkInstance(vkInstance), _windowBackend(windowBackend)
{
    switch (windowBackend) {
    case wma::WindowBackend::GLFW:
        VK_RESULT_CHECK(glfwCreateWindowSurface(*_vkInstance, (GLFWwindow*)window, _vkHostAllocator->getCallbacks(), &_vkSurface));
        break;
    case wma::WindowBackend::SDL2:
        if (!SDL_Vulkan_CreateSurface((SDL_Window*)window, *_vkInstance, &_vkSurface)) {
            throw AuraException("Fail to create SDL Window surface! SDL Error: " + std::string(SDL_GetError()));
        }
    default:
        break;
    }
}

VkSurfaceManager::~VkSurfaceManager() {
    if (_vkSurface != VK_NULL_HANDLE) {
        if (_windowBackend == wma::WindowBackend::SDL2)
            vkDestroySurfaceKHR(*_vkInstance, _vkSurface, nullptr);
        else
            vkDestroySurfaceKHR(*_vkInstance, _vkSurface, _vkHostAllocator->getCallbacks());
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
