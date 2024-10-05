#ifndef VKINSTANCEMANAGER_H
#define VKINSTANCEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>

class VkInstanceManager
{
public:
    VkInstanceManager();
    ~VkInstanceManager();

    VkInstance getVkInstance() const {
        return _vkInstance;
    }

private:
    VkApplicationInfo _appInfo;
    VkInstanceCreateInfo _instanceInfo;
    VkInstance _vkInstance;
};

#endif // VKINSTANCEMANAGER_H
