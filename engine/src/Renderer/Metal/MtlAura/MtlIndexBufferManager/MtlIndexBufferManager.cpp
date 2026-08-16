#include "aura/Renderer/Metal/MtlAura/MtlIndexBufferManager/MtlIndexBufferManager.h"

#include <ink/ink.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace mtl {

MtlIndexBufferManager::MtlIndexBufferManager(MtlBufferManager* allocator)
    : _allocator(allocator)
{
    if (!_allocator)
        throw AuraException("MtlIndexBufferManager: buffer allocator is null");
}

MtlIndexBufferManager::~MtlIndexBufferManager()
{
    clear();
    _allocator = nullptr;
}

IndexBufferHandle MtlIndexBufferManager::create(std::span<const u16> indices)
{
    return upload(indices.data(), indices.size_bytes(), MTL::IndexTypeUInt16,
                  static_cast<u32>(indices.size()));
}

IndexBufferHandle MtlIndexBufferManager::create(std::span<const u32> indices)
{
    return upload(indices.data(), indices.size_bytes(), MTL::IndexTypeUInt32,
                  static_cast<u32>(indices.size()));
}

IndexBufferHandle MtlIndexBufferManager::upload(const void* bytes, size_t byteSize,
                                               MTL::IndexType indexType, u32 indexCount)
{
    if (indexCount == 0) {
        INK_WARN << "MtlIndexBufferManager: refusing to upload an empty index span";
        return INVALID_HANDLE;
    }

    NS::SharedPtr<MTL::Buffer> buffer =
        _allocator->createFromBytes(bytes, byteSize, "Aura3D index buffer");

    if (!buffer)
        return INVALID_HANDLE;

    /*
     * The record borrows the buffer that _buffers owns; the two vectors are
     * pushed together and cleared together, so a record can never outlive its
     * buffer.
     */
    _records.push_back(IndexBufferRecord{buffer.get(), indexType, indexCount});
    _buffers.push_back(std::move(buffer));

    return static_cast<IndexBufferHandle>(_records.size()); // 1-based
}

const IndexBufferRecord* MtlIndexBufferManager::resolve(IndexBufferHandle handle) const noexcept
{
    if (!isValidHandle(handle))
        return nullptr;

    const size_t index = static_cast<size_t>(handle) - 1;
    if (index >= _records.size())
        return nullptr;

    return &_records[index];
}

void MtlIndexBufferManager::clear() noexcept
{
    //! Records first: they hold raw pointers into the buffers released below.
    _records.clear();
    _buffers.clear();
}

} // namespace mtl
} // namespace aura3d
