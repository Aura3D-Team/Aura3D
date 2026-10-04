#include "aura/Renderer/Metal/MetalRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/AuraMath.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/aura.h"

/*
 * This backend cannot work against a libwma without Metal support: no window
 * backend would honour wma::GraphicsAPI::Metal, so createWindow() would throw on
 * every platform. Caught at compile time rather than as a runtime
 * GraphicsException on someone's Mac.
 *
 * Spelled as #if/#error rather than static_assert deliberately: an *older* wma
 * does not define WMA_HAS_METAL at all, and an undefined identifier inside a
 * static_assert is a hard error naming the macro rather than the problem.
 */
#if !defined(WMA_HAS_METAL) || !WMA_HAS_METAL
#error "AURA_ENABLE_METAL requires a libwma built with Metal support: an Apple \
target with the SDL3 or GLFW backend enabled. See WMA_HAS_METAL in \
wma::wma's compile definitions."
#endif

namespace aura3d
{
namespace mtl
{

MetalRenderer::MetalRenderer(const wma::WindowDetails &windowDetails) : IRenderer(windowDetails)
{
    INK_INFO << "Renderer - METAL";
}

MetalRenderer::~MetalRenderer()
{
    cleanup();
}

void MetalRenderer::initialize(AuraSettings *settings, const JobSystem *jobs)
{
    if (_isInitialized)
        return;

    //! Unused here: Metal's own frame-pacing semaphore (_frameSlots) already
    //! covers this backend's CPU/GPU overlap; nothing on its draw path wants
    //! a generic band dispatch.
    (void)jobs;

    if (!settings)
        throw AuraException("MetalRenderer: settings are null");

    createWindow(settings->getWindowTitle().c_str(), settings->getWindowBackend());

    _frameSlots = std::make_shared<FrameSlots>(MTL_MAX_FRAMES_IN_FLIGHT);
    _deviceManager = std::make_unique<MtlDeviceManager>();

    _layerManager =
        std::make_unique<MtlLayerManager>(_deviceManager->getDevice(), _windowManagerApi.get(), settings->getVSync());

    _drawableManager = std::make_unique<MtlDrawableManager>(_deviceManager->getDevice(), _layerManager.get());

    _shaderLibrary = std::make_unique<MtlShaderLibraryManager>(_deviceManager->getDevice());

    _bufferManager = std::make_unique<MtlBufferManager>(_deviceManager->getDevice(), _deviceManager->hasUnifiedMemory(),
                                                        _deviceManager->maxBufferLength());

    _vertexManager = std::make_unique<MtlVertexBufferManager>(_bufferManager.get());
    _indexManager = std::make_unique<MtlIndexBufferManager>(_bufferManager.get());
    _textureManager = std::make_unique<MtlTextureManager>(_deviceManager->getDevice());

    createPipelines();

    /*
     * Created up front rather than lazily on the first untextured draw: it is 4
     * bytes, and doing it here keeps texture creation out of the encode path
     * entirely.
     */
    _fallbackTexture = createSolidColorTexture(255, 255, 255, 255);
    if (!isValidHandle(_fallbackTexture))
        throw AuraException("MetalRenderer: failed to create the fallback white texture");

    _isInitialized = true;
    INK_INFO << "Metal renderer initialized (" << _layerManager->getWidth() << "x" << _layerManager->getHeight()
             << " px, " << MTL_MAX_FRAMES_IN_FLIGHT << " frames in flight)";
}

void MetalRenderer::createWindow(const char *title, const wma::WindowBackend &wBackend)
{
    /*
     * wma opens the window with a CAMetalLayer already hosted in the right kind
     * of view and refuses the request outright on a backend that has no route to
     * one -- so an unsupported window backend fails here, with wma's own
     * diagnostic, rather than surviving to confuse the renderer later.
     */
    _windowManagerApi = makeWindow(wBackend, _windowDetails, wma::GraphicsAPI::Metal);
    _windowManagerApi->createWindow(title);
}

void MetalRenderer::createPipelines()
{
    MtlPipelineManager::Options sceneOptions;
    sceneOptions.vertexFunction = kVertexFunction3D;
    sceneOptions.fragmentFunction = kFragmentFunction3D;
    sceneOptions.depthTest = true;
    sceneOptions.alphaBlend = false;
    sceneOptions.cullBackFaces = true;
    sceneOptions.label = "Aura3D scene 3D";
    _scenePipeline = std::make_unique<MtlPipelineManager>(_deviceManager->getDevice(), *_shaderLibrary, sceneOptions);

    MtlPipelineManager::Options batchOptions;
    batchOptions.vertexFunction = kVertexFunctionBatch;
    batchOptions.fragmentFunction = kFragmentFunctionBatch;
    batchOptions.depthTest = true;
    batchOptions.depthWrite = false;
    batchOptions.depthCompare = MTL::CompareFunctionLessEqual;
    batchOptions.alphaBlend = true;
    batchOptions.label = "Aura3D batch";
    _batchPipeline = std::make_unique<MtlPipelineManager>(_deviceManager->getDevice(), *_shaderLibrary, batchOptions);
}

void MetalRenderer::handleWindowChanges()
{
    if (!_layerManager || !_drawableManager)
        return;

    //! The depth attachment only needs rebuilding when the size actually moved,
    //! which is precisely what resizeToWindow() reports.
    if (_layerManager->resizeToWindow())
        _drawableManager->handleResize();
}

void MetalRenderer::cleanup()
{
    if (!_isInitialized && !_windowManagerApi)
        return;

    /*
     * cleanup() can run mid-frame: the ESC key action above calls it from inside
     * the window manager's event pump, which happens between beginFrame() and
     * endFrame(). This frame therefore has to be abandoned before anything else,
     * both to give its encoder an end and to hand back the frame slot it holds --
     * without which the drain below would wait on a permit only this thread could
     * release.
     */
    if (_frameBegun)
    {
        if (_renderPassActive)
        {
            _encoder->endEncoding();
            _encoder = nullptr;
            _renderPassActive = false;
        }

        //! Dropped without commit: an uncommitted command buffer is simply
        //! released, and nothing this frame recorded needs to reach the screen.
        _commandBuffer = nullptr;
        _drawableManager->release();
        _framePool.reset();

        if (_frameSlots)
            _frameSlots->release();

        _frameBegun = false;
    }

    /*
     * Wait for every committed frame to complete before tearing the device down.
     * Taking every permit can only succeed once each in-flight frame's completion
     * handler has run, which makes this the Metal equivalent of vkDeviceWaitIdle.
     */
    if (_frameSlots)
    {
        for (u32 slot = 0; slot < MTL_MAX_FRAMES_IN_FLIGHT; ++slot)
            _frameSlots->acquire();

        for (u32 slot = 0; slot < MTL_MAX_FRAMES_IN_FLIGHT; ++slot)
            _frameSlots->release();
    }

    clearSharedResources();

    for (u32 slot = 0; slot < MTL_MAX_FRAMES_IN_FLIGHT; ++slot)
    {
        _batchVertexBuffers[slot].reset();
        _batchIndexBuffers[slot].reset();
        _batchVertexCapacity[slot] = 0;
        _batchIndexCapacity[slot] = 0;
        _batchVertexUsed[slot] = 0;
        _batchIndexUsed[slot] = 0;
    }

    //! The texture pool dies with _textureManager, so drop the cached handle.
    _fallbackTexture = {};
    _currentVertexBuffer = {};
    _currentIndexBuffer = {};
    _currentTexture = {};

    /*
     * Reverse construction order: the pipelines and pools reference the device,
     * the drawable manager references the layer, and the layer references the
     * window that wma owns.
     */
    _batchPipeline.reset();
    _scenePipeline.reset();
    _textureManager.reset();
    _indexManager.reset();
    _vertexManager.reset();
    _bufferManager.reset();
    _shaderLibrary.reset();
    _drawableManager.reset();
    _layerManager.reset();
    _deviceManager.reset();
    _frameSlots.reset();
    _windowManagerApi.reset();

    _currentFrame = 0;
    _isInitialized = false;
}

VertexBufferHandle MetalRenderer::createVertexBuffer(std::vector<gfx::Vertex3D> &&vertices)
{
    if (!_vertexManager)
        return {};

    /*
     * The rvalue reference cannot be honoured, and could not be by any Metal
     * backend: newBuffer() copies into device-visible memory it allocated
     * itself, so there is no way to adopt the vector's storage. The parameter
     * stays an rvalue reference because IRenderer's interface is shared with
     * backends that *can* take ownership (the software rasteriser keeps the
     * vector verbatim).
     */
    const std::vector<gfx::Vertex3D> owned = std::move(vertices);
    return _vertexManager->create(owned);
}

IndexBufferHandle MetalRenderer::createIndexBuffer(std::vector<u16> &&indices)
{
    if (!_indexManager)
        return {};

    const std::vector<u16> owned = std::move(indices);
    return _indexManager->create(owned);
}

IndexBufferHandle MetalRenderer::createIndexBuffer(std::vector<u32> &&indices)
{
    if (!_indexManager)
        return {};

    const std::vector<u32> owned = std::move(indices);
    return _indexManager->create(owned);
}

TextureHandle MetalRenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    if (!_textureManager)
        return {};

    const std::array<u8, 4> texel = {r, g, b, a};
    return _textureManager->createFromPixels(texel.data(), 1, 1);
}

TextureHandle MetalRenderer::createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height)
{
    if (!_textureManager)
        return {};

    return _textureManager->createFromPixels(rgbaPixels, width, height);
}

TextureHandle MetalRenderer::createDynamicTexture(u32 width, u32 height)
{
    if (!_textureManager)
        return {};

    return _textureManager->createDynamic(width, height);
}

TextureHandle MetalRenderer::createCoverageTexture(u32 width, u32 height)
{
    return _textureManager ? _textureManager->createCoverage(width, height) : TextureHandle{};
}

void MetalRenderer::updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                                const u8 *coverage)
{
    if (_textureManager)
        (void)_textureManager->updateCoverageRegion(handle, x, y, width, height, coverage);
}

void MetalRenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels)
{
    if (!_textureManager)
        return;

    //! The manager logs whichever precondition failed; there is nothing further
    //! for the renderer to say about it.
    (void)_textureManager->updateRegion(handle, x, y, width, height, rgbaPixels);
}

void MetalRenderer::beginFrame()
{
    if (!_isInitialized)
        return;

    _frameBegun = false;

    wma::WindowFlags *flags = _windowManagerApi->getWindowFlags();
    if (flags->resized)
    {
        flags->resized = false;
        handleWindowChanges();
    }

    //! Nothing to draw into while the platform has no surface attached (an
    //! iOS app in the background). Cheap enough to poll every frame.
    if (!_windowManagerApi->isSurfaceAvailable())
        return;

    /*
     * Take a frame slot before touching anything per-frame: this is what bounds
     * the CPU to MTL_MAX_FRAMES_IN_FLIGHT frames ahead of the GPU and what makes
     * the batch buffers at index _currentFrame safe to overwrite. This is
     * this backend's counterpart to Vulkan's fence wait -- a semaphore-style
     * throttle rather than a fence, but the same "block until the GPU has
     * caught up enough" cost, and scoped the same way so it is not silently
     * folded into `unscoped`.
     */
    {
        AURA_FRAME_SCOPE(FramePhase::WaitFence);
        _frameSlots->acquire();
    }

    //! Past the throttle, so this slot's batch buffers are the GPU's no
    //! longer and the frame's batches can start appending from the top again.
    _batchVertexUsed[_currentFrame] = 0;
    _batchIndexUsed[_currentFrame] = 0;

    _framePool.emplace();

    {
        AURA_FRAME_SCOPE(FramePhase::Acquire);

        if (!_drawableManager->acquire())
        {
            //! No drawable: an ordinary condition (occluded/minimised window), so
            //! the frame is skipped and the slot handed straight back.
            _framePool.reset();
            _frameSlots->release();
            return;
        }
    }

    _commandBuffer = _deviceManager->getCommandQueue()->commandBuffer();
    if (!_commandBuffer)
    {
        INK_WARN << "MetalRenderer: the command queue produced no command buffer; skipping frame";
        _drawableManager->release();
        _framePool.reset();
        _frameSlots->release();
        return;
    }

    _frameBegun = true;
}

