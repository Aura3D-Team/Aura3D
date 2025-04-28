#ifndef VKIMAGEVIEWSMANAGER_H
#define VKIMAGEVIEWSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "VkAura/VkAuraDefs.h"
#include <VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h>

namespace aura3d {

/**
 * @brief Manages the creation and cleanup of Vulkan image views for swap chain images.
 *
 * The VkImageViewsManager class encapsulates the setup and destruction of VkImageView objects for each
 * image in the Vulkan swap chain. It creates image views that allow swap chain images to be used as
 * color attachments in the rendering pipeline. This manager simplifies the process of image view
 * management and ensures proper cleanup when the image views are no longer needed.
 */
class VkImageViewsManager {
public:
    /**
     * @brief Constructs a VkImageViewsManager with a Vulkan device, swap chain images, and format.
     *
     * Initializes the VkImageViewsManager with the specified Vulkan logical device, a list of swap chain
     * images, and the image format. These inputs are required for creating image views compatible with
     * the swap chain.
     *
     * @param device The Vulkan logical device used to create image views.
     * @param swapChainImages A reference to a vector of VkImage objects representing swap chain images.
     * @param swapChainImageFormat The format of the swap chain images.
     */
    VkImageViewsManager(VkHostAllocator* vkHostAllocator, VkDevice* device);

    /**
     * @brief Destroys all created image views and releases resources.
     *
     * Destructor that ensures all VkImageView objects managed by this class are destroyed
     * using vkDestroyImageView, freeing associated resources. This prevents memory leaks
     * and ensures clean Vulkan resource management.
     */
    ~VkImageViewsManager();

    /**
     * @brief Creates a VkImageView for each image in the swap chain.
     *
     * Iterates through each VkImage in swapChainImages and creates a VkImageView with the specified
     * format and standard settings, allowing each image to be used as a color target. Throws a
     * runtime error if any image view creation fails.
     *
     * @param aspectMask The aspect mask specifying which part of the image to use (e.g., color, depth). Default is VK_IMAGE_ASPECT_COLOR_BIT.
     * @param baseMipLevel The base mip level for the image view (default is 0).
     * @param levelCount The number of mip levels for the image view (default is 1).
     * @param baseArrayLayer The starting layer for array textures (default is 0).
     * @param layerCount The number of layers in the array (default is 1).
     *
     * @throws AuraException If vkCreateImageView fails to create an image view.
     */
    void createImageViews(const std::vector<VkImage>& swapChainImages, VkFormat swapChainImageFormat, ImageViewData vkImageViewData);

    /**
     * @brief Provides access to the vector of created VkImageView objects.
     *
     * Returns a reference to the vector of VkImageView objects managed by this class, allowing
     * external access to the image views for use in other parts of the Vulkan rendering pipeline.
     *
     * @return A reference to a vector of VkImageView objects.
     */
    const std::vector<VkImageView>& getImageViews() const;

    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDevice* _device; ///< The Vulkan logical device used to create and manage image views.

    std::vector<VkImageView> _swapChainImageViews; ///< Vector storing the created VkImageView objects.
};

}

#endif // VKIMAGEVIEWSMANAGER_H
