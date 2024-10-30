#ifndef SWAPCHAINMANAGER_H
#define SWAPCHAINMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <GLFW/glfw3.h>

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

    SwapChainSupportDetails* getSwapChainSupportDetails();

    /**
     * @brief Chooses the best swap extent (resolution) for the swap chain.
     *
     * This function determines the resolution for images in the swap chain.
     * If Vulkan has specified a fixed extent (in capabilities.currentExtent), that value is used directly.
     * Otherwise, the function queries the window's framebuffer size in pixels to calculate the
     * extent, clamping it within the bounds of minImageExtent and maxImageExtent.
     *
     * @param capabilities The capabilities of the surface, including possible extent bounds.
     * @param window The GLFW window for which to determine the framebuffer size.
     *
     * @return VkExtent2D The chosen extent (resolution) for the swap chain images.
     */
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window);

private:
    SwapChainSupportDetails _swapChainSupportDetails;
    VkSwapchainKHR _swapChain;

    VkDevice* _device;

    void _initSwapChainSupportDetails(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface);

    /**
     * @brief Chooses a surface format for the swap chain.
     *
     * This function selects the best surface format based on preferred settings.
     * The preferred format is VK_FORMAT_B8G8R8A8_SRGB with VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
     * color space, which provides better perceived color accuracy in the SRGB color space.
     * If this combination is not available, the function returns the first available format.
     *
     * @param availableFormats A list of supported surface formats for the swap chain.
     * @param vkFormat Preferred vkFormat VK_FORMAT_B8G8R8A8_SRGB
     * @param vkColorSpace Preferred vkColorSpace VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
     *
     * @return VkSurfaceFormatKHR The chosen surface format for the swap chain.
     */
    VkSurfaceFormatKHR _chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats,
                                                const VkFormat vkFormat = VK_FORMAT_B8G8R8A8_SRGB, const VkColorSpaceKHR vkColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    /**
     * @brief Chooses the optimal presentation mode for the swap chain.
     *
     * This function selects the best presentation mode based on the preferred settings.
     * The preferred mode is VK_PRESENT_MODE_MAILBOX_KHR, which allows for reduced
     * latency and tearing avoidance (similar to triple buffering).
     * If VK_PRESENT_MODE_MAILBOX_KHR is unavailable, the function defaults to
     * VK_PRESENT_MODE_FIFO_KHR, which is always available and similar to traditional
     * vsync.
     *
     * @param availablePresentModes A list of supported presentation modes for the swap chain.
     * @param vkPresentMode Preferred vkPresentMode VK_PRESENT_MODE_MAILBOX_KHR
     *
     * @return VkPresentModeKHR The chosen presentation mode for the swap chain.
     */
    VkPresentModeKHR _chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes, const VkPresentModeKHR vkPresentMode = VK_PRESENT_MODE_MAILBOX_KHR);
};

#endif // SWAPCHAINMANAGER_H