void MetalRenderer::beginRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    //! cleanup() can run mid-frame (the ESC key action calls it), which drops the
    //! managers while the application's frame callback is still executing.
    if (!_frameBegun || _renderPassActive || !_drawableManager)
        return;

    MTL::RenderPassDescriptor *pass = _drawableManager->buildRenderPass(_clearR, _clearG, _clearB, _clearA);

    if (!pass)
        return;

    _encoder = _commandBuffer->renderCommandEncoder(pass);
    if (!_encoder)
    {
        INK_WARN << "MetalRenderer: failed to open a render command encoder";
        return;
    }

    /*
     * The scene pipeline is bound up front so an application that draws 3D
     * without an intervening batch needs no bind of its own. drawBatch() and
     * bindDrawState() switch pipelines only when the draw kind changes, so a
     * run of batches binds once.
     *
     * No setViewport() call: Metal's default viewport is the whole attachment
     * with a [0,1] depth range, which is exactly what is wanted. Vulkan needs an
     * explicit one only because it flips Y through a negative height there.
     */
    _scenePipeline->bind(_encoder);
    _batchPipelineBound = false;
    _renderPassActive = true;
}

void MetalRenderer::endRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (!_renderPassActive)
        return;

    _encoder->endEncoding();
    _encoder = nullptr;
    _renderPassActive = false;
}

