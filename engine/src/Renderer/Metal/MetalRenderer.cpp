#include "aura/Renderer/Metal/MetalRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <glm/ext/matrix_clip_space.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/AuraMath.h"
#include "aura/Core/Profiling/FrameProfiler.h"

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
#  error "AURA_ENABLE_METAL requires a libwma built with Metal support: an Apple \
target with the SDL3 or GLFW backend enabled. See WMA_HAS_METAL in \
wma/core/BuildConfig.hpp."
#endif

namespace aura3d {
namespace mtl {

MetalRenderer::MetalRenderer(const wma::WindowDetails& windowDetails)
    : IRenderer(windowDetails)
{
    INK_INFO << "Renderer - METAL";
}

MetalRenderer::~MetalRenderer()
{
    cleanup();
}

void MetalRenderer::initialize(AuraSettings* settings, const JobSystem* jobs)
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
    setupInput();

    _frameSlots = std::make_shared<FrameSlots>(MTL_MAX_FRAMES_IN_FLIGHT);
    _deviceManager = std::make_unique<MtlDeviceManager>();

    _layerManager = std::make_unique<MtlLayerManager>(
        _deviceManager->getDevice(), _windowManagerApi.get(), settings->getVSync());

    _drawableManager = std::make_unique<MtlDrawableManager>(
        _deviceManager->getDevice(), _layerManager.get());

    _shaderLibrary = std::make_unique<MtlShaderLibraryManager>(_deviceManager->getDevice());

