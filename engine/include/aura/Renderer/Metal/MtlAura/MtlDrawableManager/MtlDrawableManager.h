#ifndef MTLDRAWABLEMANAGER_H
#define MTLDRAWABLEMANAGER_H

#pragma once

#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"
#include "aura/Renderer/Metal/MtlAura/MtlLayerManager/MtlLayerManager.h"

namespace aura3d {
namespace mtl {

/**
 * @class MtlDrawableManager
 *
 * @brief Acquires one drawable per frame, owns the depth attachment that pairs
 *        with it, and describes the render pass over the two.
 *
 * Covers what VkImageViewsManager, VkRenderPassManager and
 * VkFrameBuffersManager do between them, which on Metal is far less work: a
 * render pass is a descriptor built at encode time rather than an object
 * pipelines must be compiled against, and there are no framebuffers to
 * pre-create per swapchain image because the drawable's texture is handed
 * straight to the pass.
 *
 * The depth texture, by contrast, is genuinely owned here: CAMetalLayer manages
 * only colour drawables, so depth is this backend's to allocate and to resize.
 *
 * @note Single-sample only. MSAA stays a Vulkan-only setting, as
 *       AuraSettings::getMsaaSamples() documents; adding it here means a
 *       multisample colour texture plus a resolve attachment, not a flag.
 */
class MtlDrawableManager {
public:
    /**
     * @brief Allocates the depth attachment for @p layers' current size.
     *
     * @param[in] device Device the depth texture is allocated on.
     * @param[in] layers Layer whose drawables this manager acquires; must
     *        outlive this object.
     *
     * @throws AuraException if either argument is null, or if the depth texture
     *         cannot be allocated.
     */
    MtlDrawableManager(MTL::Device* device, MtlLayerManager* layers);

    ~MtlDrawableManager();

    MtlDrawableManager(const MtlDrawableManager&) = delete;
    MtlDrawableManager& operator=(const MtlDrawableManager&) = delete;

    /**
     * @brief Takes the next drawable from the layer's pool.
     *
     * Blocks while every drawable is still in flight, which is what paces the
     * CPU to the display -- the same role vkAcquireNextImageKHR plays.
     *
     * @return false when the layer has no drawable to give: the window is
     *         off-screen, minimised, or mid-resize. That is an ordinary
     *         condition rather than an error, and the caller's answer is to skip
     *         the frame -- so this returns a bool instead of throwing.
     */
    [[nodiscard]] bool acquire();

    /**
     * @brief Drops the drawable acquired for this frame.
     *
     * Called once the frame's command buffer has been committed with the
     * drawable scheduled for presentation; the command buffer holds its own
     * reference until then.
     */
    void release() noexcept;

    //! The drawable acquired by the current frame, or nullptr outside a frame.
    [[nodiscard]] CA::MetalDrawable* getDrawable() const noexcept { return _drawable.get(); }

    //! The depth attachment, sized to the layer. Never null after construction.
    [[nodiscard]] MTL::Texture* getDepthTexture() const noexcept { return _depthTexture.get(); }

    /**
     * @brief Configures and returns the render pass over this frame's drawable.
     *
     * The descriptor is owned by this manager and reconfigured in place per
     * frame rather than re-created: the alternative,
     * MTL::RenderPassDescriptor::renderPassDescriptor(), hands back an
     * autoreleased object, so a frame that built one would need a live
     * autorelease pool around every encode.
     *
     * Colour and depth are both cleared on load. Colour is stored (it is what
     * gets presented) while depth is discarded, because nothing reads the depth
     * buffer after the pass and telling Metal so is what lets it keep the
     * attachment off main memory entirely on Apple GPUs.
     *
     * @param[in] clearR Red clear component (0.0f - 1.0f).
     * @param[in] clearG Green clear component.
     * @param[in] clearB Blue clear component.
     * @param[in] clearA Alpha clear component.
     * @return The configured descriptor, or nullptr when no drawable is
     *         currently acquired.
     */
    [[nodiscard]] MTL::RenderPassDescriptor* buildRenderPass(f32 clearR, f32 clearG,
                                                            f32 clearB, f32 clearA);

    /**
     * @brief Re-allocates the depth attachment to match the layer's size.
     *
     * Only does work when the layer's size and the texture's disagree, so it is
     * safe to call on every resize notification, however noisy.
     */
    void handleResize();

private:
    /**
     * @brief Allocates the depth attachment at @p width x @p height pixels.
     * @throws AuraException if the device refuses the allocation.
     */
    void createDepthTexture(u32 width, u32 height);

    MTL::Device* _device = nullptr;     //! Borrowed; owned by MtlDeviceManager.
    MtlLayerManager* _layers = nullptr; //! Borrowed; owned by MetalRenderer.

    NS::SharedPtr<MTL::Texture> _depthTexture;
    NS::SharedPtr<MTL::RenderPassDescriptor> _renderPass;

    //! Retained for the frame's duration: nextDrawable() returns an autoreleased
    //! object, which would otherwise die at the frame's pool drain while the
    //! command buffer still needs it.
    NS::SharedPtr<CA::MetalDrawable> _drawable;

    u32 _depthWidth = 0;
    u32 _depthHeight = 0;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLDRAWABLEMANAGER_H
