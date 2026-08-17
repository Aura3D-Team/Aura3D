#include "aura/Renderer/Metal/MtlAura/MtlVertexBufferManager/MtlVertexBufferManager.h"

#include <ink/ink.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace mtl {

MtlVertexBufferManager::MtlVertexBufferManager(MtlBufferManager* allocator)
    : _allocator(allocator)
{
    if (!_allocator)
        throw AuraException("MtlVertexBufferManager: buffer allocator is null");
}

MtlVertexBufferManager::~MtlVertexBufferManager()
{
    clear();
    _allocator = nullptr;
}

VertexBufferHandle MtlVertexBufferManager::create(std::span<const gfx::Vertex3D> vertices)
{
    if (vertices.empty()) {
        INK_WARN << "MtlVertexBufferManager: refusing to upload an empty vertex span";
        return {};
    }

    NS::SharedPtr<MTL::Buffer> buffer = _allocator->createFromBytes(
        vertices.data(), vertices.size_bytes(), "Aura3D vertex buffer");

    if (!buffer)
        return {};

    _buffers.push_back(std::move(buffer));
    return static_cast<VertexBufferHandle>(_buffers.size()); // 1-based
}

MTL::Buffer* MtlVertexBufferManager::resolve(VertexBufferHandle handle) const noexcept
{
    if (!isValidHandle(handle))
        return nullptr;

    const size_t index = static_cast<size_t>(handle.value()) - 1;
    if (index >= _buffers.size())
        return nullptr;

    return _buffers[index].get();
}

void MtlVertexBufferManager::clear() noexcept
{
    _buffers.clear();
}

} // namespace mtl
} // namespace aura3d
