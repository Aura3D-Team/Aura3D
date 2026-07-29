#include "aura/Renderer/Vulkan/VkAura/VkTextureManager/VkTextureManager.h"

#include <cstring>

#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkTextureManager::VkTextureManager(VulkanMemoryManager* memoryManager,
                                   VkDevice* device,
                                   VkCommandPool commandPool,
                                   VkQueue graphicsQueue)
    : _memoryManager(memoryManager),
      _device(device),
      _commandPool(commandPool),
      _graphicsQueue(graphicsQueue)
{
}

VkTextureManager::~VkTextureManager()
{
    cleanup();
}

void VkTextureManager::destroyTextureData(TextureData& texture)
{
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(*_device, texture.sampler, nullptr);
        texture.sampler = VK_NULL_HANDLE;
    }

    if (texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(*_device, texture.view, nullptr);
        texture.view = VK_NULL_HANDLE;
    }

    if (texture.image != VK_NULL_HANDLE || texture.allocation != VK_NULL_HANDLE) {
        AllocatedImage allocated{texture.image, texture.allocation};
        _memoryManager->destroyImage(allocated);
        texture.image = VK_NULL_HANDLE;
        texture.allocation = VK_NULL_HANDLE;
    }
}

VkTextureManager::TextureData VkTextureManager::createSolidColorTexture(
    const std::string& name, u8 r, u8 g, u8 b, u8 a)
{
    const u8 pixel[4] = {r, g, b, a};
    return createTextureFromPixels(name, pixel, 1, 1);
}

