#include "aura/Renderer/Metal/MetalRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/AuraMath.h"
#include "aura/Core/Files.h"
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

    createWindow(settings->windowTitle().c_str(), settings->windowBackend());

    _frameSlots = std::make_shared<FrameSlots>(MTL_MAX_FRAMES_IN_FLIGHT);
#ifdef AURA_PROFILE_FRAME
    _gpuClock = std::make_shared<GpuClock>();
#endif
    _deviceManager = std::make_unique<MtlDeviceManager>();

    _layerManager = std::make_unique<MtlLayerManager>(_deviceManager->getDevice(), _windowManagerApi.get(),
                                                      VSyncModeSyncsToDisplay(settings->presentMode()));

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
    buildPipelines(kColorFormat, kDepthFormat, _scenePipeline, _batchPipeline, _instancedPipeline);
}

bool MetalRenderer::reloadShaders(const std::filesystem::path &directory)
{
    if (!_isInitialized || _frameState != FrameState::Idle)
        return false;
    //! Every .metal file in name order, one library, as cmake/EmbedShaderHeader.cmake builds it.
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(directory / "metal", error))
        if (entry.path().extension() == ".metal")
            files.push_back(entry.path());
    std::ranges::sort(files);
    std::string source;
    for (const std::filesystem::path &file : files)
    {
        const auto bytes = readFileBytes(file);
        if (!bytes)
        {
            INK_ERROR << "MetalRenderer: cannot read " << file.string();
            return false;
        }
        source.append(reinterpret_cast<const char *>(bytes->data()), bytes->size());
        source.push_back('\n');
    }
    if (files.empty())
        return false;

    //! Everything is built from the new library before anything running is replaced.
    std::unique_ptr<MtlShaderLibraryManager> library;
    try
    {
        library = std::make_unique<MtlShaderLibraryManager>(_deviceManager->getDevice(), source);
    }
    catch (const std::exception &failure)
    {
        INK_ERROR << "MetalRenderer: shader reload failed, keeping the running shaders: " << failure.what();
        return false;
    }
    std::swap(library, _shaderLibrary);
    std::unique_ptr<MtlPipelineManager> scene, batch, instanced;
    std::vector<std::array<std::unique_ptr<MtlPipelineManager>, 3>> targets(_targetPipelines.size());
    try
    {
        buildPipelines(kColorFormat, kDepthFormat, scene, batch, instanced);
        for (usize i = 0; i < targets.size(); ++i)
        {
            const MTL::PixelFormat depth = _targetPipelines[i].depth ? kDepthFormat : MTL::PixelFormatInvalid;
            buildPipelines(_targetPipelines[i].colour, depth, targets[i][0], targets[i][1], targets[i][2]);
        }
    }
    catch (const std::exception &failure)
    {
        std::swap(library, _shaderLibrary);
        INK_ERROR << "MetalRenderer: shader reload failed, keeping the running shaders: " << failure.what();
        return false;
    }
    _scenePipeline = std::move(scene);
    _batchPipeline = std::move(batch);
    _instancedPipeline = std::move(instanced);
    //! In place: render targets point at these entries.
    for (usize i = 0; i < targets.size(); ++i)
    {
        _targetPipelines[i].scene = std::move(targets[i][0]);
        _targetPipelines[i].batch = std::move(targets[i][1]);
        _targetPipelines[i].instanced = std::move(targets[i][2]);
    }
    _activeScene = _activeBatch = _activeInstanced = nullptr;
    _boundPipeline = nullptr;
    return true;
}

NS::SharedPtr<MTL::Texture> MetalRenderer::newDepthTexture(u32 width, u32 height, MTL::TextureUsage usage) const
{
    NS::SharedPtr<MTL::TextureDescriptor> descriptor = adopt(MTL::TextureDescriptor::alloc()->init());
    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(kDepthFormat);
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setUsage(usage);
    descriptor->setStorageMode(MTL::StorageModePrivate);
    return adopt(_deviceManager->getDevice()->newTexture(descriptor.get()));
}

