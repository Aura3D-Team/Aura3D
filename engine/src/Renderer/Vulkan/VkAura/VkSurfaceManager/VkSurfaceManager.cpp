#include "aura/Renderer/Vulkan/VkAura/VkSurfaceManager/VkSurfaceManager.h"

#include <ink/ink.hpp>
#include <wma/core/BuildConfig.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

// X11/Xlib.h and wayland-client.h are kept out of VkSurfaceManager.h (and thus out
// of every consumer TU) because Xlib.h in particular #defines short, common tokens
// (None, Bool, True, False, Status, Success, ...) that collide with unrelated code
// (see wma::WmaCode's doc comment for the same rationale). They're only needed here,
// to reach the one vkCreate*SurfaceKHR call each backend needs.
#if WMA_HAS_X11
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif

#if WMA_HAS_WAYLAND
#include <vulkan/vulkan_wayland.h>
#endif

namespace aura3d {
namespace vk {

VkSurfaceManager::VkSurfaceManager(VkInstance* vkInstance, wma::WindowBackend windowBackend, void* window, void* nativeDisplay)
    : _vkInstance(vkInstance), _vkSurface(VK_NULL_HANDLE), _windowBackend(windowBackend)
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
#if WMA_HAS_X11
    case wma::WindowBackend::X11: {
        if (!nativeDisplay)
            throw AuraException("VkSurfaceManager: X11 backend requires a native display handle");

        VkXlibSurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.dpy = static_cast<Display*>(nativeDisplay);
        createInfo.window = reinterpret_cast<Window>(window);
        VK_RESULT_CHECK(vkCreateXlibSurfaceKHR(*_vkInstance, &createInfo, nullptr, &_vkSurface));
        break;
    }
#endif
#if WMA_HAS_WAYLAND
    case wma::WindowBackend::WAYLAND: {
        if (!nativeDisplay)
            throw AuraException("VkSurfaceManager: Wayland backend requires a native display handle");

        VkWaylandSurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        createInfo.display = static_cast<struct wl_display*>(nativeDisplay);
        createInfo.surface = static_cast<struct wl_surface*>(window);
        VK_RESULT_CHECK(vkCreateWaylandSurfaceKHR(*_vkInstance, &createInfo, nullptr, &_vkSurface));
        break;
    }
#endif
    default:
        throw AuraException("VkSurfaceManager: no Vulkan surface backend compiled in for the selected window backend");
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
