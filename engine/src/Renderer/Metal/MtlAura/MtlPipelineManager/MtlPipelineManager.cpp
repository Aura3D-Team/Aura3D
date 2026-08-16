#include "aura/Renderer/Metal/MtlAura/MtlPipelineManager/MtlPipelineManager.h"

#include <string>

#include <ink/ink.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace mtl {

MtlPipelineManager::MtlPipelineManager(MTL::Device* device,
                                      const MtlShaderLibraryManager& library,
                                      const Options& options)
{
    if (!device)
        throw AuraException("MtlPipelineManager: device is null");

    const NS::SharedPtr<MTL::Function> vertexFunction = library.newFunction(options.vertexFunction);
    const NS::SharedPtr<MTL::Function> fragmentFunction = library.newFunction(options.fragmentFunction);

    NS::SharedPtr<MTL::RenderPipelineDescriptor> descriptor =
        adopt(MTL::RenderPipelineDescriptor::alloc()->init());

    const NS::SharedPtr<NS::String> label = makeString(options.label);
    descriptor->setLabel(label.get());
    descriptor->setVertexFunction(vertexFunction.get());
    descriptor->setFragmentFunction(fragmentFunction.get());

    /*
     * Attachment formats, not an attachment set: this is the entire coupling
     * between a Metal pipeline and the pass it will be used in. They must match
     * MtlDrawableManager's render pass exactly or newRenderPipelineState fails.
     */
    MTL::RenderPipelineColorAttachmentDescriptor* colorAttachment =
        descriptor->colorAttachments()->object(0);
    colorAttachment->setPixelFormat(kColorFormat);
    descriptor->setDepthAttachmentPixelFormat(kDepthFormat);

    if (options.alphaBlend) {
        //! Straight (non-premultiplied) alpha, matching the Vulkan and OpenGL
        //! overlay paths: src*srcAlpha + dst*(1-srcAlpha).
        colorAttachment->setBlendingEnabled(true);
        colorAttachment->setRgbBlendOperation(MTL::BlendOperationAdd);
        colorAttachment->setAlphaBlendOperation(MTL::BlendOperationAdd);
        colorAttachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        colorAttachment->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
        colorAttachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        colorAttachment->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    }

    NS::Error* error = nullptr;
    _pipelineState = adopt(device->newRenderPipelineState(descriptor.get(), &error));

    if (!_pipelineState) {
        throw AuraException("MtlPipelineManager: failed to compile pipeline '"
                            + std::string(options.label) + "': " + describeError(error));
    }

    /*
     * The depth-stencil state. A depthTest-off pipeline still gets one (compare
     * Always, no writes) rather than none -- see getDepthStencilState()'s comment
     * for why leaving the encoder's previous state in place is not an option.
     */
    NS::SharedPtr<MTL::DepthStencilDescriptor> depthDescriptor =
        adopt(MTL::DepthStencilDescriptor::alloc()->init());

    depthDescriptor->setLabel(label.get());
    /*
     * CompareFunctionLess against a 1.0 clear, which is Metal's far plane: its
     * depth range is [0,1] like Vulkan's, not [-1,1] like OpenGL's. That is the
     * whole reason MetalRenderer asks Camera for ClipSpace::Vulkan.
     */
    depthDescriptor->setDepthCompareFunction(options.depthTest ? MTL::CompareFunctionLess
                                                              : MTL::CompareFunctionAlways);
    depthDescriptor->setDepthWriteEnabled(options.depthTest);

    _depthStencilState = adopt(device->newDepthStencilState(depthDescriptor.get()));
    if (!_depthStencilState) {
        throw AuraException("MtlPipelineManager: failed to create the depth-stencil state for '"
                            + std::string(options.label) + "'");
    }

    _cullMode = options.cullBackFaces ? MTL::CullModeBack : MTL::CullModeNone;

    INK_DEBUG << "Metal pipeline '" << options.label << "' compiled"
              << " | depth: " << (options.depthTest ? "on" : "off")
              << " | blend: " << (options.alphaBlend ? "on" : "off")
              << " | cull: " << (options.cullBackFaces ? "back" : "none");
}

MtlPipelineManager::~MtlPipelineManager()
{
    _depthStencilState.reset();
    _pipelineState.reset();
}

void MtlPipelineManager::bind(MTL::RenderCommandEncoder* encoder) const noexcept
{
    encoder->setRenderPipelineState(_pipelineState.get());
    encoder->setDepthStencilState(_depthStencilState.get());
    encoder->setCullMode(_cullMode);

    /*
     * Winding is measured in framebuffer space, where Metal's Y axis points down
     * while its clip space's points up -- so a triangle wound clockwise in the
     * projection GLM produced arrives counter-clockwise here.
     *
     * This engine's meshes are authored with front faces clockwise in GLM space
     * (see VkGraphicsPipelineManager's frontFace comment, which reaches the same
     * setting from the opposite direction: Vulkan's clip space is Y-down, and the
     * negative-height viewport it renders with introduces exactly one flip too).
     * Hence CounterClockwise here, and hence a cullBackFaces pipeline culling the
     * same triangles on both backends.
     */
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
}

} // namespace mtl
} // namespace aura3d
