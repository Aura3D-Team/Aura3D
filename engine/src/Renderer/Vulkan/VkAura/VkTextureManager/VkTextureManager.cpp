#include "aura/Renderer/Vulkan/VkAura/VkTextureManager/VkTextureManager.h"

#include <cstring>
#include <algorithm>

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

VkTextureManager::TextureId VkTextureManager::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    const u8 pixel[4] = {r, g, b, a};
    return createTextureFromPixels(pixel, 1, 1);
}

VkTextureManager::TextureId VkTextureManager::createTextureFromPixels(
    const u8* rgba, u32 width, u32 height)
{
    if (!rgba || width == 0 || height == 0)
    {
        INK_ERROR << "VkTextureManager: refusing to upload an empty texture";
        return kInvalidTextureId;
    }

    TextureData textureData{};
    textureData.width = width;
    textureData.height = height;

    const VkDeviceSize imageBytes = static_cast<VkDeviceSize>(width) * height * 4u;

    void* data = acquireStagingBuffer(imageBytes);
    if (!data)
        return kInvalidTextureId;
    std::memcpy(data, rgba, static_cast<size_t>(imageBytes));

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

    // The transition/copy/transition joins the current upload batch.
    VkCommandBuffer commandBuffer = beginUploadCommands();
    recordLayoutTransition(commandBuffer, textureData.image,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    recordCopyBufferToImageRegion(commandBuffer, upload().staging.buffer, textureData.image,
                                  0, 0, width, height);
    recordLayoutTransition(commandBuffer, textureData.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    textureData.view = createImageView(textureData.image, VK_FORMAT_R8G8B8A8_UNORM);
    textureData.sampler = createSampler();

    _textures.push_back(textureData);
    return static_cast<TextureId>(_textures.size());
}

VkTextureManager::TextureId VkTextureManager::createDynamicTexture(u32 width, u32 height)
{
    if (width == 0 || height == 0)
    {
        INK_ERROR << "VkTextureManager: refusing to allocate an empty dynamic texture";
        return kInvalidTextureId;
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
    VkCommandBuffer commandBuffer = beginUploadCommands();

    recordLayoutTransition(commandBuffer, textureData.image,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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

    recordLayoutTransition(commandBuffer, textureData.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


    textureData.view = createImageView(textureData.image, VK_FORMAT_R8G8B8A8_UNORM);
    textureData.sampler = createSampler();

    _textures.push_back(textureData);
    return static_cast<TextureId>(_textures.size());
}

void VkTextureManager::updateRegion(TextureId id,
                                    u32 x, u32 y, u32 width, u32 height,
                                    const u8* rgba)
{
    if (!rgba || width == 0 || height == 0)
        return;

    if (id == kInvalidTextureId || id > _textures.size())
    {
        INK_ERROR << "VkTextureManager: updateRegion on an unknown texture id " << id;
        return;
    }

    TextureData& textureData = _textures[id - 1];
    if (x > textureData.width || y > textureData.height ||
        width > textureData.width - x || height > textureData.height - y)
    {
        INK_ERROR << "VkTextureManager: updateRegion rectangle exceeds the bounds of texture " << id;
        return;
    }

    const VkDeviceSize regionBytes = static_cast<VkDeviceSize>(width) * height * 4u;

    void* data = acquireStagingBuffer(regionBytes);
    if (!data)
        return;
    std::memcpy(data, rgba, static_cast<size_t>(regionBytes));

    // Each patch owns a distinct staging range until this batch's fence completes.
    VkCommandBuffer commandBuffer = beginUploadCommands();
    recordLayoutTransition(commandBuffer, textureData.image,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    recordCopyBufferToImageRegion(commandBuffer, upload().staging.buffer, textureData.image,
                                  x, y, width, height);
    recordLayoutTransition(commandBuffer, textureData.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

const VkTextureManager::TextureData* VkTextureManager::getTexture(TextureId id) const noexcept
{
    if (id == kInvalidTextureId || id > _textures.size())
        return nullptr;
    return &_textures[id - 1];
}

void VkTextureManager::retireUpload(UploadSlot& slot)
{
    if (slot.pending)
    {
        VK_RESULT_CHECK(vkWaitForFences(*_device, 1, &slot.fence, VK_TRUE, UINT64_MAX));
        slot.pending = false;
        slot.used = 0;
    }
}

void VkTextureManager::releaseStagingBuffer()
{
    auto& slot = upload();
    if (slot.manuallyMapped) _memoryManager->unmap(slot.staging);
    _memoryManager->destroyBuffer(slot.staging);
    slot.capacity = 0;
    slot.manuallyMapped = false;
}

void* VkTextureManager::acquireStagingBuffer(VkDeviceSize bytes)
{
    if (!bytes) return nullptr;
    // Each region is RGBA8, so its offset satisfies buffer-image copy alignment.
    if (upload().recording && bytes > upload().capacity - upload().used)
        flushUploads();
    retireUpload(upload());
    auto& slot = upload();
    if (bytes > slot.capacity)
    {
        releaseStagingBuffer();
        const VkDeviceSize capacity = std::max<VkDeviceSize>(bytes, 4 * 1024 * 1024);
        slot.staging = _memoryManager->createUploadBuffer(capacity, VK_SHARING_MODE_EXCLUSIVE);
        if (slot.staging.buffer == VK_NULL_HANDLE)
        {
            INK_ERROR << "VkTextureManager: could not allocate a " << capacity << "-byte staging buffer";
            return nullptr;
        }
        //! Persistent mapping is the normal case; map() is the fallback for a
        //! driver that refused, flagged so releaseStagingBuffer() balances it.
        if (!slot.staging.mappedData)
            slot.manuallyMapped = _memoryManager->map(slot.staging) != nullptr;
        if (!slot.staging.mappedData)
        {
            INK_ERROR << "VkTextureManager: staging buffer could not be mapped";
            releaseStagingBuffer();
            return nullptr;
        }
        slot.capacity = capacity;
    }
    _stagingOffset = slot.used;
    slot.used += bytes;
    return static_cast<u8*>(slot.staging.mappedData) + _stagingOffset;
}

void VkTextureManager::cleanup()
{
    flushUploads();
    for (usize i = 0; i < _uploads.size(); ++i)
    {
        _uploadIndex = i;
        auto& slot = upload();
        retireUpload(slot);
        releaseStagingBuffer();
        if (slot.command) vkFreeCommandBuffers(*_device, _commandPool, 1, &slot.command);
        if (slot.fence) vkDestroyFence(*_device, slot.fence, nullptr);
        slot = {};
    }
    _uploadIndex = 0;
    for (auto& texture : _textures) destroyTextureData(texture);
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

VkCommandBuffer VkTextureManager::beginUploadCommands()
{
    auto& slot = upload();
    if (slot.recording) return slot.command;
    retireUpload(slot);
    if (!slot.command)
    {
        VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        info.commandPool = _commandPool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        VK_RESULT_CHECK(vkAllocateCommandBuffers(*_device, &info, &slot.command));
    }
    else
        VK_RESULT_CHECK(vkResetCommandBuffer(slot.command, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_RESULT_CHECK(vkBeginCommandBuffer(slot.command, &begin));
    slot.recording = true;
    return slot.command;
}

void VkTextureManager::flushUploads()
{
    auto& slot = upload();
    if (!slot.recording) return;
    if (slot.used)
        VK_RESULT_CHECK(vmaFlushAllocation(_memoryManager->getAllocator(), slot.staging.allocation, 0, slot.used));
    VK_RESULT_CHECK(vkEndCommandBuffer(slot.command));
    if (!slot.fence)
    {
        VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_RESULT_CHECK(vkCreateFence(*_device, &info, nullptr, &slot.fence));
    }
    else
        VK_RESULT_CHECK(vkResetFences(*_device, 1, &slot.fence));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot.command;
    VK_RESULT_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, slot.fence));
    slot.pending = true;
    slot.recording = false;
    _uploadIndex = (_uploadIndex + 1) % _uploads.size();
}

void VkTextureManager::recordLayoutTransition(VkCommandBuffer cmd, VkImage image,
                                              VkImageLayout oldLayout, VkImageLayout newLayout) const
{
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

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VkTextureManager::recordCopyBufferToImageRegion(VkCommandBuffer cmd, VkBuffer buffer, VkImage image,
                                                     u32 x, u32 y, u32 width, u32 height) const
{
    VkBufferImageCopy region{};
    region.bufferOffset = _stagingOffset;
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

    vkCmdCopyBufferToImage(cmd,
                           buffer,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);
}

} // namespace vk
} // namespace aura3d
