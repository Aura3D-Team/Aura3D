#include "aura/Renderer/Metal/MtlAura/MtlBufferManager/MtlBufferManager.h"

#include <ink/ink.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace mtl {

MtlBufferManager::MtlBufferManager(MTL::Device* device, bool unifiedMemory,
                                  NS::UInteger maxBufferLength)
    : _device(device), _unifiedMemory(unifiedMemory), _maxBufferLength(maxBufferLength)
{
    if (!_device)
        throw AuraException("MtlBufferManager: device is null");
}

MtlBufferManager::~MtlBufferManager()
{
    _device = nullptr;
}

MTL::ResourceOptions MtlBufferManager::uploadResourceOptions() const noexcept
{
    return _unifiedMemory ? MTL::ResourceStorageModeShared : MTL::ResourceStorageModeManaged;
}

bool MtlBufferManager::validateSize(size_t byteSize, const char* label) const
{
    if (byteSize == 0) {
        INK_WARN << "MtlBufferManager: refusing a zero-sized allocation for '" << label << "'";
        return false;
    }

    /*
     * Metal answers an over-limit request with nil and no diagnostic of its own,
     * which surfaces much later as an unexplained missing mesh. Checking here
     * turns that into one actionable line at the point of upload.
     */
    if (_maxBufferLength != 0 && static_cast<NS::UInteger>(byteSize) > _maxBufferLength) {
        INK_ERROR << "MtlBufferManager: '" << label << "' needs " << byteSize
                  << " bytes but the device caps a single buffer at " << _maxBufferLength;
        return false;
    }

    return true;
}

NS::SharedPtr<MTL::Buffer> MtlBufferManager::createFromBytes(const void* bytes,
                                                            size_t byteSize,
                                                            const char* label) const
{
    if (!validateSize(byteSize, label))
        return nullptr;

    if (!bytes) {
        INK_ERROR << "MtlBufferManager: '" << label << "' was given a null source pointer";
        return nullptr;
    }

    NS::SharedPtr<MTL::Buffer> buffer = adopt(_device->newBuffer(
        bytes, static_cast<NS::UInteger>(byteSize), uploadResourceOptions()));

    if (!buffer) {
        INK_ERROR << "MtlBufferManager: the device refused a " << byteSize
                  << "-byte allocation for '" << label << "'";
        return nullptr;
    }

    const NS::SharedPtr<NS::String> bufferLabel = makeString(label);
    buffer->setLabel(bufferLabel.get());

    /*
     * Managed storage keeps two copies (a CPU-side one and a VRAM one) and needs
     * to be told which bytes changed before the GPU reads them. newBuffer's own
     * initial copy is covered by Metal, but marking the range explicitly is what
     * keeps that guarantee from depending on an implementation detail -- and it
     * is a no-op on the shared path, where there is only one copy.
     */
    if (!_unifiedMemory)
        buffer->didModifyRange(NS::Range::Make(0, static_cast<NS::UInteger>(byteSize)));

    return buffer;
}

NS::SharedPtr<MTL::Buffer> MtlBufferManager::createDynamic(size_t byteSize,
                                                          const char* label) const
{
    if (!validateSize(byteSize, label))
        return nullptr;

    NS::SharedPtr<MTL::Buffer> buffer = adopt(_device->newBuffer(
        static_cast<NS::UInteger>(byteSize), MTL::ResourceStorageModeShared));

    if (!buffer) {
        INK_ERROR << "MtlBufferManager: the device refused a " << byteSize
                  << "-byte dynamic allocation for '" << label << "'";
        return nullptr;
    }

    const NS::SharedPtr<NS::String> bufferLabel = makeString(label);
    buffer->setLabel(bufferLabel.get());

    return buffer;
}

} // namespace mtl
} // namespace aura3d
