#ifndef VKSURFACEMANAGER_H
#define VKSURFACEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>

class VkSurfaceManager
{
public:
    VkSurfaceManager(VkInstance* vkInstance);
    ~VkSurfaceManager();

    VkSurfaceKHR* getSurface();
private:
    VkInstance* _vkInstance; ///< Vulkan instance pointer used to bind the surface.

    VkSurfaceKHR _vkSurface; ///< Vulkan Surface for appling rendering on cross plataform windowing systems (GLFW).
};

#endif // VKSURFACEMANAGER_H
