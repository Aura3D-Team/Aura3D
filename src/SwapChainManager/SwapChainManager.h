#ifndef SWAPCHAINMANAGER_H
#define SWAPCHAINMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class SwapChainManager
{
public:
    SwapChainManager(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface, VkDevice* device);
    ~SwapChainManager();
private:
    SwapChainSupportDetails _swapChainSupportDetails;
    VkSwapchainKHR _swapChain;

    VkDevice* _device;

    VkSurfaceFormatKHR _chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

    VkPresentModeKHR _chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
};

#endif // SWAPCHAINMANAGER_H