void MetalRenderer::buildPipelines(MTL::PixelFormat colour, MTL::PixelFormat depth,
                                   std::unique_ptr<MtlPipelineManager> &scene,
                                   std::unique_ptr<MtlPipelineManager> &batch,
                                   std::unique_ptr<MtlPipelineManager> &instanced) const
{
    MtlPipelineManager::Options sceneOptions;
    sceneOptions.vertexFunction = kVertexFunction3D;
    sceneOptions.fragmentFunction = kFragmentFunction3D;
    sceneOptions.state = PipelineState::DepthTest | PipelineState::DepthWrite | PipelineState::CullBackFaces;
    sceneOptions.label = "Aura3D scene 3D";
    sceneOptions.colorFormat = colour;
    sceneOptions.depthFormat = depth;
    scene = std::make_unique<MtlPipelineManager>(_deviceManager->getDevice(), *_shaderLibrary, sceneOptions);
    sceneOptions.vertexFunction = kVertexFunction3DInstanced;
    sceneOptions.label = "Aura3D scene 3D instanced";
    instanced = std::make_unique<MtlPipelineManager>(_deviceManager->getDevice(), *_shaderLibrary, sceneOptions);

    MtlPipelineManager::Options batchOptions;
    batchOptions.vertexFunction = kVertexFunctionBatch;
    batchOptions.fragmentFunction = kFragmentFunctionBatch;
    batchOptions.state = PipelineState::DepthTest | PipelineState::AlphaBlend;
    batchOptions.depthCompare = MTL::CompareFunctionLessEqual;
    batchOptions.label = "Aura3D batch";
    batchOptions.colorFormat = colour;
    batchOptions.depthFormat = depth;
    batch = std::make_unique<MtlPipelineManager>(_deviceManager->getDevice(), *_shaderLibrary, batchOptions);
}

MetalRenderer::TargetPipelines &MetalRenderer::pipelinesFor(MTL::PixelFormat colour, bool depth)
{
    for (TargetPipelines &cached : _targetPipelines)
        if (cached.colour == colour && cached.depth == depth)
            return cached;
    TargetPipelines &built = _targetPipelines.emplace_back();
    built.colour = colour;
    built.depth = depth;
    buildPipelines(colour, depth ? kDepthFormat : MTL::PixelFormatInvalid, built.scene, built.batch, built.instanced);
    return built;
}

RenderTargetHandle MetalRenderer::createRenderTarget(const RenderTargetDesc &desc)
{
    if (!_textureManager || !_shaderLibrary)
        return {};
    if (desc.format == TextureFormat::Depth)
    {
        INK_ERROR << "MetalRenderer: depth-only render targets are not supported yet";
        return {};
    }
    RenderTarget target;
    target.colour = _textureManager->createAttachment(desc.width, desc.height, desc.format);
    if (!isValidHandle(target.colour))
        return {};
    target.width = desc.width;
    target.height = desc.height;
    if (desc.depth)
    {
        target.depth = newDepthTexture(desc.width, desc.height, MTL::TextureUsageRenderTarget);
        if (!target.depth)
            return {};
    }
    target.pipelines = &pipelinesFor(_textureManager->resolve(target.colour)->pixelFormat(), desc.depth);
    _renderTargets.push_back(std::move(target));
    return RenderTargetHandle{static_cast<u32>(_renderTargets.size())};
}

glm::uvec2 MetalRenderer::targetSize(RenderTargetHandle target) const
{
    return isValidHandle(target) && target.value() <= _renderTargets.size()
               ? glm::uvec2{_renderTargets[target.value() - 1].width, _renderTargets[target.value() - 1].height}
               : glm::uvec2{0};
}

bool MetalRenderer::resizeRenderTarget(RenderTargetHandle handle, u32 width, u32 height)
{
    if (!_textureManager || !isValidHandle(handle) || handle.value() > _renderTargets.size() || width == 0 ||
        height == 0 || _frameState == FrameState::InPass)
        return false;
    RenderTarget &target = _renderTargets[handle.value() - 1];
    MTL::Texture *current = _textureManager->resolve(target.colour);
    if (!current)
        return false;
    const TextureFormat format = current->pixelFormat() == MTL::PixelFormatRGBA16Float  ? TextureFormat::Rgba16Float
                                 : current->pixelFormat() == MTL::PixelFormatRGBA8Unorm ? TextureFormat::Rgba8Unorm
                                                                                        : TextureFormat::Rgba8Srgb;
    if (!isValidHandle(_textureManager->createAttachment(width, height, format, target.colour)))
        return false;
    if (target.depth)
        target.depth = newDepthTexture(width, height, MTL::TextureUsageRenderTarget);
    target.width = width;
    target.height = height;
    return !target.depth || target.depth->width() == width;
}

TextureHandle MetalRenderer::renderTargetTexture(RenderTargetHandle target) const
{
    return isValidHandle(target) && target.value() <= _renderTargets.size() ? _renderTargets[target.value() - 1].colour
                                                                            : TextureHandle{};
}

