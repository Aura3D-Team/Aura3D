#ifndef MTLVERTEXBUFFERMANAGER_H
#define MTLVERTEXBUFFERMANAGER_H

#pragma once

#include <span>
#include <vector>

#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"
#include "aura/Renderer/Metal/MtlAura/MtlBufferManager/MtlBufferManager.h"
#include "aura/Renderer/RenderHandles.h"

namespace aura3d {
namespace mtl {

/**
 * @class MtlVertexBufferManager
 *
 * @brief Owns every uploaded vertex buffer and resolves a VertexBufferHandle to
 *        one in constant time.
 *
 * The Metal counterpart of VkVertexBufferManager, with one deliberate
 * difference: buffers are stored in a vector indexed by handle rather than in a
 * map keyed by a synthetic "vb_N" string. VulkanRenderer had to keep a
 * handle-indexed mirror alongside its name map for exactly this reason (see its
 * _vbByHandle comment -- resolving through the map cost an integer hash to
 * obtain a std::string and then a string hash inside the manager, per buffer per
 * draw); starting from the dense form leaves no second structure to keep in sync.
 *
 * Handles are 1-based, matching the engine-wide convention that a valid handle
 * is never 0 (see isValidHandle()).
 */
class MtlVertexBufferManager {
public:
    /**
     * @brief Binds this manager to the allocator its buffers come from.
     * @param[in] allocator Allocation primitive; must outlive this object.
     * @throws AuraException if @p allocator is null.
     */
    explicit MtlVertexBufferManager(MtlBufferManager* allocator);

    ~MtlVertexBufferManager();

    MtlVertexBufferManager(const MtlVertexBufferManager&) = delete;
    MtlVertexBufferManager& operator=(const MtlVertexBufferManager&) = delete;

    /**
     * @brief Uploads @p vertices and returns a handle to the result.
     *
     * @param[in] vertices Geometry to upload; an empty span is rejected.
     * @return A 1-based handle, or INVALID_HANDLE when the upload failed.
     */
    [[nodiscard]] VertexBufferHandle create(std::span<const gfx::Vertex3D> vertices);

    /**
     * @brief Resolves @p handle to its buffer.
     * @return The buffer, or nullptr for a handle that was never created (which
     *         is the "skip this bind" case the draw path already handles).
     */
    [[nodiscard]] MTL::Buffer* resolve(VertexBufferHandle handle) const noexcept;

    //! Releases every buffer. Every handle issued before this becomes stale.
    void clear() noexcept;

    //! Number of buffers currently held, for logging.
    [[nodiscard]] size_t count() const noexcept { return _buffers.size(); }

private:
    MtlBufferManager* _allocator = nullptr; //! Borrowed; owned by MetalRenderer.

    /*
     * Handle N lives at index N - 1. A failed upload still occupies its slot,
     * holding an empty handle, so the handles issued afterwards keep referring to
     * the buffers they were issued for.
     */
    std::vector<NS::SharedPtr<MTL::Buffer>> _buffers;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLVERTEXBUFFERMANAGER_H
