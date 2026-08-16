#include "aura/Renderer/Metal/MtlAura/MtlLayerManager/MtlLayerManager.h"

#include <algorithm>

#include <ink/ink.hpp>
#include <TargetConditionals.h>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"
//! For WindowBackendToString, used to name the offending backend in the
//! "no Metal layer" diagnostic below.
#include "aura/Core/AuraSettings/AuraSettings.h"

namespace aura3d {
namespace mtl {

namespace {

/*
 * Reinterpreting the CAMetalLayer* wma hands back as metal-cpp's CA::MetalLayer*
 * is the intended bridge, not a workaround: CA::MetalLayer has no data members of
 * its own and every method on it is an objc_msgSend to the same object. Apple's
 * own metal-cpp samples cross the boundary exactly this way, which is also why
 * wma can return it as a plain void* without either side needing the other's
 * headers.
 */
[[nodiscard]] CA::MetalLayer* asMetalCppLayer(void* objcLayer) noexcept
{
    return reinterpret_cast<CA::MetalLayer*>(objcLayer);
}

} // namespace

MtlLayerManager::MtlLayerManager(MTL::Device* device, wma::IWindowManager* windowManager, bool vsync)
    : _windowManager(windowManager)
{
    if (!device)
        throw AuraException("MtlLayerManager: device is null");

    if (!_windowManager)
        throw AuraException("MtlLayerManager: window manager is null");

    void* objcLayer = _windowManager->getMetalLayer();
    if (!objcLayer) {
        throw AuraException(
            "MtlLayerManager: the window has no CAMetalLayer; it was not created with "
            "wma::GraphicsAPI::Metal (window backend: "
            + std::string(WindowBackendToString(_windowManager->getBackendType())) + ")");
    }

    _layer = retain(asMetalCppLayer(objcLayer));

    _layer->setDevice(device);
    _layer->setPixelFormat(kColorFormat);

    /*
     * framebufferOnly lets Metal pick the most efficient drawable layout it can,
     * at the cost of the drawable being unreadable as a texture. Nothing in this
     * backend samples or blits from a drawable -- the scene renders into it and it
     * is presented -- so the trade is free here. A future screenshot or
     * post-process path would have to turn this off.
     */
    _layer->setFramebufferOnly(true);

#if TARGET_OS_OSX
    /*
     * iOS has no displaySyncEnabled: presents are always display-synced there, so
     * the setting is macOS-only rather than something to emulate.
     */
    _layer->setDisplaySyncEnabled(vsync);
#else
    (void)vsync;
#endif

    const wma::FramebufferSize size = queryPixelSize();
    _width = static_cast<u32>(size.width);
    _height = static_cast<u32>(size.height);
    _layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(_width),
                                       static_cast<CGFloat>(_height)));

    INK_INFO << "CAMetalLayer configured: " << _width << "x" << _height
             << " px | vsync: " << (vsync ? "on" : "off");
}

MtlLayerManager::~MtlLayerManager()
{
    //! Only our retain is dropped: the layer belongs to the view wma created, and
    //! dies with the window rather than with this object.
    _layer.reset();
    _windowManager = nullptr;

    INK_DEBUG << "MtlLayerManager destroyed";
}

bool MtlLayerManager::resizeToWindow()
{
    const wma::FramebufferSize size = queryPixelSize();
    const u32 width = static_cast<u32>(size.width);
    const u32 height = static_cast<u32>(size.height);

    if (width == _width && height == _height)
        return false;

    _width = width;
    _height = height;

    _layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(_width),
                                       static_cast<CGFloat>(_height)));

    INK_DEBUG << "CAMetalLayer resized: " << _width << "x" << _height << " px";
    return true;
}

wma::FramebufferSize MtlLayerManager::queryPixelSize() const
{
    const wma::FramebufferSize size = _windowManager->getFramebufferSize();

    /*
     * wma already clamps to 1x1, but a zero-sized drawable is rejected outright
     * by CAMetalLayer, so this does not rely on that guarantee holding for every
     * backend it might later gain.
     */
    return wma::FramebufferSize{
        std::max(size.width, 1),
        std::max(size.height, 1),
    };
}

} // namespace mtl
} // namespace aura3d