void MetalRenderer::beginRenderPass(RenderTargetHandle target)
{
    if (!isValidHandle(target))
        return beginRenderPass();
    AURA_FRAME_SCOPE(FramePhase::BeginPass);
    if (_frameState != FrameState::Recording || target.value() > _renderTargets.size())
        return;
    const RenderTarget &chosen = _renderTargets[target.value() - 1];

    NS::SharedPtr<MTL::RenderPassDescriptor> pass = adopt(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor *colour = pass->colorAttachments()->object(0);
    colour->setTexture(_textureManager->resolve(chosen.colour));
    colour->setLoadAction(MTL::LoadActionClear);
    colour->setStoreAction(MTL::StoreActionStore);
    colour->setClearColor(MTL::ClearColor(static_cast<double>(_clearR), static_cast<double>(_clearG),
                                          static_cast<double>(_clearB), static_cast<double>(_clearA)));
    if (chosen.depth)
    {
        MTL::RenderPassDepthAttachmentDescriptor *depth = pass->depthAttachment();
        depth->setTexture(chosen.depth.get());
        depth->setLoadAction(MTL::LoadActionClear);
        depth->setStoreAction(MTL::StoreActionDontCare);
        depth->setClearDepth(1.0);
    }
    _encoder = _commandBuffer->renderCommandEncoder(pass.get());
    if (!_encoder)
    {
        INK_WARN << "MetalRenderer: failed to open a render target encoder";
        return;
    }
    _activeScene = chosen.pipelines->scene.get();
    _activeBatch = chosen.pipelines->batch.get();
    _activeInstanced = chosen.pipelines->instanced.get();
    _boundPipeline = nullptr;
    usePipeline(_activeScene);
    _passTarget = target.value();
    _frameState = FrameState::InPass;
}

bool MetalRenderer::readPixels(RenderTargetHandle target, std::span<u8> rgba)
{
    if (!isValidHandle(target) || target.value() > _renderTargets.size() || !_textureManager)
        return false;
    const RenderTarget &chosen = _renderTargets[target.value() - 1];
    if (rgba.size() != static_cast<usize>(chosen.width) * chosen.height * 4)
        return false;
    MTL::Texture *texture = _textureManager->resolve(chosen.colour);
    const bool half = texture->pixelFormat() == MTL::PixelFormatRGBA16Float;
    const NS::UInteger rowBytes = static_cast<NS::UInteger>(chosen.width) * (half ? 8u : 4u);
    const NS::UInteger bytes = rowBytes * chosen.height;
    NS::SharedPtr<MTL::Buffer> readback =
        adopt(_deviceManager->getDevice()->newBuffer(bytes, MTL::ResourceStorageModeShared));
    if (!readback)
        return false;

    //! On the same queue as the frames, after them: waiting for this command buffer waits for those too.
    MTL::CommandBuffer *commands = _deviceManager->getCommandQueue()->commandBuffer();
    MTL::BlitCommandEncoder *blit = commands->blitCommandEncoder();
    blit->copyFromTexture(texture, 0, 0, MTL::Origin(0, 0, 0), MTL::Size(chosen.width, chosen.height, 1),
                          readback.get(), 0, rowBytes, bytes);
    blit->endEncoding();
    commands->commit();
    commands->waitUntilCompleted();

    const auto *source = static_cast<const u8 *>(readback->contents());
    if (!half)
    {
        std::memcpy(rgba.data(), source, rgba.size());
        return true;
    }
    for (usize i = 0; i < rgba.size(); ++i)
    {
        u16 value = 0;
        std::memcpy(&value, source + i * 2, sizeof(value));
        const f32 linear = halfToFloat(value);
        rgba[i] = linear > 0.0f ? static_cast<u8>(std::lround(std::min(linear, 1.0f) * 255.0f)) : u8{0};
    }
    return true;
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
    if (_frameState != FrameState::Idle)
    {
        if (_frameState == FrameState::InPass)
        {
            _encoder->endEncoding();
            _encoder = nullptr;
        }

        //! Dropped without commit: an uncommitted command buffer is simply
        //! released, and nothing this frame recorded needs to reach the screen.
        _commandBuffer = nullptr;
        _drawableManager->release();
        _framePool.reset();

        if (_frameSlots)
            _frameSlots->release();

        _frameState = FrameState::Idle;
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
    _activeScene = _activeBatch = _activeInstanced = nullptr;
    _buffers.clear();
    _kernels.clear();
    _instancedPipeline.reset();
    _renderTargets.clear();
    _targetPipelines.clear();
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

bool MetalRenderer::updateVertexBuffer(VertexBufferHandle handle, std::span<const gfx::Vertex3D> vertices)
{
    //! A blit in this frame's command buffer, so no render encoder may be open; Metal orders it against
    //! earlier frames' reads of the buffer and this frame's.
    MTL::Buffer *target = _vertexManager ? _vertexManager->resolve(handle) : nullptr;
    if (!target || _frameState != FrameState::Recording || vertices.size_bytes() > target->length())
        return false;
    NS::SharedPtr<MTL::Buffer> staging = adopt(
        _deviceManager->getDevice()->newBuffer(vertices.data(), vertices.size_bytes(), MTL::ResourceStorageModeShared));
    if (!staging)
        return false;
    MTL::BlitCommandEncoder *blit = _commandBuffer->blitCommandEncoder();
    blit->copyFromBuffer(staging.get(), 0, target, 0, vertices.size_bytes());
    blit->endEncoding();
    return true;
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

TextureHandle MetalRenderer::createTexture(const TextureDesc &desc, std::span<const std::byte> pixels)
{
    if (!_textureManager || desc.format == TextureFormat::Depth)
        return {};
    return _textureManager->createTexture(desc, pixels, _deviceManager->getCommandQueue());
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

    _frameState = FrameState::Idle;

    if (consumeResize())
        handleWindowChanges();

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

    _frameState = FrameState::Recording;
}

void MetalRenderer::beginRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    //! cleanup() can run mid-frame (the ESC key action calls it), which drops the
    //! managers while the application's frame callback is still executing.
    if (_frameState != FrameState::Recording || !_drawableManager)
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
    _activeScene = _scenePipeline.get();
    _activeBatch = _batchPipeline.get();
    _activeInstanced = _instancedPipeline.get();
    _boundPipeline = nullptr;
    usePipeline(_activeScene);
    _passTarget = 0;
    _frameState = FrameState::InPass;
}

void MetalRenderer::endRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (_frameState != FrameState::InPass)
        return;

    _encoder->endEncoding();
    _encoder = nullptr;
    _frameState = FrameState::Recording;
}

#ifdef AURA_PROFILE_FRAME
GpuTimingStats MetalRenderer::gpuTiming() const noexcept
{
    if (!_gpuTimingEnabled || !_gpuClock)
        return {};

    GpuTimingStats timing;
    timing.available = _gpuClock->resolved.load(std::memory_order_acquire) > 0;
    timing.frameMillis = static_cast<f64>(_gpuClock->frameNanos.load(std::memory_order_relaxed)) / 1.0e6;
    timing.droppedSamples = _gpuClock->dropped.load(std::memory_order_relaxed);

    return timing;
}
#endif

void MetalRenderer::endFrame()
{
    //! Before any scope opens, matching beginFrame()/beginRenderPass()'s own
    //! early return on the same flag: a frame that never began records no
    //! samples for any phase, rather than a Present entry with nothing behind
    //! it. AURA_FRAME_END() below is reached only past this point, so a
    //! skipped frame closes nothing and biases no average.
    if (_frameState == FrameState::Idle)
        return;

    //! Tolerate a caller that never closed its pass: an encoder still open when
    //! the command buffer is committed is a hard Metal error, not a warning.
    //! endRenderPass() opens its own EndPass scope, so this is not double-timed
    //! by wrapping it in one here too.
    if (_frameState == FrameState::InPass)
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
#ifdef AURA_PROFILE_FRAME
        //! Null while timing is off, so the handler then only releases the slot.
        const std::shared_ptr<GpuClock> clock = _gpuTimingEnabled ? _gpuClock : nullptr;
        _commandBuffer->addCompletedHandler(
            [slots, clock](MTL::CommandBuffer *buffer)
            {
                if (clock && buffer->status() == MTL::CommandBufferStatusCompleted)
                {
                    const f64 seconds = buffer->GPUEndTime() - buffer->GPUStartTime();
                    clock->frameNanos.store(static_cast<i64>(seconds * 1.0e9), std::memory_order_relaxed);
                    clock->resolved.fetch_add(1, std::memory_order_release);
                }
                else if (clock)
                    clock->dropped.fetch_add(1, std::memory_order_relaxed);
                slots->release();
            });
#else
        _commandBuffer->addCompletedHandler(
            [slots](MTL::CommandBuffer *)
            {
                slots->release();
            });
#endif

        _commandBuffer->commit();
    }

#ifdef AURA_PROFILE_FRAME
    if (const u64 resolved = _gpuClock->resolved.load(std::memory_order_acquire); resolved != _gpuReported)
    {
        _gpuReported = resolved;
        AURA_FRAME_GPU(_gpuClock->frameNanos.load(std::memory_order_relaxed));
    }
#endif

    /*
     * Metal keeps its own reference to the drawable and to every resource the
     * command buffer touched, so releasing them here cannot pull anything out
     * from under the GPU.
     */
    _drawableManager->release();
    _commandBuffer = nullptr;
    _framePool.reset();

    _currentFrame = (_currentFrame + 1) % MTL_MAX_FRAMES_IN_FLIGHT;
    _frameState = FrameState::Idle;

    //! Outside the Present scope so it is closed and counted before the frame
    //! is.
    AURA_FRAME_END();
}

MTL::Texture *MetalRenderer::resolveSampledTexture(TextureHandle handle)
{
    if (MTL::Texture *texture = _textureManager->resolve(handle))
        return texture;

    return _textureManager->resolve(_fallbackTexture);
}

void MetalRenderer::bindDrawState()
{
    usePipeline(_activeScene);

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
    bindSceneMaterial();

    _encoder->setFragmentTexture(resolveSampledTexture(_currentTexture), kFragmentAlbedoSlot);
    _encoder->setFragmentSamplerState(_textureManager->samplerFor(_currentTexture), kFragmentAlbedoSlot);

    if (MTL::Buffer *vertexBuffer = _vertexManager->resolve(_currentVertexBuffer))
        _encoder->setVertexBuffer(vertexBuffer, 0, kVertexGeometrySlot);
}

void MetalRenderer::bindSceneMaterial()
{
    const gfx::LightingUBO block = lighting();
    _encoder->setFragmentBytes(&block, sizeof(block), kFragmentLightSlot);
    const gfx::MaterialUniforms material = materialUniforms(_currentMaterial);
    _encoder->setFragmentBytes(&material, sizeof(material), kFragmentMaterialSlot);
    //! An absent map samples the fallback texture, which its flag tells the shader to ignore.
    const std::array<TextureHandle, 3> maps{_currentMaterial.normalMap, _currentMaterial.metallicRoughness,
                                            _currentMaterial.emissiveMap};
    for (NS::UInteger i = 0; i < maps.size(); ++i)
    {
        _encoder->setFragmentTexture(resolveSampledTexture(maps[i]), kFragmentFirstMapSlot + i);
        _encoder->setFragmentSamplerState(_textureManager->samplerFor(maps[i]), kFragmentFirstMapSlot + i);
    }
}

RendererCaps MetalRenderer::caps() const noexcept
{
    return RendererCaps::Pbr | RendererCaps::Instancing | RendererCaps::RenderTargets | RendererCaps::Compute;
}

void MetalRenderer::usePipeline(MtlPipelineManager *pipeline)
{
    if (_boundPipeline == pipeline)
        return;
    pipeline->bind(_encoder);
    _boundPipeline = pipeline;
}

void MetalRenderer::drawMeshInstanced(MeshHandle mesh, MaterialHandle material, std::span<const glm::mat4> models)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    if (_frameState != FrameState::InPass || !_activeInstanced)
        return;
    if (isValidHandle(material))
        bindMaterial(material);
    const MeshRecord *record = getMesh(mesh);
    const IndexBufferRecord *indices = record ? _indexManager->resolve(record->indexBuffer) : nullptr;
    MTL::Buffer *vertices = record ? _vertexManager->resolve(record->vertexBuffer) : nullptr;
    if (!indices || !indices->buffer || !vertices)
        return;

    const Frustum frustum = viewFrustum();
    _instanceScratch.clear();
    for (const glm::mat4 &model : models)
        if (!culled(*record, model, frustum))
            _instanceScratch.push_back({model, glm::mat4(gfx::normalMatrixOf(model))});
    if (_instanceScratch.empty())
        return;

    usePipeline(_activeInstanced);
    TransformUniforms transform;
    transform.view = _currentTransform.view;
    transform.proj = _currentTransform.proj;
    _encoder->setVertexBytes(&transform, sizeof(transform), kVertexUniformSlot);
    bindSceneMaterial();
    _encoder->setFragmentTexture(resolveSampledTexture(_currentTexture), kFragmentAlbedoSlot);
    _encoder->setFragmentSamplerState(_textureManager->samplerFor(_currentTexture), kFragmentAlbedoSlot);
    _encoder->setVertexBuffer(vertices, 0, kVertexGeometrySlot);

    const NS::UInteger bytes = _instanceScratch.size() * sizeof(InstanceUniforms);
    //! setVertexBytes() copies up to 4 KB into the command buffer; more goes through a buffer it retains.
    constexpr NS::UInteger kInlineLimit = 4096;
    if (bytes <= kInlineLimit)
        _encoder->setVertexBytes(_instanceScratch.data(), bytes, kVertexInstanceSlot);
    else
    {
        const NS::SharedPtr<MTL::Buffer> buffer = adopt(
            _deviceManager->getDevice()->newBuffer(_instanceScratch.data(), bytes, MTL::ResourceStorageModeShared));
        if (!buffer)
            return;
        _encoder->setVertexBuffer(buffer.get(), 0, kVertexInstanceSlot);
    }
    _encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indices->indexCount),
                                    indices->indexType, indices->buffer, 0,
                                    static_cast<NS::UInteger>(_instanceScratch.size()));
}

