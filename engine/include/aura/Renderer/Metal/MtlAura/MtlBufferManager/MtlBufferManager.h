#ifndef MTLBUFFERMANAGER_H
#define MTLBUFFERMANAGER_H

#pragma once

#include <cstddef>

#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"

namespace aura3d {
namespace mtl {

/**
 * @class MtlBufferManager
 *
 * @brief The one place a MTLBuffer is allocated, and the one place a storage
 *        mode is chosen.
 *
 * The Metal counterpart of VkBufferManager + VulkanMemoryManager: Metal has no
 * separate allocator to configure and no memory type to search for, so what is
 * left is the policy VMA would otherwise be told -- where a buffer's bytes
 * should live -- plus the size check Metal itself does not perform.
 *
 * Deliberately owns no buffers. The vertex, index and texture managers each own
 * theirs, keyed by the handles they hand out; this class only creates them, so
 * the overlay's per-frame buffers (owned directly by MetalRenderer, and resized
 * rather than pooled) can come from the same code path.
 */
class MtlBufferManager {
public:
    /**
     * @brief Records the device and the allocation policy derived from it.
     *
     * @param[in] device Device buffers are allocated on; must outlive this object.
     * @param[in] unifiedMemory MtlDeviceManager::hasUnifiedMemory(), which
     *        selects the storage mode uploaded data lands in.
     * @param[in] maxBufferLength MtlDeviceManager::maxBufferLength(), the size
     *        beyond which an allocation is refused up front.
     *
     * @throws AuraException if @p device is null.
     */
    MtlBufferManager(MTL::Device* device, bool unifiedMemory, NS::UInteger maxBufferLength);

    ~MtlBufferManager();

    MtlBufferManager(const MtlBufferManager&) = delete;
    MtlBufferManager& operator=(const MtlBufferManager&) = delete;

    /**
     * @brief Allocates a buffer holding a copy of @p bytes.
     *
     * For data uploaded once and then only read by the GPU: vertices, indices.
     * The copy happens inside Metal, so @p bytes may be freed as soon as this
     * returns.
     *
     * @param[in] bytes Source data; must be non-null when @p byteSize is nonzero.
     * @param[in] byteSize Number of bytes to upload.
     * @param[in] label Debug label, shown in Xcode's GPU debugger.
     * @return The buffer, or an empty handle when @p byteSize is 0, exceeds the
     *         device's limit, or the allocation fails. Each case is logged.
     */
    [[nodiscard]] NS::SharedPtr<MTL::Buffer> createFromBytes(const void* bytes,
                                                           size_t byteSize,
                                                           const char* label) const;

    /**
     * @brief Allocates an empty CPU-writable buffer of @p byteSize bytes.
     *
     * For data rewritten every frame -- the overlay's geometry -- which is why
     * this is always shared storage regardless of hasUnifiedMemory(): the CPU
     * must be able to write it without a staging copy, and an overlay batch is
     * far too small for a per-frame blit to pay for itself. contents() on the
     * result is writable for the buffer's whole life.
     *
     * @param[in] byteSize Capacity in bytes.
     * @param[in] label Debug label.
     * @return The buffer, or an empty handle on the same failures as
     *         createFromBytes().
     */
    [[nodiscard]] NS::SharedPtr<MTL::Buffer> createDynamic(size_t byteSize,
                                                          const char* label) const;

    /**
     * @brief Storage mode uploaded, GPU-read-only data is placed in.
     *
     * Shared on unified memory (every Apple-silicon Mac and every iOS device):
     * one physical copy both processors read, so there is nothing to stage.
     * Managed on the discrete/Intel Macs: the buffer gets a GPU-side copy in
     * VRAM that Metal keeps in sync, which beats leaving geometry on the far
     * side of PCIe without needing an explicit blit encoder here.
     */
    [[nodiscard]] MTL::ResourceOptions uploadResourceOptions() const noexcept;

private:
    /**
     * @brief Rejects a zero-sized or over-limit allocation, logging why.
     * @return true when @p byteSize may be handed to Metal.
     */
    [[nodiscard]] bool validateSize(size_t byteSize, const char* label) const;

    MTL::Device* _device = nullptr; //! Borrowed; owned by MtlDeviceManager.
    bool _unifiedMemory = false;
    NS::UInteger _maxBufferLength = 0;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLBUFFERMANAGER_H
