#ifndef VKTEXTUREMANAGER_H
#define VKTEXTUREMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <array>

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
    //! Dense 1-based identifier issued by the create* methods; 0 means "none".
    using TextureId = u32;
    static constexpr TextureId kInvalidTextureId = 0;

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
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     * @param a Alpha component (0-255, default 255)
     * @return Id of the new texture, or kInvalidTextureId on failure.
     */
    TextureId createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255);

    /**
     * @brief Uploads arbitrary RGBA8 pixel data through a staging buffer.
     *
     * The general upload path: createSolidColorTexture() is a 1x1 case of it.
     *
     * @param rgba Tightly packed @p width * @p height * 4 bytes, RGBA order.
     * @param width Texture width in pixels.
     * @param height Texture height in pixels.
     * @return Id of the new texture, or kInvalidTextureId when the input is empty.
     */
    TextureId createTextureFromPixels(const u8* rgba, u32 width, u32 height);

    /**
     * @brief Allocates an empty texture whose texels are written later by
     *        updateRegion().
     *
     * Records a clear to transparent black and a transition to
     * SHADER_READ_ONLY_OPTIMAL. Call flushUploads() before submitting draws
     * that sample it. Because the VkImage, view and sampler never
     * change afterwards, the descriptor sets pointing at it stay valid for the
     * texture's whole life -- which is what makes an atlas that keeps growing
     * affordable on this backend.
     *
     * @param width Texture width in pixels.
     * @param height Texture height in pixels.
     * @return Id of the new texture, or kInvalidTextureId on failure.
     */
    TextureId createDynamicTexture(u32 width, u32 height);

    /**
     * @brief Uploads RGBA8 texels into a sub-rectangle of an existing texture.
     *
     * Stages through a host-visible buffer and copies only the named rectangle,
     * transitioning the image to TRANSFER_DST and back around the copy.
     * Patches are batched until flushUploads() or staging capacity is exhausted.
     *
     * @param id Identifier of the texture to update.
     * @param x Left edge of the destination rectangle, in pixels.
     * @param y Top edge of the destination rectangle, in pixels.
     * @param width Rectangle width in pixels.
     * @param height Rectangle height in pixels.
     * @param rgba Tightly packed @p width * @p height * 4 bytes.
     */
    void updateRegion(TextureId id,
                      u32 x, u32 y, u32 width, u32 height,
                      const u8* rgba);

    /**
     * @brief Retrieves a texture by id.
     *
     * Ids are dense and allocated sequentially by the create* methods, so this
     * is a bounds check and an indexed load -- no hashing, and nothing to
     * allocate at the call site. It is called on the per-frame descriptor
     * path, which is why it is not keyed by a string name.
     *
     * @param id The identifier of the texture to retrieve.
     * @return Pointer to the texture data, or nullptr if @p id was never issued.
     */
    [[nodiscard]] const TextureData* getTexture(TextureId id) const noexcept;

    //! Number of ids issued so far; valid ids are [1, textureCount()].
    [[nodiscard]] u32 textureCount() const noexcept { return static_cast<u32>(_textures.size()); }

    /**
     * @brief Cleans up all texture resources
     *
     * Destroys all textures and frees their associated memory.
     */
    void cleanup();

    /**
     * @brief Submits the batch of uploads recorded since the last flush,
     *        without waiting for it.
     *
     * The renderer calls this at the top of endFrame(), ahead of the frame's
     * own submit on the same queue, so submission order plus the recorded
     * barriers make every patch visible to that frame's sampling. A no-op
     * when nothing was recorded.
     */
    void flushUploads();