void MetalRenderer::waitForFrames()
{
    //! Every committed frame's completion handler hands its permit back; this frame, if open, holds one.
    if (!_frameSlots)
        return;
    const u32 committed = MTL_MAX_FRAMES_IN_FLIGHT - (_frameState == FrameState::Idle ? 0u : 1u);
    for (u32 slot = 0; slot < committed; ++slot)
        _frameSlots->acquire();
    for (u32 slot = 0; slot < committed; ++slot)
        _frameSlots->release();
}

BufferHandle MetalRenderer::createBuffer(usize bytes, std::span<const std::byte> initial)
{
    if (!_deviceManager || bytes == 0 || initial.size() > bytes)
        return {};
    NS::SharedPtr<MTL::Buffer> buffer =
        adopt(_deviceManager->getDevice()->newBuffer(bytes, MTL::ResourceStorageModeShared));
    if (!buffer)
        return {};
    std::memset(buffer->contents(), 0, bytes);
    std::memcpy(buffer->contents(), initial.data(), initial.size());
    _buffers.push_back(std::move(buffer));
    return BufferHandle{static_cast<u32>(_buffers.size())};
}

MTL::Buffer *MetalRenderer::bufferOf(BufferHandle handle) const noexcept
{
    return isValidHandle(handle) && handle.value() <= _buffers.size() ? _buffers[handle.value() - 1].get() : nullptr;
}

