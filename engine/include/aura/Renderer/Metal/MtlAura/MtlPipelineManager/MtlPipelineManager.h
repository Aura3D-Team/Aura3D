#ifndef MTLPIPELINEMANAGER_H
#define MTLPIPELINEMANAGER_H

#pragma once

#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"
#include "aura/Renderer/Metal/MtlAura/MtlShaderLibraryManager/MtlShaderLibraryManager.h"

namespace aura3d {
namespace mtl {

/**
 * @class MtlPipelineManager
 *
 * @brief One compiled render pipeline: its state object, its depth-stencil
 *        state and the rasterizer settings an encoder must apply alongside them.
 *
 * The Metal counterpart of VkGraphicsPipelineManager, and used the same way --
 * one instance per pipeline, so MetalRenderer holds two: the lit 3D scene
 * pipeline and the unlit 2D overlay.
 *
 * Two structural differences from the Vulkan original are worth knowing:
 *
 *  - There is no vertex-input state to declare. The shaders read geometry from
 *    a device buffer indexed by [[vertex_id]], so the vertex layout lives in
 *    the MSL structs alone rather than being restated as an
 *    MTLVertexDescriptor here (see mtl_shader3d.metal's header comment).
 *
 *  - A pipeline is not built against a render pass object, only against
 *    attachment *formats*. That is why nothing here has to be rebuilt when the
 *    window resizes -- unlike the Vulkan path, where a swapchain rebuild takes
 *    the render pass and every pipeline with it.
 *
 * Cull mode and winding are not baked into the pipeline state on Metal; they are
 * encoder state. They are still decided here, so all of a pipeline's
 * fixed-function choices stay in one place, and applied together by bind().
 */
class MtlPipelineManager {
public:
    /**
     * @struct Options
     * @brief The fixed-function choices that distinguish one pipeline from another.
     *
     * Mirrors aura3d::vk::PipelineOptions field for field (minus its sample
     * count, since this backend is single-sample) so the two backends' pipelines
     * can be compared side by side.
     */
    struct Options {
        const char* vertexFunction = kVertexFunction3D;   //! MSL vertex entry point.
        const char* fragmentFunction = kFragmentFunction3D; //! MSL fragment entry point.
        bool depthTest = false;     //! Enables both the depth test and depth writes.
        bool alphaBlend = false;    //! src*srcAlpha + dst*(1-srcAlpha).
        bool cullBackFaces = false; //! Discards back faces; see bind().
        const char* label = "Aura3D pipeline"; //! Shown in Xcode's GPU debugger.
    };

    /**
     * @brief Compiles the pipeline described by @p options.
     *
     * @param[in] device Device the pipeline is compiled for.
     * @param[in] library Source of the two shader functions; not retained.
     * @param[in] options Which shaders, and which fixed-function state.
     *
     * @throws AuraException if @p device is null, if either shader function is
     *         missing, or if the pipeline is rejected -- the compiler's
     *         diagnostic is carried in the message.
     */
    MtlPipelineManager(MTL::Device* device,
                       const MtlShaderLibraryManager& library,
                       const Options& options);

    ~MtlPipelineManager();

    MtlPipelineManager(const MtlPipelineManager&) = delete;
    MtlPipelineManager& operator=(const MtlPipelineManager&) = delete;

    //! The compiled pipeline state. Never null after construction.
    [[nodiscard]] MTL::RenderPipelineState* getPipelineState() const noexcept
    {
        return _pipelineState.get();
    }

    /**
     * @brief The depth-stencil state pairing with this pipeline.
     *
     * Always present, including for a pipeline with depthTest off -- in that
     * case it compares Always and writes nothing, which is how the overlay
     * composites over a scene without disturbing the depth buffer. Binding a
     * state explicitly (rather than binding none) matters because depth-stencil
     * state is encoder state that persists from whatever was drawn before.
     */
    [[nodiscard]] MTL::DepthStencilState* getDepthStencilState() const noexcept
    {
        return _depthStencilState.get();
    }

    /**
     * @brief Applies this pipeline's pipeline state, depth-stencil state, cull
     *        mode and winding to @p encoder.
     *
     * One call rather than four at each bind site, so a pipeline can never be
     * bound with another pipeline's leftover cull mode -- the failure that looks
     * like randomly disappearing geometry rather than an error.
     *
     * @param[in,out] encoder Encoder to configure; must be non-null.
     */
    void bind(MTL::RenderCommandEncoder* encoder) const noexcept;

private:
    NS::SharedPtr<MTL::RenderPipelineState> _pipelineState;
    NS::SharedPtr<MTL::DepthStencilState> _depthStencilState;

    MTL::CullMode _cullMode = MTL::CullModeNone;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLPIPELINEMANAGER_H