void MetalRenderer::endFrame()
{
    //! Before any scope opens, matching beginFrame()/beginRenderPass()'s own
    //! early return on the same flag: a frame that never began records no
    //! samples for any phase, rather than a Present entry with nothing behind
    //! it. AURA_FRAME_END() below is reached only past this point, so a
    //! skipped frame closes nothing and biases no average.
    if (!_frameBegun)
        return;

    //! Tolerate a caller that never closed its pass: an encoder still open when
    //! the command buffer is committed is a hard Metal error, not a warning.
    //! endRenderPass() opens its own EndPass scope, so this is not double-timed
    //! by wrapping it in one here too.
    if (_renderPassActive)
        endRenderPass();

    {
        AURA_FRAME_SCOPE(FramePhase::Present);

        _commandBuffer->presentDrawable(_drawableManager->getDrawable());

        /*
         * The completion handler captures the semaphore itself, by shared_ptr,
         * rather than `this`. It runs on a Metal-internal thread at an
         * unspecified time, so a captured `this` would be a use-after-free the
         * moment a frame outlived the renderer -- exactly what switchBackend()
         * does.
         */
        const std::shared_ptr<FrameSlots> slots = _frameSlots;
        _commandBuffer->addCompletedHandler(
            [slots](MTL::CommandBuffer *)
            {
                slots->release();
            });

        _commandBuffer->commit();
    }

    /*
     * Metal keeps its own reference to the drawable and to every resource the
     * command buffer touched, so releasing them here cannot pull anything out
     * from under the GPU.
     */
    _drawableManager->release();
    _commandBuffer = nullptr;
    _framePool.reset();

    _currentFrame = (_currentFrame + 1) % MTL_MAX_FRAMES_IN_FLIGHT;
    _frameBegun = false;

    //! Outside the Present scope so it is closed and counted before the frame
    //! is.
    AURA_FRAME_END();
}

void MetalRenderer::setTransform(const gfx::TransformUBO &ubo)
{
    //! Recorded rather than pushed: the bytes travel to the GPU at each draw,
    //! from bindDrawState(). IRenderer::drawMeshes() also reads this back to vary
    //! only the model matrix per item.
    _currentTransform = ubo;
}

void MetalRenderer::bindVertexBuffer(VertexBufferHandle handle)
{
    _currentVertexBuffer = handle;
}

void MetalRenderer::bindIndexBuffer(IndexBufferHandle handle)
{
    _currentIndexBuffer = handle;
}

void MetalRenderer::bindTexture(TextureHandle handle)
{
    _currentTexture = handle;
}

MTL::Texture *MetalRenderer::resolveSampledTexture(TextureHandle handle)
{
    if (MTL::Texture *texture = _textureManager->resolve(handle))
        return texture;

    return _textureManager->resolve(_fallbackTexture);
}

void MetalRenderer::bindDrawState()
{
    if (_batchPipelineBound)
    {
        _scenePipeline->bind(_encoder);
        _batchPipelineBound = false;
    }

    TransformUniforms transform;
    transform.model = _currentTransform.model;
    transform.view = _currentTransform.view;
    transform.proj = _currentTransform.proj;
    transform.normalMatrix = glm::mat4(gfx::normalMatrixOf(_currentTransform.model));

    /*
     * setVertexBytes/setFragmentBytes rather than a uniform buffer: Metal copies
     * these into the command buffer, so successive draws in a frame each get
     * their own snapshot with no double-buffering or per-frame descriptor on this
     * side. 256 + 48 bytes is well inside the 4 KB the API allows.
     */
    _encoder->setVertexBytes(&transform, sizeof(transform), kVertexUniformSlot);
    _encoder->setFragmentBytes(&_light, sizeof(_light), kFragmentLightSlot);

    _encoder->setFragmentTexture(resolveSampledTexture(_currentTexture), kFragmentAlbedoSlot);
    _encoder->setFragmentSamplerState(_textureManager->getSampler(), kFragmentAlbedoSlot);

    if (MTL::Buffer *vertexBuffer = _vertexManager->resolve(_currentVertexBuffer))
        _encoder->setVertexBuffer(vertexBuffer, 0, kVertexGeometrySlot);
}

void MetalRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (!_renderPassActive || instanceCount == 0)
        return;

    const IndexBufferRecord *record = _indexManager->resolve(_currentIndexBuffer);
    if (!record || !record->buffer)
        return;

    //! Nothing to read positions from; the same "skip this draw" case the other
    //! backends' bind caches produce.
    if (!_vertexManager->resolve(_currentVertexBuffer))
        return;

    /*
     * Clamped to what the buffer actually holds. Metal reads exactly the number
     * of indices it is given, and running off the end of the buffer is undefined
     * behaviour rather than a validated error, so an over-large count from a
     * caller is trimmed here instead of being handed to the GPU.
     */
    const u32 drawnIndices = std::min(indexCount, record->indexCount);
    if (drawnIndices == 0)
        return;

    bindDrawState();

    _encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(drawnIndices),
                                    record->indexType, record->buffer, 0, static_cast<NS::UInteger>(instanceCount));
}

void MetalRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (!_renderPassActive || vertexCount == 0 || instanceCount == 0)
        return;

    if (!_vertexManager->resolve(_currentVertexBuffer))
        return;

    bindDrawState();

    _encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(0),
                             static_cast<NS::UInteger>(vertexCount), static_cast<NS::UInteger>(instanceCount));
}

bool MetalRenderer::ensureBatchCapacity(size_t vertexBytes, size_t indexBytes)
{
    const u32 frame = _currentFrame;

    /*
     * Doubling, so a frame of many small batches reallocates a handful of times
     * rather than once per batch. A replaced buffer stays alive while earlier
     * batches still use it: the command buffer retains everything it binds.
     */
    const auto grow =
        [&](NS::SharedPtr<MTL::Buffer> &buffer, size_t &capacity, size_t needed, size_t minimum, const char *label)
    {
        if (needed <= capacity)
            return true;
        const size_t grown = std::max({needed, minimum, capacity * 2});
        NS::SharedPtr<MTL::Buffer> replacement = _bufferManager->createDynamic(grown, label);
        if (!replacement)
            return false;
        buffer = std::move(replacement);
        capacity = grown;
        return true;
    };
    return grow(_batchVertexBuffers[frame], _batchVertexCapacity[frame], vertexBytes, 65536, "Aura3D batch vertices") &&
           grow(_batchIndexBuffers[frame], _batchIndexCapacity[frame], indexBytes, 16384, "Aura3D batch indices");
}

glm::uvec2 MetalRenderer::renderTargetSize() const noexcept
{
    /*
     * The drawable's size rather than wma's logical window size: those differ by
     * the backing scale factor on a Retina display, and it is the drawable that a
     * batch has to cover (the same reason the Vulkan path uses its swapchain
     * extent).
     */
    return _layerManager ? glm::uvec2{_layerManager->getWidth(), _layerManager->getHeight()} : glm::uvec2{0};
}

void MetalRenderer::drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                              TextureHandle texture, gfx::BatchSpace space)
{
    AURA_FRAME_SCOPE(space == gfx::BatchSpace::Screen ? FramePhase::RecordOverlay : FramePhase::RecordScene);

    if (!_renderPassActive || !_batchPipeline || vertices.empty() || indices.empty())
        return;

    // Every encoded draw retains its own slice until this frame slot completes.
    const size_t vertexOffset = _batchVertexUsed[_currentFrame];
    const size_t indexOffset = _batchIndexUsed[_currentFrame];
    if (!ensureBatchCapacity(vertexOffset + vertices.size_bytes(), indexOffset + indices.size_bytes()))
        return;

    MTL::Buffer *vertexBuffer = _batchVertexBuffers[_currentFrame].get();
    MTL::Buffer *indexBuffer = _batchIndexBuffers[_currentFrame].get();
    std::memcpy(static_cast<u8 *>(vertexBuffer->contents()) + vertexOffset, vertices.data(), vertices.size_bytes());
    std::memcpy(static_cast<u8 *>(indexBuffer->contents()) + indexOffset, indices.data(), indices.size_bytes());
    _batchVertexUsed[_currentFrame] = vertexOffset + vertices.size_bytes();
    _batchIndexUsed[_currentFrame] = indexOffset + indices.size_bytes();

    const BatchUniforms batch{batchTransform(space)};
    if (!_batchPipelineBound)
    {
        _batchPipeline->bind(_encoder);
        _batchPipelineBound = true;
    }
    _encoder->setVertexBuffer(vertexBuffer, vertexOffset, kVertexGeometrySlot);
    _encoder->setVertexBytes(&batch, sizeof(batch), kVertexUniformSlot);
    _encoder->setFragmentTexture(resolveSampledTexture(texture), kFragmentAlbedoSlot);
    _encoder->setFragmentSamplerState(_textureManager->getSampler(), kFragmentAlbedoSlot);
    _encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indices.size()),
                                    MTL::IndexTypeUInt32, indexBuffer, static_cast<NS::UInteger>(indexOffset));
}

void MetalRenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    _clearR = r;
    _clearG = g;
    _clearB = b;
    _clearA = a;
}

} // namespace mtl
} // namespace aura3d