    _bufferManager = std::make_unique<MtlBufferManager>(
        _deviceManager->getDevice(),
        _deviceManager->hasUnifiedMemory(),
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
    INK_INFO << "Metal renderer initialized ("
             << _layerManager->getWidth() << "x" << _layerManager->getHeight() << " px, "
             << MTL_MAX_FRAMES_IN_FLIGHT << " frames in flight)";
}

void MetalRenderer::createWindow(const char* title, const wma::WindowBackend& wBackend)
{
    /*
     * wma opens the window with a CAMetalLayer already hosted in the right kind
     * of view and refuses the request outright on a backend that has no route to
     * one -- so an unsupported window backend fails here, with wma's own
     * diagnostic, rather than surviving to confuse the renderer later.
     */
    _windowManagerApi = wma::createWindowManager(wBackend, _windowDetails, wma::GraphicsAPI::Metal);
    _windowManagerApi->createWindow(title);
}

void MetalRenderer::setupInput()
{
    _windowManagerApi->getKeyboardListener().addKeyAction(
        wma::Key::KEY_ESCAPE, wma::KeyAction{[this]() { cleanup(); }, nullptr});
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
    _scenePipeline = std::make_unique<MtlPipelineManager>(
        _deviceManager->getDevice(), *_shaderLibrary, sceneOptions);

    MtlPipelineManager::Options overlayOptions;
    overlayOptions.vertexFunction = kVertexFunction2D;
    overlayOptions.fragmentFunction = kFragmentFunction2D;
    //! No depth test and no writes: the overlay composites over the finished
    //! scene and must never be occluded by it.
    overlayOptions.depthTest = false;
    overlayOptions.alphaBlend = true;
    //! Screen-space quads have no meaningful facing, so culling them would drop
    //! whichever winding the batch happened to emit.
    overlayOptions.cullBackFaces = false;
    overlayOptions.label = "Aura3D overlay 2D";
    _overlayPipeline = std::make_unique<MtlPipelineManager>(
        _deviceManager->getDevice(), *_shaderLibrary, overlayOptions);
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
    if (_frameBegun) {
        if (_renderPassActive) {
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
    if (_frameSlots) {
        for (u32 slot = 0; slot < MTL_MAX_FRAMES_IN_FLIGHT; ++slot)
            _frameSlots->acquire();

        for (u32 slot = 0; slot < MTL_MAX_FRAMES_IN_FLIGHT; ++slot)
            _frameSlots->release();
    }

    clearSharedResources();

    for (u32 slot = 0; slot < MTL_MAX_FRAMES_IN_FLIGHT; ++slot) {
        _overlayVertexBuffers[slot].reset();
        _overlayIndexBuffers[slot].reset();
        _overlayVertexCapacity[slot] = 0;
        _overlayIndexCapacity[slot] = 0;
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
    _overlayPipeline.reset();
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

VertexBufferHandle MetalRenderer::createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices)
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

IndexBufferHandle MetalRenderer::createIndexBuffer(std::vector<u16>&& indices)
{
    if (!_indexManager)
        return {};

    const std::vector<u16> owned = std::move(indices);
    return _indexManager->create(owned);
}

IndexBufferHandle MetalRenderer::createIndexBuffer(std::vector<u32>&& indices)
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

TextureHandle MetalRenderer::createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height)
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

void MetalRenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y,
                                       u32 width, u32 height, const u8* rgbaPixels)
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

    wma::WindowFlags* flags = _windowManagerApi->getWindowFlags();
    if (flags->resized) {
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
     * the overlay buffers at index _currentFrame safe to overwrite. This is
     * this backend's counterpart to Vulkan's fence wait -- a semaphore-style
     * throttle rather than a fence, but the same "block until the GPU has
     * caught up enough" cost, and scoped the same way so it is not silently
     * folded into `unscoped`.
     */
    {
        AURA_FRAME_SCOPE(FramePhase::WaitFence);
        _frameSlots->acquire();
    }

    _framePool.emplace();

    {
        AURA_FRAME_SCOPE(FramePhase::Acquire);

        if (!_drawableManager->acquire()) {
            //! No drawable: an ordinary condition (occluded/minimised window), so
            //! the frame is skipped and the slot handed straight back.
            _framePool.reset();
            _frameSlots->release();
            return;
        }
    }

    _commandBuffer = _deviceManager->getCommandQueue()->commandBuffer();
    if (!_commandBuffer) {
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

    MTL::RenderPassDescriptor* pass =
        _drawableManager->buildRenderPass(_clearR, _clearG, _clearB, _clearA);

    if (!pass)
        return;

    _encoder = _commandBuffer->renderCommandEncoder(pass);
    if (!_encoder) {
        INK_WARN << "MetalRenderer: failed to open a render command encoder";
        return;
    }

    /*
     * The scene pipeline is bound up front so an application that draws 3D
     * without an intervening overlay needs no bind of its own. drawBatch2D()
     * swaps to the overlay pipeline and swaps back, so this stays the state any
     * scene draw can assume.
     *
     * No setViewport() call: Metal's default viewport is the whole attachment
     * with a [0,1] depth range, which is exactly what is wanted. Vulkan needs an
     * explicit one only because it flips Y through a negative height there.
     */
    _scenePipeline->bind(_encoder);
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
        _commandBuffer->addCompletedHandler([slots](MTL::CommandBuffer*) { slots->release(); });

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

void MetalRenderer::setTransform(const gfx::TransformUBO& ubo)
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

MTL::Texture* MetalRenderer::resolveSampledTexture(TextureHandle handle)
{
    if (MTL::Texture* texture = _textureManager->resolve(handle))
        return texture;

    return _textureManager->resolve(_fallbackTexture);
}

void MetalRenderer::bindDrawState()
{
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

    if (MTL::Buffer* vertexBuffer = _vertexManager->resolve(_currentVertexBuffer))
        _encoder->setVertexBuffer(vertexBuffer, 0, kVertexGeometrySlot);
}

void MetalRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (!_renderPassActive || instanceCount == 0)
        return;

    const IndexBufferRecord* record = _indexManager->resolve(_currentIndexBuffer);
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

    _encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                    static_cast<NS::UInteger>(drawnIndices),
                                    record->indexType,
                                    record->buffer,
                                    0,
                                    static_cast<NS::UInteger>(instanceCount));
}

void MetalRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (!_renderPassActive || vertexCount == 0 || instanceCount == 0)
        return;

    if (!_vertexManager->resolve(_currentVertexBuffer))
        return;

    bindDrawState();

    _encoder->drawPrimitives(MTL::PrimitiveTypeTriangle,
                             static_cast<NS::UInteger>(0),
                             static_cast<NS::UInteger>(vertexCount),
                             static_cast<NS::UInteger>(instanceCount));
}

bool MetalRenderer::ensureOverlay2DCapacity(size_t vertexBytes, size_t indexBytes)
{
    const u32 frame = _currentFrame;

    if (_overlayVertexCapacity[frame] < vertexBytes) {
        NS::SharedPtr<MTL::Buffer> buffer =
            _bufferManager->createDynamic(vertexBytes, "Aura3D overlay vertices");

        if (!buffer)
            return false;

        _overlayVertexBuffers[frame] = std::move(buffer);
        _overlayVertexCapacity[frame] = vertexBytes;
    }

    if (_overlayIndexCapacity[frame] < indexBytes) {
        NS::SharedPtr<MTL::Buffer> buffer =
            _bufferManager->createDynamic(indexBytes, "Aura3D overlay indices");

        if (!buffer)
            return false;

        _overlayIndexBuffers[frame] = std::move(buffer);
        _overlayIndexCapacity[frame] = indexBytes;
    }

    return true;
}

void MetalRenderer::drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                               std::span<const u32> indices,
                               TextureHandle texture)
{
    AURA_FRAME_SCOPE(FramePhase::RecordOverlay);

    if (!_renderPassActive || vertices.empty() || indices.empty())
        return;

    const size_t vertexBytes = vertices.size_bytes();
    const size_t indexBytes = indices.size_bytes();

    if (!ensureOverlay2DCapacity(vertexBytes, indexBytes))
        return;

    MTL::Buffer* vertexBuffer = _overlayVertexBuffers[_currentFrame].get();
    MTL::Buffer* indexBuffer = _overlayIndexBuffers[_currentFrame].get();

    /*
     * A plain memcpy into shared storage. Safe to write without any barrier
     * because these are this frame slot's own buffers, and beginFrame()'s
     * semaphore acquire already established that the last frame to use this slot
     * has completed on the GPU.
     */
    std::memcpy(vertexBuffer->contents(), vertices.data(), vertexBytes);
    std::memcpy(indexBuffer->contents(), indices.data(), indexBytes);

    const f32 width = static_cast<f32>(_layerManager->getWidth());
    const f32 height = static_cast<f32>(_layerManager->getHeight());
    if (width <= 0.0f || height <= 0.0f)
        return;

    /*
     * Window pixels -> clip space, taken from the drawable's size rather than
     * wma's logical window size: those differ by the backing scale factor on a
     * Retina display, and it is the drawable that the projection has to cover
     * (the same reason the Vulkan path uses its swapchain extent).
     *
     * Passing height as `bottom` and 0 as `top` inverts the Y axis, which puts
     * pixel (0,0) at the top-left corner even though Metal's clip space grows
     * upwards -- the identical swap the OpenGL and Vulkan overlays make. _ZO
     * because Metal's depth range is [0,1]; the value is irrelevant with the
     * overlay's depth test disabled.
     */
    Overlay2DUniforms overlay;
    overlay.proj = glm::orthoRH_ZO(0.0f, width, height, 0.0f, 0.0f, 1.0f);

    _overlayPipeline->bind(_encoder);
    _encoder->setVertexBuffer(vertexBuffer, 0, kVertexGeometrySlot);
    _encoder->setVertexBytes(&overlay, sizeof(overlay), kVertexUniformSlot);
    _encoder->setFragmentTexture(resolveSampledTexture(texture), kFragmentAlbedoSlot);
    _encoder->setFragmentSamplerState(_textureManager->getSampler(), kFragmentAlbedoSlot);

    //! The whole batch in one call -- the point of the exercise.
    _encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                    static_cast<NS::UInteger>(indices.size()),
                                    MTL::IndexTypeUInt32,
                                    indexBuffer,
                                    0);

    /*
     * Hand the scene pipeline back, so a following 3D draw needs no knowledge
     * that an overlay ran. Its own bindDrawState() re-pushes the transform,
     * light and texture, so only the pipeline/depth/cull triple has to be
     * restored here.
     */
    _scenePipeline->bind(_encoder);
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