VkTextureManager::TextureData VkTextureManager::createTextureFromPixels(
    const std::string& name, const u8* rgba, u32 width, u32 height)
{
    if (!rgba || width == 0 || height == 0)
    {
        INK_ERROR << "VkTextureManager: refusing to upload an empty texture: " << name;
        return TextureData{};
    }

    auto it = _textures.find(name);
    if (it != _textures.end()) {
        destroyTextureData(it->second);
        _textures.erase(it);
    }

    TextureData textureData{};
    textureData.width = width;
    textureData.height = height;

    const VkDeviceSize imageBytes = static_cast<VkDeviceSize>(width) * height * 4u;

    AllocatedBuffer staging = _memoryManager->createUploadBuffer(imageBytes, VK_SHARING_MODE_EXCLUSIVE);
    void* data = staging.mappedData ? staging.mappedData : _memoryManager->map(staging);
    std::memcpy(data, rgba, static_cast<size_t>(imageBytes));
    if (!staging.mappedData) {
        _memoryManager->unmap(staging);
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    AllocatedImage gpuImage = _memoryManager->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    textureData.image = gpuImage.image;
    textureData.allocation = gpuImage.allocation;

    transitionImageLayout(textureData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(staging.buffer, textureData.image, width, height);
    transitionImageLayout(textureData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    textureData.view = createImageView(textureData.image, VK_FORMAT_R8G8B8A8_UNORM);
    textureData.sampler = createSampler();

    _memoryManager->destroyBuffer(staging);
    _textures[name] = textureData;
    return textureData;
}

VkTextureManager::TextureData VkTextureManager::createDynamicTexture(
    const std::string& name, u32 width, u32 height)
{
    if (width == 0 || height == 0)
    {
        INK_ERROR << "VkTextureManager: refusing to allocate an empty dynamic texture: " << name;
        return TextureData{};
    }

    auto it = _textures.find(name);
    if (it != _textures.end()) {
        destroyTextureData(it->second);
        _textures.erase(it);
    }

    TextureData textureData{};
    textureData.width = width;
    textureData.height = height;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    AllocatedImage gpuImage = _memoryManager->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    textureData.image = gpuImage.image;
    textureData.allocation = gpuImage.allocation;

    /*
     * Clear on the device rather than staging an all-zero buffer: a 2048x2048
     * atlas would otherwise mean pushing 16 MB across the bus just to write
     * zeroes. vkCmdClearColorImage does it without any host memory at all.
     */
    transitionImageLayout(textureData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    const VkClearColorValue transparentBlack{{0.0f, 0.0f, 0.0f, 0.0f}};
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(commandBuffer, textureData.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &transparentBlack, 1, &range);

    endSingleTimeCommands(commandBuffer);

    transitionImageLayout(textureData.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    textureData.view = createImageView(textureData.image, VK_FORMAT_R8G8B8A8_UNORM);
    textureData.sampler = createSampler();

    _textures[name] = textureData;
    return textureData;
}

void VkTextureManager::updateRegion(const std::string& name,
                                    u32 x, u32 y, u32 width, u32 height,
                                    const u8* rgba)
{
    if (!rgba || width == 0 || height == 0)
        return;

    auto it = _textures.find(name);
    if (it == _textures.end())
    {
        INK_ERROR << "VkTextureManager: updateRegion on an unknown texture: " << name;
        return;
    }

    TextureData& textureData = it->second;
    if (x + width > textureData.width || y + height > textureData.height)
    {
        INK_ERROR << "VkTextureManager: updateRegion rectangle exceeds the bounds of " << name;
        return;
    }

    const VkDeviceSize regionBytes = static_cast<VkDeviceSize>(width) * height * 4u;

    AllocatedBuffer staging = _memoryManager->createUploadBuffer(regionBytes, VK_SHARING_MODE_EXCLUSIVE);
    void* data = staging.mappedData ? staging.mappedData : _memoryManager->map(staging);
    std::memcpy(data, rgba, static_cast<size_t>(regionBytes));

    if (!staging.mappedData)
        _memoryManager->unmap(staging);

    transitionImageLayout(textureData.image,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copyBufferToImageRegion(staging.buffer, textureData.image, x, y, width, height);

    transitionImageLayout(textureData.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    _memoryManager->destroyBuffer(staging);
}

const VkTextureManager::TextureData* VkTextureManager::getTexture(const std::string& name) const
{
    auto it = _textures.find(name);
    if (it != _textures.end()) {
        return &it->second;
    }
    return nullptr;
}

void VkTextureManager::cleanup()
{
    for (auto& pair : _textures) {
        destroyTextureData(pair.second);
    }
    _textures.clear();
}

VkImageView VkTextureManager::createImageView(VkImage image, VkFormat format)
{
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

    VkImageView imageView;
    VK_RESULT_CHECK(vkCreateImageView(*_device, &viewInfo, nullptr, &imageView));
    return imageView;
}

VkSampler VkTextureManager::createSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    VkSampler sampler;
    VK_RESULT_CHECK(vkCreateSampler(*_device, &samplerInfo, nullptr, &sampler));
    return sampler;
}

VkCommandBuffer VkTextureManager::beginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = _commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(*_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VkTextureManager::endSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(_graphicsQueue);
    vkFreeCommandBuffers(*_device, _commandPool, 1, &commandBuffer);
}

void VkTextureManager::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && 
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } 
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } 
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
    {
        //! Re-entering the transfer state to patch an already sampleable
        //! texture, as the glyph atlas does whenever a new character appears.
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } 
    else
    {
        throw AuraException("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endSingleTimeCommands(commandBuffer);
}

void VkTextureManager::copyBufferToImage(VkBuffer buffer, VkImage image, u32 width, u32 height)
{
    copyBufferToImageRegion(buffer, image, 0, 0, width, height);
}

void VkTextureManager::copyBufferToImageRegion(VkBuffer buffer, VkImage image,
                                               u32 x, u32 y, u32 width, u32 height)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    /*
     * Zero means "rows are tightly packed at imageExtent.width". That holds
     * here because the caller stages exactly the sub-rectangle it wants to
     * write, rather than a window into a larger image.
     */
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {static_cast<i32>(x), static_cast<i32>(y), 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer,
                           buffer,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    endSingleTimeCommands(commandBuffer);
}

} // namespace vk
} // namespace aura3d
