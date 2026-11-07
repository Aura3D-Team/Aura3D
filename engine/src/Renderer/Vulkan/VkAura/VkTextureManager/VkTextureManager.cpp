#include "aura/Renderer/Vulkan/VkAura/VkTextureManager/VkTextureManager.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkBufferManager/VkBufferManager.h"

namespace aura3d
{
namespace vk
{
namespace
{
[[nodiscard]] VkDeviceSize textureByteSize(u32 width, u32 height, u32 bytesPerTexel) noexcept
{
    constexpr VkDeviceSize maxBytes =
        std::min<VkDeviceSize>(std::numeric_limits<VkDeviceSize>::max(), std::numeric_limits<usize>::max());
    if (width == 0 || height == 0 || bytesPerTexel == 0 || width > static_cast<u32>(std::numeric_limits<i32>::max()) ||
        height > static_cast<u32>(std::numeric_limits<i32>::max()) ||
        static_cast<VkDeviceSize>(width) > maxBytes / bytesPerTexel / height)
        return 0;
    return static_cast<VkDeviceSize>(width) * height * bytesPerTexel;
}
[[nodiscard]] VkFormat vulkanFormat(TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Rgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::Depth: //! Never a colour image: createDepthAttachment() picks its own.
        break;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

[[nodiscard]] u32 formatBytes(VkFormat format) noexcept
{
    return format == VK_FORMAT_R8_UNORM ? 1u : format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8u : 4u;
}

[[nodiscard]] VkFilter vulkanFilter(TextureFilter filter) noexcept
{
    return filter == TextureFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

[[nodiscard]] VkSamplerAddressMode vulkanWrap(TextureWrap wrap) noexcept
{
    switch (wrap)
    {
    case TextureWrap::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case TextureWrap::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureWrap::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

} // namespace

VkTextureManager::VkTextureManager(VulkanMemoryManager *memoryManager, VkDevice *device, VkCommandPool commandPool,
                                   VkQueue graphicsQueue, f32 maxAnisotropy)
    : _memoryManager(memoryManager), _device(device), _commandPool(commandPool), _graphicsQueue(graphicsQueue),
      _maxAnisotropy(maxAnisotropy)
{
}

VkTextureManager::~VkTextureManager()
{
    try
    {
        cleanup();
    }
    catch (const std::exception &error)
    {
        INK_ERROR << "Fail to clean texture up: " << error.what();
    }
}

void VkTextureManager::destroyTextureData(TextureData &texture)
{
    if (texture.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(*_device, texture.sampler, nullptr);
        texture.sampler = VK_NULL_HANDLE;
    }

    if (texture.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(*_device, texture.view, nullptr);
        texture.view = VK_NULL_HANDLE;
    }

    if (texture.image != VK_NULL_HANDLE || texture.allocation != VK_NULL_HANDLE)
    {
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

VkTextureManager::TextureId VkTextureManager::createTextureFromPixels(const u8 *rgba, u32 width, u32 height)
{
    if (!rgba)
    {
        INK_ERROR << "VkTextureManager: refusing to upload an empty texture";
        return kInvalidTextureId;
    }
    return createTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, rgba);
}

VkTextureManager::TextureId VkTextureManager::createDynamicTexture(u32 width, u32 height)
{
    return createTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM);
}

VkTextureManager::TextureId VkTextureManager::createCoverageTexture(u32 width, u32 height)
{
    return createTexture(width, height, VK_FORMAT_R8_UNORM);
}

VkTextureManager::TextureId VkTextureManager::createTexture(const TextureDesc &desc, std::span<const std::byte> pixels)
{
    const VkFormat format = vulkanFormat(desc.format);
    if (pixels.size() != textureByteSize(desc.width, desc.height, formatBytes(format)))
    {
        INK_ERROR << "VkTextureManager: createTexture pixels do not match the descriptor's size and format";
        return kInvalidTextureId;
    }
    u32 mipLevels = desc.mipmaps ? mipLevelCount(desc.width, desc.height) : 1u;
    if (mipLevels > 1 && !canGenerateMips(format))
    {
        INK_WARN << "VkTextureManager: this device cannot filter-blit format " << format << "; keeping one mip level";
        mipLevels = 1;
    }
    return createTexture(desc.width, desc.height, format, pixels.data(), mipLevels, desc.sampler);
}

bool VkTextureManager::canGenerateMips(VkFormat format) const
{
    VmaAllocatorInfo allocator{};
    vmaGetAllocatorInfo(_memoryManager->getAllocator(), &allocator);
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(allocator.physicalDevice, format, &properties);
    // NOLINTNEXTLINE(bugprone-signed-bitwise): Vulkan flag bits are C enums.
    constexpr VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & kNeeded) == kNeeded;
}

VkTextureManager::TextureId VkTextureManager::createAttachment(u32 width, u32 height, TextureFormat format,
                                                               TextureId replace)
{
    constexpr SamplerDesc kClamped{.wrapU = TextureWrap::ClampToEdge, .wrapV = TextureWrap::ClampToEdge};
    const TextureId id = createTexture(width, height, vulkanFormat(format), nullptr, 1, kClamped,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, replace);
    if (id != kInvalidTextureId)
        _textures[id - 1].attachment = true;
    return id;
}

VkTextureManager::TextureId VkTextureManager::createDepthAttachment(u32 width, u32 height, VkFormat format,
                                                                    TextureId replace)
{
    TextureData texture{};
    texture.width = width;
    texture.height = height;
    texture.format = format;
    texture.attachment = true;
    try
    {
        const AllocatedImage image =
            _memoryManager->createImage2D(format, {width, height}, VK_SAMPLE_COUNT_1_BIT,
                                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        texture.image = image.image;
        texture.allocation = image.allocation;
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = texture.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        };
        VK_RESULT_CHECK(vkCreateImageView(*_device, &viewInfo, nullptr, &texture.view));
        texture.sampler = createSampler({.magFilter = TextureFilter::Nearest,
                                         .minFilter = TextureFilter::Nearest,
                                         .mipFilter = TextureFilter::Nearest,
                                         .wrapU = TextureWrap::ClampToEdge,
                                         .wrapV = TextureWrap::ClampToEdge},
                                        1);
    }
    catch (...)
    {
        destroyTextureData(texture);
        throw;
    }
    if (replace != kInvalidTextureId && replace <= _textures.size())
    {
        destroyTextureData(_textures[replace - 1]);
        _textures[replace - 1] = texture;
        return replace;
    }
    _textures.push_back(texture);
    return static_cast<TextureId>(_textures.size());
}

bool VkTextureManager::readPixels(TextureId id, std::span<u8> rgba)
{
    const TextureData *texture = getTexture(id);
    if (!texture || !texture->image || texture->format == VK_FORMAT_R8_UNORM ||
        rgba.size() != static_cast<usize>(texture->width) * texture->height * 4)
        return false;

    //! Uploads to this texture must land before the copy reads it.
    flushUploads();
    const VkDeviceSize bytes = textureByteSize(texture->width, texture->height, texture->bytesPerTexel);
    AllocatedBuffer readback = _memoryManager->createBuffer(
        bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    INK_DEFER
    {
        _memoryManager->destroyBuffer(readback);
    };

    VkBufferManager::executeImmediateCommand(
        *_device, _commandPool, _graphicsQueue,
        [&](VkCommandBuffer cmd)
        {
            recordLayoutTransition(cmd, texture->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {texture->width, texture->height, 1};
            vkCmdCopyImageToBuffer(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                                   &region);
            recordLayoutTransition(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
    VK_RESULT_CHECK(vmaInvalidateAllocation(_memoryManager->getAllocator(), readback.allocation, 0, bytes));

    const auto *source = static_cast<const u8 *>(readback.mappedData);
    if (texture->format != VK_FORMAT_R16G16B16A16_SFLOAT)
    {
        std::memcpy(rgba.data(), source, rgba.size());
        return true;
    }
    for (usize i = 0; i < rgba.size(); ++i)
    {
        u16 half = 0;
        std::memcpy(&half, source + i * 2, sizeof(half));
        const f32 value = halfToFloat(half);
        rgba[i] = value > 0.0f ? static_cast<u8>(std::lround(std::min(value, 1.0f) * 255.0f)) : u8{0};
    }
    return true;
}

VkTextureManager::TextureId VkTextureManager::createTexture(u32 width, u32 height, VkFormat format, const void *pixels,
                                                            u32 mipLevels, const SamplerDesc &sampler,
                                                            VkImageUsageFlags extraUsage, TextureId replace)
{
    if (replace > _textures.size())
        replace = kInvalidTextureId;
    const u32 bytesPerTexel = formatBytes(format);
    const VkDeviceSize imageBytes = textureByteSize(width, height, bytesPerTexel);
    if (imageBytes == 0 || _textures.size() >= std::numeric_limits<TextureId>::max())
    {
        INK_ERROR << "VkTextureManager: invalid texture dimensions or exhausted texture ids";
        return kInvalidTextureId;
    }
    const VkPhysicalDeviceProperties *properties = nullptr;
    vmaGetPhysicalDeviceProperties(_memoryManager->getAllocator(), &properties);
    if (width > properties->limits.maxImageDimension2D || height > properties->limits.maxImageDimension2D)
    {
        INK_ERROR << "VkTextureManager: texture dimensions exceed the device limit";
        return kInvalidTextureId;
    }

    TextureData textureData{};
    textureData.width = width;
    textureData.height = height;
    textureData.format = format;
    textureData.bytesPerTexel = bytesPerTexel;
    textureData.mipLevels = mipLevels;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    try
    {
        // NOLINTNEXTLINE(bugprone-signed-bitwise): Vulkan flag bits are C enums.
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        (mipLevels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u) | extraUsage;
        AllocatedImage gpuImage =
            _memoryManager->createImage2D(format, {width, height}, VK_SAMPLE_COUNT_1_BIT, usage, mipLevels);
        textureData.image = gpuImage.image;
        textureData.allocation = gpuImage.allocation;
        textureData.view = createImageView(textureData.image, format, mipLevels);
        textureData.sampler = createSampler(sampler, mipLevels);

        if (pixels)
        {
            void *data = acquireStagingBuffer(imageBytes);
            if (!data)
            {
                destroyTextureData(textureData);
                return kInvalidTextureId;
            }
            std::memcpy(data, pixels, static_cast<usize>(imageBytes));
        }
        commandBuffer = beginUploadCommands();
        if (replace != kInvalidTextureId)
        {
            destroyTextureData(_textures[replace - 1]);
            _textures[replace - 1] = textureData;
        }
        else
            _textures.push_back(textureData);
    }
    catch (...)
    {
        // No commands reference this image until every fallible allocation succeeds.
        destroyTextureData(textureData);
        throw;
    }

    recordLayoutTransition(commandBuffer, textureData.image, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, mipLevels);

    if (pixels)
        recordCopyBufferToImageRegion(commandBuffer, upload().staging.buffer, textureData.image, 0, 0, width, height);
    else
    {
        // Device-side clear avoids staging an entire empty atlas.
        const VkClearColorValue transparentBlack{{0.0f, 0.0f, 0.0f, 0.0f}};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(commandBuffer, textureData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparentBlack,
                             1, &range);
    }

    if (mipLevels > 1)
        recordMipChain(commandBuffer, textureData.image, width, height, mipLevels);
    else
        recordLayoutTransition(commandBuffer, textureData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return replace != kInvalidTextureId ? replace : static_cast<TextureId>(_textures.size());
}

void VkTextureManager::recordMipChain(VkCommandBuffer cmd, VkImage image, u32 width, u32 height, u32 mipLevels) const
{
    auto sourceWidth = static_cast<i32>(width);
    auto sourceHeight = static_cast<i32>(height);
    for (u32 level = 1; level < mipLevels; ++level)
    {
        const i32 targetWidth = std::max(sourceWidth / 2, 1);
        const i32 targetHeight = std::max(sourceHeight / 2, 1);
        recordLayoutTransition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               level - 1);
        const VkImageBlit blit{
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1},
            .srcOffsets = {{0, 0, 0}, {sourceWidth, sourceHeight, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
            .dstOffsets = {{0, 0, 0}, {targetWidth, targetHeight, 1}},
        };
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &blit, VK_FILTER_LINEAR);
        recordLayoutTransition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, level - 1);
        sourceWidth = targetWidth;
        sourceHeight = targetHeight;
    }
    recordLayoutTransition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           mipLevels - 1);
}

void VkTextureManager::updateRegion(TextureId id, u32 x, u32 y, u32 width, u32 height, const u8 *rgba)
{
    updateRegion(id, x, y, width, height, rgba, VK_FORMAT_R8G8B8A8_UNORM);
}

void VkTextureManager::updateCoverageRegion(TextureId id, u32 x, u32 y, u32 width, u32 height, const u8 *coverage)
{
    updateRegion(id, x, y, width, height, coverage, VK_FORMAT_R8_UNORM);
}

void VkTextureManager::updateRegion(TextureId id, u32 x, u32 y, u32 width, u32 height, const u8 *pixels,
                                    VkFormat format)
{
    if (!pixels || width == 0 || height == 0)
        return;

    if (id == kInvalidTextureId || id > _textures.size())
    {
        INK_ERROR << "VkTextureManager: updateRegion on an unknown texture id " << id;
        return;
    }

    TextureData &textureData = _textures[id - 1];
    //! Colour updates are RGBA8 bytes, which an sRGB texture takes as they are; a mip chain would go stale.
    if ((textureData.format == VK_FORMAT_R8_UNORM) != (format == VK_FORMAT_R8_UNORM) ||
        textureData.bytesPerTexel != formatBytes(format) || textureData.mipLevels > 1 || textureData.attachment)
    {
        INK_ERROR << "VkTextureManager: updateRegion does not match the format of texture " << id
                  << ", or the texture is mipmapped";
        return;
    }
    if (!regionFits(x, y, width, height, textureData.width, textureData.height))
    {
        INK_ERROR << "VkTextureManager: updateRegion rectangle exceeds the bounds of texture " << id;
        return;
    }

    const VkDeviceSize regionBytes = textureByteSize(width, height, textureData.bytesPerTexel);

    void *data = acquireStagingBuffer(regionBytes);
    if (!data)
        return;
    std::memcpy(data, pixels, static_cast<usize>(regionBytes));

    // Each patch owns a distinct staging range until this batch's fence completes.
    VkCommandBuffer commandBuffer = beginUploadCommands();
    recordLayoutTransition(commandBuffer, textureData.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    recordCopyBufferToImageRegion(commandBuffer, upload().staging.buffer, textureData.image, x, y, width, height);
    recordLayoutTransition(commandBuffer, textureData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

const VkTextureManager::TextureData *VkTextureManager::getTexture(TextureId id) const noexcept
{
    if (id == kInvalidTextureId || id > _textures.size())
        return nullptr;
    return &_textures[id - 1];
}

void VkTextureManager::retireUpload(UploadSlot &slot)
{
    if (slot.state == UploadSlot::State::Submitted)
    {
        VK_RESULT_CHECK(vkWaitForFences(*_device, 1, &slot.fence, VK_TRUE, UINT64_MAX));
        slot.state = UploadSlot::State::Idle;
        slot.used = 0;
    }
}

void VkTextureManager::releaseStagingBuffer()
{
    auto &slot = upload();
    if (slot.manuallyMapped)
        _memoryManager->unmap(slot.staging);
    _memoryManager->destroyBuffer(slot.staging);
    slot.capacity = 0;
    slot.used = 0;
    slot.manuallyMapped = false;
}

void *VkTextureManager::acquireStagingBuffer(VkDeviceSize bytes)
{
    if (!bytes || bytes > std::numeric_limits<usize>::max())
        return nullptr;
    //! A copy's buffer offset must be a multiple of its texel size, and R8 patches leave odd ends:
    //! 16 covers every format here (and a compressed block). Padding counts when deciding to rotate.
    constexpr VkDeviceSize kAlignment = 16;
    const VkDeviceSize padding = (kAlignment - upload().used % kAlignment) % kAlignment;
    if (upload().state == UploadSlot::State::Recording && upload().used != 0 &&
        (bytes > upload().capacity - upload().used || padding > upload().capacity - upload().used - bytes))
        flushUploads();
    retireUpload(upload());
    auto &slot = upload();
    if (slot.state != UploadSlot::State::Recording)
        slot.used = 0;
    if (bytes > slot.capacity)
    {
        releaseStagingBuffer();
        const VkDeviceSize capacity = std::max(bytes, VkDeviceSize{INK_MIB_TO_BYTES(4)});
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
    _stagingOffset = slot.used + (kAlignment - slot.used % kAlignment) % kAlignment;
    slot.used = _stagingOffset + bytes;
    return static_cast<u8 *>(slot.staging.mappedData) + _stagingOffset;
}

void VkTextureManager::cleanup()
{
    flushUploads();
    for (usize i = 0; i < _uploads.size(); ++i)
    {
        _uploadIndex = i;
        auto &slot = upload();
        retireUpload(slot);
        releaseStagingBuffer();
        if (slot.command)
            vkFreeCommandBuffers(*_device, _commandPool, 1, &slot.command);
        if (slot.fence)
            vkDestroyFence(*_device, slot.fence, nullptr);
        slot = {};
    }
    _uploadIndex = 0;
    for (auto &texture : _textures)
        destroyTextureData(texture);
    _textures.clear();
}

VkImageView VkTextureManager::createImageView(VkImage image, VkFormat format, u32 mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    if (format == VK_FORMAT_R8_UNORM)
        viewInfo.components = {VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE,
                               VK_COMPONENT_SWIZZLE_R};
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    VK_RESULT_CHECK(vkCreateImageView(*_device, &viewInfo, nullptr, &imageView));
    return imageView;
}

VkSampler VkTextureManager::createSampler(const SamplerDesc &sampler, u32 mipLevels)
{
    const f32 anisotropy = std::clamp(sampler.maxAnisotropy, 1.0f, _maxAnisotropy);
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = vulkanFilter(sampler.magFilter);
    samplerInfo.minFilter = vulkanFilter(sampler.minFilter);
    samplerInfo.addressModeU = vulkanWrap(sampler.wrapU);
    samplerInfo.addressModeV = vulkanWrap(sampler.wrapV);
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = anisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode =
        sampler.mipFilter == TextureFilter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<f32>(mipLevels - 1);

    VkSampler handle = VK_NULL_HANDLE;
    VK_RESULT_CHECK(vkCreateSampler(*_device, &samplerInfo, nullptr, &handle));
    return handle;
}

VkCommandBuffer VkTextureManager::beginUploadCommands()
{
    auto &slot = upload();
    if (slot.state == UploadSlot::State::Recording)
        return slot.command;
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
    slot.state = UploadSlot::State::Recording;
    return slot.command;
}

void VkTextureManager::flushUploads()
{
    auto &slot = upload();
    if (slot.state != UploadSlot::State::Recording)
        return;
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
    slot.state = UploadSlot::State::Submitted;
    _uploadIndex = (_uploadIndex + 1) % _uploads.size();
}

void VkTextureManager::recordLayoutTransition(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                                              VkImageLayout newLayout, u32 baseMip, u32 levelCount) const
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        //! Re-entering the transfer state to patch an already sampleable
        //! texture, as the glyph atlas does whenever a new character appears.
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        //! A render target read back: its last writer was a render pass, which ends at SHADER_READ_ONLY.
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        //! A mip level just written becomes the next level's blit source.
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        throw AuraException("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VkTextureManager::recordCopyBufferToImageRegion(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, u32 x, u32 y,
                                                     u32 width, u32 height) const
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

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

} // namespace vk
} // namespace aura3d
