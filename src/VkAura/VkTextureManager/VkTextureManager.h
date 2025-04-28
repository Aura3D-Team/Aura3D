#ifndef VKTEXTUREMANAGER_H
#define VKTEXTUREMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>

#include "aura.hpp"
#include "VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h"
#include "VkAura/VkMemory/VkDeviceAllocator/VkDeviceAllocator.h"

namespace aura3d {

/**
 * @file VkTextureManager
 * @brief Manages Vulkan texture resources with proper memory allocation
 *
 * This class handles creation, storage, and cleanup of Vulkan textures
 * using a custom allocator for efficient memory management.
 */
class VkTextureManager {
public:
    /**
     * @struct TextureData
     * @brief Contains all resources associated with a texture
     *
     * Holds Vulkan handles and metadata for a complete texture
     * including the image, memory allocation, view, and sampler.
     */
    struct TextureData {
        VkImage image = VK_NULL_HANDLE;          // Vulkan image handle
        VkDeviceAllocation* allocation = nullptr;     // Memory allocation information
        VkImageView view = VK_NULL_HANDLE;        // Image view for shader access
        VkSampler sampler = VK_NULL_HANDLE;       // Sampler for texture filtering
        u32 width = 0;                       // Texture width in pixels
        u32 height = 0;                      // Texture height in pixels
    };

    /**
     * @brief Constructs a texture manager
     *
     * @param device Pointer to the Vulkan logical device
     * @param physicalDevice Pointer to the Vulkan physical device
     * @param commandPool Command pool for texture operations
     * @param graphicsQueue Queue for submitting texture commands
     * @param bufferAllocator Memory allocator for texture resources
     */
    VkTextureManager(VkHostAllocator* vkHostAllocator,
                     VkDeviceAllocator* vkDeviceAllocator,
                     VkDevice* device,
                     VkPhysicalDevice* physicalDevice,
                     VkCommandPool commandPool,
                     VkQueue graphicsQueue);

    /**
     * @brief Destructor that cleans up all texture resources
     */
    ~VkTextureManager();

    /**
     * @brief Creates a 1x1 solid color texture
     *
     * @param name Unique identifier for the texture
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     * @param a Alpha component (0-255, default 255)
     * @return TextureData containing the created texture resources
     */
    TextureData createSolidColorTexture(const std::string& name,
                                        u8 r, u8 g, u8 b,
                                        u8 a = 255);

    /**
     * @brief Retrieves a texture by name
     *
     * @param name The identifier of the texture to retrieve
     * @return Pointer to the texture data or nullptr if not found
     */
    const TextureData* getTexture(const std::string& name) const;

    /**
     * @brief Cleans up all texture resources
     *
     * Destroys all textures and frees their associated memory.
     */
    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDeviceAllocator* vkDeviceAllocator;

    VkDevice* _device;                  // Logical Vulkan device
    VkPhysicalDevice* _physicalDevice;  // Physical Vulkan device
    VkCommandPool _commandPool;         // Command pool for operations
    VkQueue _graphicsQueue;             // Graphics queue for submissions

    // Storage for textures by name
    std::unordered_map<std::string, TextureData> _textures;

    /**
     * @brief Creates a Vulkan image
     *
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param format Vulkan format of the image
     * @param image Output parameter for the created image handle
     */
    void createImage(u32 width, u32 height, VkFormat format, VkImage& image);

    /**
     * @brief Creates an image view for a texture
     *
     * @param image The image to create a view for
     * @param format The format of the image
     * @return VkImageView handle for the created image view
     */
    VkImageView createImageView(VkImage image, VkFormat format);

    /**
     * @brief Creates a sampler for texture filtering
     *
     * @return VkSampler handle for the created sampler
     */
    VkSampler createSampler();

    /**
     * @brief Begins a single-use command buffer
     *
     * @return Command buffer ready for recording
     */
    VkCommandBuffer beginSingleTimeCommands();

    /**
     * @brief Ends and submits a single-use command buffer
     *
     * @param commandBuffer The command buffer to submit
     */
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    /**
     * @brief Transitions an image between layouts
     *
     * @param image The image to transition
     * @param oldLayout Current layout of the image
     * @param newLayout Target layout for the image
     */
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

    /**
     * @brief Copies data from a buffer to an image
     *
     * @param buffer Source buffer containing pixel data
     * @param image Destination image
     * @param width Width of the region to copy
     * @param height Height of the region to copy
     */
    void copyBufferToImage(VkBuffer buffer, VkImage image, u32 width, u32 height);
};

} // namespace aura3d
#endif // VKTEXTUREMANAGER_H
