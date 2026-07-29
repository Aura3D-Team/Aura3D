#ifndef VKSURFACEMANAGER_H
#define VKSURFACEMANAGER_H

#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <wma/wma.hpp>

namespace aura3d {
namespace vk {

/**
 * @brief Manages the creation and destruction of a Vulkan surface for rendering.
 *
 * The VkSurfaceManager class is responsible for creating a Vulkan surface using
 * a specified Vulkan instance and a cross-platform windowing system (GLFW).
 * It provides a convenient interface to retrieve the created surface.
 */
class VkSurfaceManager
{
public:
    /**
     * @brief Constructs a VkSurfaceManager with the specified Vulkan instance and window.
     *
     * This constructor initializes the VkSurfaceManager, creating a Vulkan surface
     * that links the Vulkan instance to the provided GLFW window. The surface allows
     * Vulkan to render on the window across different platforms.
     *
     * @param vkInstance A pointer to the Vulkan instance, used to create the surface.
     * @param window A pointer to the GLFW window for which the surface will be created.
     */
    VkSurfaceManager(VkInstance* vkInstance, wma::WindowBackend windowBackend, void* window);

    /**
     * @brief Destroys the Vulkan surface and cleans up resources.
     *
     * The destructor ensures that the Vulkan surface is properly destroyed
     * when the VkSurfaceManager object is deleted, releasing any resources
     * associated with the surface.
     */
    ~VkSurfaceManager();

    /**
     * @brief Retrieves the Vulkan surface.
     *
     * This function returns a pointer to the Vulkan surface created for the specified
     * GLFW window, allowing it to be used for rendering operations.
     *
     * @return VkSurfaceKHR* A pointer to the Vulkan surface.
     */
    VkSurfaceKHR* getSurface();

    /**
     * @brief Avaliate physical device support for a queue family that allows operations on a VkSurface
     *
     * @param physicalDevice A physical device choosed on a machine.
     * @param familyIndex A VkQueue family index.
     *
     * @return VkBool32 if physicalDevice device support that queue on a VkSurface
     */
    VkBool32 getQueuePhysicalDeviceSurfaceSupport(VkPhysicalDevice physicalDevice, const int familyIndex);

private:
    VkInstance* _vkInstance; ///< Pointer to the Vulkan instance used to bind the surface.
    VkSurfaceKHR _vkSurface; ///< Vulkan surface for rendering on cross-platform windowing systems (GLFW/SDL2).]
    wma::WindowBackend _windowBackend;
};

}
}

#endif // VKSURFACEMANAGER_H
