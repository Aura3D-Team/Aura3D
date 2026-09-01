#ifndef MTLLAYERMANAGER_H
#define MTLLAYERMANAGER_H

#pragma once

#include <wma/wma.hpp>

#include "aura/aura.h"
#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"

namespace aura3d {
namespace mtl {

/**
 * @class MtlLayerManager
 *
 * @brief Configures the CAMetalLayer wma attached to the window, and keeps its
 *        drawable size in step with that window.
 *
 * The Metal counterpart of VkSurfaceManager plus the sizing half of
 * VkSwapChainManager: a CAMetalLayer *is* the swapchain -- it owns the drawable
 * pool, the present queue and the surface format -- so there is no separate
 * object to create, no format/present-mode negotiation and no
 * VK_ERROR_OUT_OF_DATE_KHR to recover from. Resizing is one property write.
 *
 * Notably this class creates nothing. wma owns the platform side entirely: a
 * window opened with wma::GraphicsAPI::Metal comes with a layer already hosted
 * in the right kind of view (NSView on macOS, UIView on iOS), reachable through
 * IWindowManager::getMetalLayer(). What is left for the renderer is the half wma
 * deliberately does not choose -- the device, the pixel format, the drawable
 * size -- which is exactly what this class sets.
 *
 * That split is why the whole Metal backend is plain C++ with no Objective-C of
 * its own: the AppKit work lives in libwma, beside the other native window code
 * (compare VkSurfaceManager, which likewise leaves the window to wma and only
 * builds the surface).
 */
class MtlLayerManager {
public:
    /**
     * @brief Binds @p device and this backend's formats to @p windowManager's layer.
     *
     * @param[in] device Device the layer allocates its drawables on.
     * @param[in] windowManager Window whose layer is configured; must outlive
     *        this object, and must have been created with
     *        wma::GraphicsAPI::Metal.
     * @param[in] vsync Whether presents should be throttled to the display
     *        refresh. macOS honours this through CAMetalLayer's
     *        displaySyncEnabled; iOS always syncs and ignores it.
     *
     * @throws AuraException if either argument is null, or if the window has no
     *         Metal layer -- which means it was opened for a different graphics
     *         API, since wma fails Metal window creation outright rather than
     *         handing back a layerless window.
     */
    MtlLayerManager(MTL::Device* device, wma::IWindowManager* windowManager, bool vsync);

    ~MtlLayerManager();

    MtlLayerManager(const MtlLayerManager&) = delete;
    MtlLayerManager& operator=(const MtlLayerManager&) = delete;

    /**
     * @brief The layer frames are presented through. Never null after construction.
     */
    [[nodiscard]] CA::MetalLayer* getLayer() const noexcept { return _layer.get(); }

    /**
     * @brief Re-reads the window's backing-store size and resizes the drawables
     *        to match.
     *
     * Call on every resize and on every DPI change. A no-op when the size has
     * not actually changed, because assigning drawableSize discards the drawable
     * pool even when the new value is identical -- which would cost a
     * reallocation per frame if this were called unconditionally from the render
     * loop.
     *
     * @return true if the size changed, so the caller knows to rebuild anything
     *         sized against it (the depth texture, in MtlDrawableManager).
     */
    bool resizeToWindow();

    //! Drawable width in pixels (not points): what the depth attachment and the
    //! overlay's orthographic projection must both be sized from.
    [[nodiscard]] u32 getWidth() const noexcept { return _width; }

    //! Drawable height in pixels. See getWidth().
    [[nodiscard]] u32 getHeight() const noexcept { return _height; }

private:
    /**
     * @brief The window's backing-store size in pixels, clamped to at least 1x1.
     *
     * Comes from wma::IWindowManager::getFramebufferSize() rather than from
     * wma::WindowDetails, whose width/height are the logical size the window was
     * requested at: on a Retina display the backing store is 2x that, and a
     * drawable sized from the logical value renders a quarter of the window and
     * stretches it.
     */
    [[nodiscard]] wma::FramebufferSize queryPixelSize() const;

    //! Retained, not adopted: the layer belongs to the view wma created, and
    //! outliving this object is normal (the window does too). Retaining is what
    //! keeps it valid for this object's lifetime regardless.
    NS::SharedPtr<CA::MetalLayer> _layer;

    wma::IWindowManager* _windowManager = nullptr; //! Borrowed; owned by MetalRenderer.

    u32 _width = 1;
    u32 _height = 1;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLLAYERMANAGER_H