bool MetalRenderer::updateBuffer(BufferHandle handle, usize offset, std::span<const std::byte> bytes)
{
    MTL::Buffer *buffer = bufferOf(handle);
    if (!buffer || offset > buffer->length() || bytes.size() > buffer->length() - offset ||
        _frameState == FrameState::InPass)
        return false;
    if (_frameState == FrameState::Idle)
    {
        waitForFrames();
        std::memcpy(static_cast<std::byte *>(buffer->contents()) + offset, bytes.data(), bytes.size());
        return true;
    }
    //! A blit in this frame's command buffer, ordered by Metal against the buffer's other uses.
    NS::SharedPtr<MTL::Buffer> staging =
        adopt(_deviceManager->getDevice()->newBuffer(bytes.data(), bytes.size(), MTL::ResourceStorageModeShared));
    if (!staging)
        return false;
    MTL::BlitCommandEncoder *blit = _commandBuffer->blitCommandEncoder();
    blit->copyFromBuffer(staging.get(), 0, buffer, offset, bytes.size());
    blit->endEncoding();
    return true;
}

bool MetalRenderer::readBuffer(BufferHandle handle, usize offset, std::span<std::byte> out)
{
    MTL::Buffer *buffer = bufferOf(handle);
    if (!buffer || offset > buffer->length() || out.size() > buffer->length() - offset)
        return false;
    waitForFrames();
    std::memcpy(out.data(), static_cast<const std::byte *>(buffer->contents()) + offset, out.size());
    return true;
}

