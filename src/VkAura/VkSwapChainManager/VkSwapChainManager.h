#ifndef VKSWAPCHAINMANAGER_H
#define VKSWAPCHAINMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "aura.hpp"
#include "VkAura/VkAuraDefs.h"
#include <VkAura/VkDeviceManager/VkDeviceManager.h>
#include <VkAura/VkImageViewsManager/VkImageViewsManager.h>
#include <VkAura/VkFrameBuffersManager/VkFrameBuffersManager.h>
#include <aura.hpp>
#include <AuraWindowManagers/CommonWindow.hpp>
#include <VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h>

namespace aura3d {

class VkSwapChainManager
{
public:
    /**
     * @brief Constructs the VkSwapChainManager with specified device and surface parameters.
     *
     * This constructor initializes the swap chain manager with the provided physical device,
     * logical device, and rendering surface. It prepares the manager to handle swap chain
     * creation and management tasks, including querying swap chain support details and
     * configuring optimal settings for presentation.
     *
     * @param physicalDevice The Vulkan physical device that supports rendering operations.
     * @param device A pointer to the logical Vulkan device associated with the physical device.
     * @param vkSurface The Vulkan surface handle corresponding to the window or display surface.
     */
    VkSwapChainManager(VkHostAllocator* vkHostAllocator, VkPhysicalDevice physicalDevice, VkDevice* device, VkSurfaceKHR vkSurface);

    /**
     * @brief Destructor for the VkSwapChainManager.
     *
     * This destructor cleans up resources allocated by the VkSwapChainManager, including
     * the swap chain itself and any associated resources. It ensures that Vulkan objects
     * managed by the swap chain manager are properly released before the manager is destroyed.
     */
    ~VkSwapChainManager();

    /**
     * @brief Initializes swap chain support details for a physical device and surface.
     *
     * This function populates `_swapChainSupportDetails` by querying the physical
     * device and surface for supported swap chain capabilities, formats, and
     * presentation modes. It ensures the device and surface are compatible with
     * the swap chain requirements.
     *
     * @param physicalDevice The Vulkan physical device to query for swap chain support.
     * @param vkSurface The Vulkan surface handle associated with the rendering window.
     */
    void initSwapChainSupportDetails(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface);

    /**
     * @brief Creates the Vulkan swap chain.
     *
     * This function initializes and creates a Vulkan swap chain tailored to the specified
     * window and surface. It configures the swap chain settings, such as image format,
     * color space, and extent, based on the surface capabilities and user-defined preferences.
     * Additionally, it sets up the image sharing mode to handle concurrent access from
     * multiple queue families if necessary.
     *
     * The created swap chain is essential for presenting rendered images to the screen,
     * and its configuration directly affects the visual output and performance.
     *
     * @param window The GLFW window associated with the rendering surface.
     * @param surface The Vulkan surface corresponding to the window where images will be presented.
     * @param vkDeviceManager A pointer to the VkDeviceManager responsible for managing the logical device and queue families.
     * @param layerCount Optional parameter specifying the number of image layers in the swap chain; defaults to 2.
     */
    void createSwapChain(WindowAPI* window, VkSurfaceKHR surface, VkDeviceManager* vkDeviceManager, u32 layerCount = 2);

    /**
     * @brief Retrieves swap chain support details.
     *
     * This function provides access to the `SwapChainSupportDetails` structure, which contains
     * information about the swap chain capabilities, available formats, and presentation modes
     * for the associated physical device and surface. This information is crucial for creating
     * a compatible swap chain and configuring it with the optimal settings.
     *
     * @return SwapChainSupportDetails* A pointer to the structure containing swap chain support details.
     */
    SwapChainSupportDetails* getSwapChainSupportDetails();

    VkSwapchainCreateInfoKHR* getSwapchainCreateInfoKHR();


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
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, WindowAPI* window);

    VkExtent2D* getExtent2D();

    VkSurfaceFormatKHR* getChoosedSurfaceFormat();

    VkPresentModeKHR* getChoosedPresentMode();

    VkSwapchainKHR* getSwapChain();

    const std::vector<VkImage>& getSwapChainImages();

    u32 acquireNextImage(VkSemaphore imageSemaphore, WindowFlags* windowFlags);

    void presentBackToSwapChain(VkQueue queue, VkSemaphore* renderFinishedSemaphore, const u32& imageIndex);

    void transitionImageLayout(
        VkCommandBuffer commandBuffer,
        const u32& imageIndex,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkFixedArray<VkPipelineStageFlags> stages,
        VkFixedArray<VkAccessFlags> accessFlags);

    void cleanup();
private:
    VkHostAllocator* vkHostAllocator;
    SwapChainSupportDetails _swapChainSupportDetails; ///< Holds details about swap chain support.

    VkSwapchainCreateInfoKHR _swapChainCreateInfo; ///< Stores configuration settings for creating a swap chain.
    VkSwapchainKHR _swapChain; ///< Vulkan swap chain that is used to manage the presentation of rendered images to the screen.

    VkDevice* _device; ///< A pointer to the logical Vulkan device, which is used for interfacing with the GPU.

    /**
     * @brief Swap chain surface format chosen for the Vulkan surface.
     *
     * This specifies the format (e.g., pixel layout) and color space for images in the swap chain.
     */
    VkSurfaceFormatKHR _choosedSurfaceFormat;

    /**
     * @brief The presentation mode selected for the swap chain.
     *
     * Determines how images are presented to the surface, influencing vsync behavior and latency.
     * Common options include VK_PRESENT_MODE_FIFO_KHR for vsync and VK_PRESENT_MODE_MAILBOX_KHR for low-latency, triple-buffering.
     */
    VkPresentModeKHR _choosedPresentMode;

    /**
     * @brief The extent (dimensions) of the swap chain images in pixels.
     *
     * Typically matches the dimensions of the window or the surface on which Vulkan renders.
     */
    VkExtent2D _choosedExtent;

    /**
     * @brief A collection of images in the swap chain.
     *
     * These images are used for rendering and are presented to the screen via the chosen presentation mode.
     */
    std::vector<VkImage> _swapChainImages;

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

}

#endif // VKSWAPCHAINMANAGER_H
