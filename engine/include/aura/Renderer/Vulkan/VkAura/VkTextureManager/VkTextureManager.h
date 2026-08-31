#ifndef VKTEXTUREMANAGER_H
#define VKTEXTUREMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

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
     * The image is created, cleared to transparent black and left in
     * SHADER_READ_ONLY_OPTIMAL, so it is immediately safe to sample and to
     * describe in a descriptor set. Because the VkImage, view and sampler never
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

private:
    VulkanMemoryManager* _memoryManager;
    VkDevice* _device;
    VkCommandPool _commandPool;
    VkQueue _graphicsQueue;
    //! Dense, id-indexed (id - 1). Textures are never individually removed,
    //! only dropped wholesale by cleanup(), so ids stay stable for the
    //! manager's lifetime and no free-list or generation counter is needed.
    std::vector<TextureData> _textures;

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
     * @brief Ends @p commandBuffer and submits it, without waiting for it.
     *
     * The wait is deferred to waitForPendingUpload(), which the next writer of
     * the staging buffer performs. Waiting here instead cost a full CPU/GPU
     * round trip *per call*, which for an application re-uploading a dynamic
     * texture every frame -- the documented use of createDynamicTexture() --
     * was the single largest item in the frame.
     *
     * Nothing is lost by not waiting: the recorded barrier transitioning the
     * image back to SHADER_READ_ONLY_OPTIMAL orders the copy ahead of any later
     * sampling in submission order, and every frame is submitted to this same
     * queue. What the wait actually protected was the shared staging buffer,
     * and that is exactly what the deferred wait still protects.
     *
     * @param commandBuffer The command buffer to submit.
     */
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    /**
     * @brief Blocks until the previous upload has retired, then frees it.
     *
     * A no-op when nothing is in flight, so it is cheap to call defensively
     * before anything that overwrites the staging buffer or tears state down.
     * In steady state the previous upload retired frames ago and this returns
     * immediately -- which is the whole point of deferring it.
     */
    void waitForPendingUpload();

    /**
     * @brief Records an image layout transition into @p cmd.
     *
     * Records only -- submission is the caller's business, which is what lets
     * an upload put its two transitions and the copy between them into a single
     * command buffer instead of three separately submitted ones.
     */
    void recordLayoutTransition(VkCommandBuffer cmd, VkImage image,
                                VkImageLayout oldLayout, VkImageLayout newLayout) const;

    /**
     * @brief Records a buffer -> image sub-rectangle copy into @p cmd.
     *
     * A whole-image upload is this with a zero offset and the image's extent.
     */
    void recordCopyBufferToImageRegion(VkCommandBuffer cmd, VkBuffer buffer, VkImage image,
                                       u32 x, u32 y, u32 width, u32 height) const;

    /**
     * @brief Returns a host-visible staging buffer of at least @p bytes,
     *        together with a pointer to write into.
     *
     * One buffer is kept and handed out repeatedly rather than created and
     * destroyed per upload: a VMA allocation plus a map/unmap pair costs more
     * than the memcpy it exists to serve, and the glyph atlas performs one
     * upload per newly seen character. The buffer only ever grows, so a scene
     * settles on its largest texture and stops allocating.
     *
     * Reusing a single buffer is safe because this waits for the previous
     * upload before handing the pointer out: waitForPendingUpload() is called
     * on the way in, so the GPU is provably finished reading the staging memory
     * before the next caller can overwrite it. That wait -- not one at
     * submission time -- is what the single buffer actually needs.
     *
     * @param bytes Required capacity.
     * @return Mapped write pointer, or nullptr if the allocation failed.
     */
    [[nodiscard]] void* acquireStagingBuffer(VkDeviceSize bytes);

    //! Unmaps (when this class did the mapping) and destroys the staging
    //! buffer, resetting every field that describes it. Safe on an empty one.
    void releaseStagingBuffer();

    void destroyTextureData(TextureData& texture);

    //! Reused by every upload; see acquireStagingBuffer(). Freed by cleanup().
    AllocatedBuffer _staging{};
    VkDeviceSize _stagingCapacity = 0;
    //! Write pointer for _staging, established once when it is created.
    void* _stagingMapped = nullptr;
    //! True when _stagingMapped came from an explicit map() this class must
    //! balance, rather than from VMA's persistent mapping.
    bool _stagingManuallyMapped = false;

    /**
     * @brief Fence waitForPendingUpload() waits on, created once on first use.
     *
     * A single fence suffices because uploads through this manager are strictly
     * serialized -- each waits for the one before it on the way in -- so there
     * is never more than one in flight to track.
     */
    VkFence _uploadFence = VK_NULL_HANDLE;

    //! The submitted-but-not-yet-waited-for upload, held so waitForPendingUpload()
    //! can free it once the GPU is done. VK_NULL_HANDLE when nothing is in flight.
    VkCommandBuffer _pendingUploadCmd = VK_NULL_HANDLE;
};

} // namespace vk
} // namespace aura3d

#endif // VKTEXTUREMANAGER_H