ComputeHandle MetalRenderer::createCompute(const ComputeDesc &desc)
{
    if (!_deviceManager || desc.msl.empty())
    {
        INK_ERROR << "MetalRenderer: createCompute needs ComputeDesc::msl";
        return {};
    }
    MTL::Device *device = _deviceManager->getDevice();
    const NS::SharedPtr<NS::String> source = makeString(desc.msl.c_str());
    NS::SharedPtr<MTL::CompileOptions> options = adopt(MTL::CompileOptions::alloc()->init());
    options->setLanguageVersion(MTL::LanguageVersion3_0);
    NS::Error *error = nullptr;
    const NS::SharedPtr<MTL::Library> library = adopt(device->newLibrary(source.get(), options.get(), &error));
    const NS::SharedPtr<NS::String> entry = makeString(desc.mslEntry.c_str());
    const NS::SharedPtr<MTL::Function> function =
        library ? adopt(library->newFunction(entry.get())) : NS::SharedPtr<MTL::Function>{};
    NS::SharedPtr<MTL::ComputePipelineState> pipeline =
        function ? adopt(device->newComputePipelineState(function.get(), &error))
                 : NS::SharedPtr<MTL::ComputePipelineState>{};
    if (!pipeline)
    {
        INK_ERROR << "MetalRenderer: compute kernel rejected"
                  << (error ? std::string(": ") + error->localizedDescription()->utf8String() : std::string{});
        return {};
    }
    _kernels.push_back({std::move(pipeline), desc.buffers, desc.mslThreadsPerGroup});
    return ComputeHandle{static_cast<u32>(_kernels.size())};
}

