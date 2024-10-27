#ifndef VKSURFACEMANAGER_H
#define VKSURFACEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>

class VkSurfaceManager
{
public:
    VkSurfaceManager(VkInstance* vkInstance);
    ~VkSurfaceManager();
private:
    VkInstance* _vkInstance; ///< Vulkan instance pointer used bind the surface
};

#endif // VKSURFACEMANAGER_H
