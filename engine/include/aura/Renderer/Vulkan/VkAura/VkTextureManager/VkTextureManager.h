#ifndef VKTEXTUREMANAGER_H
#define VKTEXTUREMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>

#include "aura/aura.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"

namespace aura3d {
namespace vk {

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
        VkImage       image      = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView   view       = VK_NULL_HANDLE;
        VkSampler     sampler    = VK_NULL_HANDLE;
        u32           width      = 0;
        u32           height     = 0;
    };

    VkTextureManager(VulkanMemoryManager* memoryManager,
                     VkDevice* device,
                     VkCommandPool commandPool,
                     VkQueue graphicsQueue);

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
     * @brief Uploads arbitrary RGBA8 pixel data through a staging buffer.
     *
     * The general upload path: createSolidColorTexture() is a 1x1 case of it.
     *
     * @param name Unique identifier for the texture; an existing entry is replaced.
     * @param rgba Tightly packed @p width * @p height * 4 bytes, RGBA order.
     * @param width Texture width in pixels.
     * @param height Texture height in pixels.
     * @return TextureData containing the created texture resources; a default
     *         constructed value when the input is empty.
     */
    TextureData createTextureFromPixels(const std::string& name,
                                        const u8* rgba,
                                        u32 width,
                                        u32 height);

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
    VulkanMemoryManager* _memoryManager;
    VkDevice* _device;
    VkCommandPool _commandPool;
    VkQueue _graphicsQueue;
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
    void destroyTextureData(TextureData& texture);
};

} // namespace vk
} // namespace aura3d

#endif // VKTEXTUREMANAGER_H
