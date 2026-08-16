#include "aura/Renderer/Metal/MtlAura/MtlDeviceManager/MtlDeviceManager.h"

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace mtl {

MtlDeviceManager::MtlDeviceManager()
{
    /*
     * CreateSystemDefaultDevice returns an owned reference (it is the
     * MTLCreateSystemDefaultDevice C entry point, not an autoreleased
     * convenience method), so it is adopted rather than retained.
     */
    _device = adopt(MTL::CreateSystemDefaultDevice());
    if (!_device)
        throw AuraException("MtlDeviceManager: no Metal device available on this system");

    _commandQueue = adopt(_device->newCommandQueue());
    if (!_commandQueue)
        throw AuraException("MtlDeviceManager: failed to create the Metal command queue");

    const NS::String* name = _device->name();
    const char* utf8Name = name ? name->utf8String() : nullptr;
    _deviceName = utf8Name ? utf8Name : "unnamed Metal device";

    _maxBufferLength = _device->maxBufferLength();
    _unifiedMemory = _device->hasUnifiedMemory();

    INK_INFO << "Metal device: " << _deviceName
             << " | unified memory: " << (_unifiedMemory ? "yes" : "no")
             << " | max buffer: " << (_maxBufferLength / (1024ull * 1024ull)) << " MiB";
}

MtlDeviceManager::~MtlDeviceManager()
{
    /*
     * Release order matters: the queue holds a reference to the device, so it
     * goes first. Both are NS::SharedPtr, so this is the only ordering
     * decision -- there is no manual release to get wrong.
     */
    _commandQueue.reset();
    _device.reset();
    INK_DEBUG << "MtlDeviceManager destroyed";
}

} // namespace mtl
} // namespace aura3d
