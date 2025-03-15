#include "VkTextureManager.h"
#include <aura.hpp>
#include <AuraException/AuraException.h>
#include <cstring>
#include "VkAura/VkBufferManager/VkBufferManager.h"

namespace aura3d {

/**
 * Constructor - initializes the texture manager with required Vulkan resources
 */
VkTextureManager::VkTextureManager(VkDevice* device,
                                   VkPhysicalDevice* physicalDevice,
                                   VkCommandPool commandPool,
                                   VkQueue graphicsQueue)
    : _device(device),
    _physicalDevice(physicalDevice),
    _commandPool(commandPool),
    _graphicsQueue(graphicsQueue)
{
    // Constructor now correctly initializes member variables
}

/**
 * Destructor - ensures all texture resources are properly cleaned up
 */
VkTextureManager::~VkTextureManager() {
    cleanup();
}

/**
 * Creates a 1x1 solid color texture with RGBA components
 * Uses staging buffer for data transfer to GPU-local memory
 */
VkTextureManager::TextureData VkTextureManager::createSolidColorTexture(
    const std::string& name, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // Create a 1x1 texture with the given color
    uint32_t width = 1;
    uint32_t height = 1;
    VkDeviceAllocator& allocator = VkDeviceAllocator::getInstance();

    // Check if texture already exists and clean it up if so
    auto it = _textures.find(name);
    if (it != _textures.end()) {
        // Destroy existing texture resources in correct order
        if (it->second.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(*_device, it->second.sampler, allocationCallbacks);
        }
        if (it->second.view != VK_NULL_HANDLE) {
            vkDestroyImageView(*_device, it->second.view, allocationCallbacks);
        }
        if (it->second.image != VK_NULL_HANDLE) {
            vkDestroyImage(*_device, it->second.image, allocationCallbacks);
        }

        // Free memory allocation using device allocator
        if (it->second.allocation != nullptr) {
            allocator.freeMemory(*it->second.allocation);
            delete it->second.allocation;
        }

        _textures.erase(it);
    }

    // Initialize new texture data
    TextureData textureData = {};
    textureData.width = width;
    textureData.height = height;

    // Create a staging buffer for the pixel data
    VkBuffer stagingBuffer;
    VkDeviceAllocation stagingAllocation;

    // Create staging buffer with the device allocator
    VkBufferManager::createBuffer(
        *_device,
        *_physicalDevice,
        4, // RGBA = 4 bytes
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingAllocation
        );

    // Copy pixel data to the staging buffer
    uint8_t pixelData[4] = { r, g, b, a };
    void* data = nullptr;
    VK_RESULT_CHECK(allocator.mapMemory(
        stagingAllocation, 0, 4, &data
        ));
    std::memcpy(data, pixelData, 4);
    allocator.unmapMemory(stagingAllocation);

    // Create the image
    createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, textureData.image);

    // Allocate memory for the image
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(*_device, textureData.image, &memRequirements);

    // Allocate device-local memory for the image
    textureData.allocation = new VkDeviceAllocation();
    VK_RESULT_CHECK(allocator.allocateMemory(
        memRequirements,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        *textureData.allocation
        ));

    // Bind image memory
    VK_RESULT_CHECK(vkBindImageMemory(
        *_device,
        textureData.image,
        textureData.allocation->memory,
        textureData.allocation->offset
        ));