private:
    /**
     * @brief One in-flight upload batch: its staging memory, command buffer
     *        and the fence that says the GPU is done reading both.
     *
     * Three of these rotate so that recording the next batch never waits on
     * the previous one; a slot is only reused after its fence has signalled.
     */
    struct UploadSlot {
        //! Host-visible, persistently mapped; patches are packed into it in order.
        AllocatedBuffer staging{};
        //! Bytes @c staging can hold. Zero until the first upload sizes it.
        VkDeviceSize capacity = 0;
        //! Bytes handed out from @c staging for the batch being recorded.
        VkDeviceSize used = 0;
        //! Primary buffer the batch records into; allocated once, reset per batch.
        VkCommandBuffer command = VK_NULL_HANDLE;
        //! Signalled when the submitted batch has retired; created on first submit.
        VkFence fence = VK_NULL_HANDLE;
        //! Submitted and not yet waited for: @c staging must not be overwritten.
        bool pending = false;
        //! @c command is begun and accepting more patches.
        bool recording = false;
        //! @c staging was mapped by an explicit map() this class must unmap,
        //! rather than by VMA's persistent mapping.
        bool manuallyMapped = false;
    };

    //! The slot the next patch goes into.
    UploadSlot& upload() noexcept { return _uploads[_uploadIndex]; }

    //! Blocks until @p slot's submitted batch has retired, freeing its staging range.
    //! A no-op when nothing is pending.
    void retireUpload(UploadSlot& slot);

    /**
     * @brief Returns the current slot's command buffer, beginning a new batch
     *        if none is open.
     *
     * Every transition/copy pair of a create* or updateRegion() call lands in
     * this one buffer, so a frame that touches many atlas pages costs a single
     * submit instead of one per page.
     */
    VkCommandBuffer beginUploadCommands();

    /**
     * @brief Reserves @p bytes of staging memory and returns a write pointer.
     *
     * Packs into the current slot when it fits; otherwise submits the open
     * batch and moves to the next slot, waiting on that slot's fence first.
     * Sets the offset recordCopyBufferToImageRegion() copies from.
     *
     * @return Mapped pointer, or nullptr when @p bytes is zero or the slot
     *         could not be allocated or mapped.
     */
    [[nodiscard]] void* acquireStagingBuffer(VkDeviceSize bytes);

    //! Unmaps (when this class did the mapping) and destroys the current
    //! slot's staging buffer. Safe on an empty slot.
    void releaseStagingBuffer();

    //! 2D colour view over @p image with a single mip level.
    VkImageView createImageView(VkImage image, VkFormat format);

    //! Linear filtering, repeat addressing, no anisotropy; shared by every texture.
    VkSampler createSampler();

    //! Destroys the image, view, sampler and allocation of @p texture.
    void destroyTextureData(TextureData& texture);

    /**
     * @brief Records an image layout transition into @p cmd.
     *
     * Records only -- submission is the caller's business, which is what lets
     * an upload put its two transitions and the copy between them into a single
     * command buffer.
     */
    void recordLayoutTransition(VkCommandBuffer cmd, VkImage image,
                                VkImageLayout oldLayout, VkImageLayout newLayout) const;

    /**
     * @brief Records a buffer -> image sub-rectangle copy into @p cmd, reading
     *        from the staging offset acquireStagingBuffer() last handed out.
     *
     * A whole-image upload is this with a zero origin and the image's extent.
     */
    void recordCopyBufferToImageRegion(VkCommandBuffer cmd, VkBuffer buffer, VkImage image,
                                      u32 x, u32 y, u32 width, u32 height) const;

    VulkanMemoryManager* _memoryManager;
    VkDevice* _device;
    //! Upload pool from VkCommandManager; created with the reset bit so batch
    //! buffers can be recycled without freeing them.
    VkCommandPool _commandPool;
    VkQueue _graphicsQueue;
    //! Dense, id-indexed (id - 1). Textures are never individually removed,
    //! only dropped wholesale by cleanup(), so ids stay stable for the
    //! manager's lifetime and no free-list or generation counter is needed.
    std::vector<TextureData> _textures;
    //! Rotating batches. A slot collects any number of patches; reuse waits on
    //! its fence, never on a frame count, so it is correct outside a frame too.
    std::array<UploadSlot, 3> _uploads{};
    //! Index into @c _uploads of the slot currently recording or next to record.
    usize _uploadIndex = 0;
    //! Byte offset of the most recent acquireStagingBuffer() range, consumed
    //! by the copy recorded right after it.
    VkDeviceSize _stagingOffset = 0;
};

} // namespace vk
} // namespace aura3d

#endif