void MetalRenderer::dispatch(ComputeHandle handle, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                             std::span<const std::byte> constants)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    //! An encoder of its own, so no render pass may be open.
    if (_frameState != FrameState::Recording || !isValidHandle(handle) || handle.value() > _kernels.size())
        return;
    const Kernel &kernel = _kernels[handle.value() - 1];
    if (buffers.size() < kernel.buffers)
        return;
    MTL::ComputeCommandEncoder *encoder = _commandBuffer->computeCommandEncoder();
    if (!encoder)
        return;
    encoder->setComputePipelineState(kernel.pipeline.get());
    for (NS::UInteger i = 0; i < kernel.buffers; ++i)
        encoder->setBuffer(bufferOf(buffers[i]), 0, i);
    if (!constants.empty())
        encoder->setBytes(constants.data(), constants.size(), kernel.buffers);
    encoder->dispatchThreadgroups(MTL::Size(groups.x, groups.y, groups.z),
                                  MTL::Size(kernel.threads.x, kernel.threads.y, kernel.threads.z));
    encoder->endEncoding();
}

void MetalRenderer::drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                              BufferHandle arguments, usize argumentOffset)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    MTL::Buffer *instanceBuffer = bufferOf(instances);
    MTL::Buffer *argumentBuffer = bufferOf(arguments);
    const MeshRecord *record = getMesh(mesh);
    if (_frameState != FrameState::InPass || !_activeInstanced || !instanceBuffer || !argumentBuffer || !record ||
        argumentOffset % 4 != 0 || argumentOffset + sizeof(DrawIndexedIndirect) > argumentBuffer->length())
        return;
    if (isValidHandle(material))
        bindMaterial(material);
    MTL::Buffer *vertices = _vertexManager->resolve(record->vertexBuffer);
    const IndexBufferRecord *indices = _indexManager->resolve(record->indexBuffer);
    if (!vertices || !indices || !indices->buffer)
        return;
    usePipeline(_activeInstanced);
    TransformUniforms transform;
    transform.view = _currentTransform.view;
    transform.proj = _currentTransform.proj;
    _encoder->setVertexBytes(&transform, sizeof(transform), kVertexUniformSlot);
    bindSceneMaterial();
    _encoder->setFragmentTexture(resolveSampledTexture(_currentTexture), kFragmentAlbedoSlot);
    _encoder->setFragmentSamplerState(_textureManager->samplerFor(_currentTexture), kFragmentAlbedoSlot);
    _encoder->setVertexBuffer(vertices, 0, kVertexGeometrySlot);
    static_assert(sizeof(InstanceUniforms) == sizeof(InstanceTransform));
    _encoder->setVertexBuffer(instanceBuffer, 0, kVertexInstanceSlot);
    _encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, indices->indexType, indices->buffer, 0, argumentBuffer,
                                    argumentOffset);
}

void MetalRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (_frameState != FrameState::InPass || instanceCount == 0)
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
    if (_frameState != FrameState::InPass || vertexCount == 0 || instanceCount == 0)
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
    if (_frameState == FrameState::InPass && _passTarget != 0)
        return {_renderTargets[_passTarget - 1].width, _renderTargets[_passTarget - 1].height};
    return _layerManager ? glm::uvec2{_layerManager->getWidth(), _layerManager->getHeight()} : glm::uvec2{0};
}

void MetalRenderer::drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                              TextureHandle texture, gfx::BatchSpace space)
{
    AURA_FRAME_SCOPE(space == gfx::BatchSpace::Screen ? FramePhase::RecordOverlay : FramePhase::RecordScene);

    if (_frameState != FrameState::InPass || !_activeBatch || vertices.empty() || indices.empty())
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
    usePipeline(_activeBatch);
    _encoder->setVertexBuffer(vertexBuffer, vertexOffset, kVertexGeometrySlot);
    _encoder->setVertexBytes(&batch, sizeof(batch), kVertexUniformSlot);
    _encoder->setFragmentTexture(resolveSampledTexture(texture), kFragmentAlbedoSlot);
    _encoder->setFragmentSamplerState(_textureManager->samplerFor(texture), kFragmentAlbedoSlot);
    _encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indices.size()),
                                    MTL::IndexTypeUInt32, indexBuffer, static_cast<NS::UInteger>(indexOffset));
}

} // namespace mtl
} // namespace aura3d