    // Transition the image layout for copy operation
    transitionImageLayout(textureData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy data from the staging buffer to the image
    copyBufferToImage(stagingBuffer, textureData.image, width, height);

    // Transition the image layout for shader access
    transitionImageLayout(textureData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Create an image view
    textureData.view = createImageView(textureData.image, VK_FORMAT_R8G8B8A8_UNORM);

    // Create a sampler
    textureData.sampler = createSampler();

    // Clean up staging resources
    vkDestroyBuffer(*_device, stagingBuffer, allocationCallbacks);
    allocator.freeMemory(stagingAllocation);

    // Store the texture
    _textures[name] = textureData;
    return textureData;
}

/**
 * Retrieves a texture by name from the texture cache
 */
const VkTextureManager::TextureData* VkTextureManager::getTexture(const std::string& name) const {
    auto it = _textures.find(name);
    if (it != _textures.end()) {
        return &it->second;
    }
    return nullptr;
}

/**
 * Cleans up all texture resources in the correct order to avoid Vulkan validation errors
 */
void VkTextureManager::cleanup() {
    // Clean up all textures in the proper order
    for (auto& pair : _textures) {
        TextureData& texture = pair.second;

        // Destroy sampler
        if (texture.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(*_device, texture.sampler, allocationCallbacks);
            texture.sampler = VK_NULL_HANDLE;
        }

        // Destroy image view
        if (texture.view != VK_NULL_HANDLE) {
            vkDestroyImageView(*_device, texture.view, allocationCallbacks);
            texture.view = VK_NULL_HANDLE;
        }

        // Destroy image
        if (texture.image != VK_NULL_HANDLE) {
            vkDestroyImage(*_device, texture.image, allocationCallbacks);
            texture.image = VK_NULL_HANDLE;
        }

        // Free memory allocation
        if (texture.allocation != nullptr) {
            VkDeviceAllocator::getInstance().freeMemory(*texture.allocation);
            delete texture.allocation;
            texture.allocation = nullptr;
        }
    }

    _textures.clear();
}

/**
 * Creates a Vulkan image with specified dimensions and format
 */
void VkTextureManager::createImage(uint32_t width, uint32_t height, VkFormat format, VkImage& image) {
    // Configure image creation parameters
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    // Create the image - memory will be allocated separately
    VK_RESULT_CHECK(vkCreateImage(*_device, &imageInfo, allocationCallbacks, &image))
}

/**
 * Creates an image view for a texture to be used in shader binding
 */
VkImageView VkTextureManager::createImageView(VkImage image, VkFormat format) {
    // Configure image view creation parameters
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    // Create the image view
    VkImageView imageView;
    VK_RESULT_CHECK(vkCreateImageView(*_device, &viewInfo, allocationCallbacks, &imageView))

    return imageView;
}

/**
 * Creates a sampler for texture filtering and addressing
 */
VkSampler VkTextureManager::createSampler() {
    // Configure sampler creation parameters
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;              // Linear interpolation for magnification
    samplerInfo.minFilter = VK_FILTER_LINEAR;              // Linear interpolation for minification
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // Wrap U coordinates
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; // Wrap V coordinates
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; // Wrap W coordinates
    samplerInfo.anisotropyEnable = VK_FALSE;               // Disable anisotropic filtering
    samplerInfo.maxAnisotropy = 1.0f;                      // Anisotropy level (not used)
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK; // Border color for clamp mode
    samplerInfo.unnormalizedCoordinates = VK_FALSE;        // Use normalized coordinates [0,1]
    samplerInfo.compareEnable = VK_FALSE;                  // No comparison operation
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;          // Always pass comparison
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; // Linear mipmap interpolation
    samplerInfo.mipLodBias = 0.0f;                         // No LOD bias
    samplerInfo.minLod = 0.0f;                             // Minimum LOD level
    samplerInfo.maxLod = 0.0f;                             // Maximum LOD level (0 = no mipmaps)

    // Create the sampler
    VkSampler sampler;
    VK_RESULT_CHECK(vkCreateSampler(*_device, &samplerInfo, allocationCallbacks, &sampler));

    return sampler;
}

/**
 * Begins recording a one-time use command buffer
 */
VkCommandBuffer VkTextureManager::beginSingleTimeCommands() {
    // Allocate a command buffer from the command pool
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = _commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(*_device, &allocInfo, &commandBuffer);

    // Begin recording commands
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // One-time use

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

/**
 * Submits and cleans up a one-time use command buffer
 */
void VkTextureManager::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    // End command recording
    vkEndCommandBuffer(commandBuffer);

    // Submit the command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Submit to queue and wait for completion
    vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(_graphicsQueue);

    // Free the command buffer
    vkFreeCommandBuffers(*_device, _commandPool, 1, &commandBuffer);
}

/**
 * Transitions an image between layouts using appropriate memory barriers
 */
void VkTextureManager::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    // Start a command buffer for the transition
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // Configure image memory barrier
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // No queue family transfer
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // No queue family transfer
    barrier.image = image;

    // Configure subresource range
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // Configure access masks and pipeline stages based on the transition
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Transitioning from initial state to transfer destination
        barrier.srcAccessMask = 0;                         // No access needed before
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // Will be written to
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;      // Earliest possible stage
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;    // Transfer operations
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Transitioning from transfer destination to shader reading
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // Was written to
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;    // Will be read by shader
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;         // Transfer operations
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // Fragment shader reading
    }
    else {
        throw AuraException("Unsupported layout transition!");
    }

    // Record the barrier command
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,  // Pipeline stages
        0,                              // No dependency flags
        0, nullptr,                     // No memory barriers
        0, nullptr,                     // No buffer memory barriers
        1, &barrier                     // One image memory barrier
        );

    // Submit the command buffer
    endSingleTimeCommands(commandBuffer);
}

/**
 * Copies buffer data to an image using a command buffer
 */
void VkTextureManager::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    // Start a command buffer for the copy
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // Configure the copy region
    VkBufferImageCopy region{};
    region.bufferOffset = 0;                      // Start of buffer
    region.bufferRowLength = 0;                   // Tightly packed (no padding)
    region.bufferImageHeight = 0;                 // Tightly packed (no padding)
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;        // Base mip level
    region.imageSubresource.baseArrayLayer = 0;  // First array layer
    region.imageSubresource.layerCount = 1;      // Single layer
    region.imageOffset = {0, 0, 0};              // Start at origin
    region.imageExtent = {width, height, 1};     // 2D image extent

    // Record the copy command
    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,                                  // Source buffer
        image,                                   // Destination image
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,    // Image layout during copy
        1,                                       // Region count
        &region                                  // Region info
        );

    // Submit the command buffer
    endSingleTimeCommands(commandBuffer);
}

} // namespace aura3d
