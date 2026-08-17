#ifndef MTLINDEXBUFFERMANAGER_H
#define MTLINDEXBUFFERMANAGER_H

#pragma once

#include <span>
#include <vector>

#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"
#include "aura/Renderer/Metal/MtlAura/MtlBufferManager/MtlBufferManager.h"
#include "aura/Renderer/RenderHandles.h"

namespace aura3d {
namespace mtl {

/**
 * @struct IndexBufferRecord
 * @brief An index buffer together with everything drawIndexedPrimitives() needs
 *        to consume it.
 *
 * The index *type* has to travel with the buffer: IRenderer exposes both 16- and
 * 32-bit index uploads, and Metal takes the width as a draw-call argument rather
 * than binding it with the buffer the way vkCmdBindIndexBuffer does. Pairing
 * them here is what stops a 16-bit buffer from being drawn as 32-bit -- a
 * mismatch that reads twice the memory and renders garbage instead of failing.
 */
struct IndexBufferRecord {
    MTL::Buffer* buffer = nullptr;                 //! Borrowed from the owning manager.
    MTL::IndexType indexType = MTL::IndexTypeUInt32;
    u32 indexCount = 0;                            //! Indices the buffer holds.
};

/**
 * @class MtlIndexBufferManager
 *
 * @brief Owns every uploaded index buffer and resolves an IndexBufferHandle to
 *        the buffer plus its index type.
 *
 * The Metal counterpart of VkIndexBufferManager. Storage and handle conventions
 * match MtlVertexBufferManager exactly -- see its class comment for why the
 * records live in a handle-indexed vector.
 */
class MtlIndexBufferManager {
public:
    /**
     * @brief Binds this manager to the allocator its buffers come from.
     * @param[in] allocator Allocation primitive; must outlive this object.
     * @throws AuraException if @p allocator is null.
     */
    explicit MtlIndexBufferManager(MtlBufferManager* allocator);

    ~MtlIndexBufferManager();

    MtlIndexBufferManager(const MtlIndexBufferManager&) = delete;
    MtlIndexBufferManager& operator=(const MtlIndexBufferManager&) = delete;

    /**
     * @brief Uploads 16-bit indices.
     * @param[in] indices Indices to upload; an empty span is rejected.
     * @return A 1-based handle, or an invalid handle when the upload failed.
     */
    [[nodiscard]] IndexBufferHandle create(std::span<const u16> indices);

    /**
     * @brief Uploads 32-bit indices.
     * @param[in] indices Indices to upload; an empty span is rejected.
     * @return A 1-based handle, or an invalid handle when the upload failed.
     */
    [[nodiscard]] IndexBufferHandle create(std::span<const u32> indices);

    /**
     * @brief Resolves @p handle to its record.
     * @return The record, or nullptr for a handle that was never created.
     */
    [[nodiscard]] const IndexBufferRecord* resolve(IndexBufferHandle handle) const noexcept;

    //! Releases every buffer. Every handle issued before this becomes stale.
    void clear() noexcept;

    //! Number of buffers currently held, for logging.
    [[nodiscard]] size_t count() const noexcept { return _records.size(); }

private:
    /**
     * @brief The shared body of both create() overloads.
     *
     * @param[in] bytes Index data.
     * @param[in] byteSize Its size in bytes.
     * @param[in] indexType Width Metal should read the indices at.
     * @param[in] indexCount Number of indices.
     * @return A 1-based handle, or an invalid handle on failure.
     */
    [[nodiscard]] IndexBufferHandle upload(const void* bytes, size_t byteSize,
                                          MTL::IndexType indexType, u32 indexCount);

    MtlBufferManager* _allocator = nullptr; //! Borrowed; owned by MetalRenderer.

    //! Handle N lives at index N - 1; see MtlVertexBufferManager::_buffers.
    std::vector<NS::SharedPtr<MTL::Buffer>> _buffers;
    std::vector<IndexBufferRecord> _records;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLINDEXBUFFERMANAGER_H
