#include "aura/Renderer/Software/CPURenderer.h"

#include "aura/aura.h"

namespace aura3d {
namespace cpu {

CPURenderer::CPURenderer(const wma::WindowDetails& windowDetails)
    : IRenderer(windowDetails), _workerPool(std::thread::hardware_concurrency())
{
    INK_INFO << "Renderer - SOFTWARE";
}

CPURenderer::~CPURenderer()
{
    cleanup();
}

void CPURenderer::initialize(const aura3d::AuraSettings* settings)
{
    _vertexBufferPool3d.reserve(256);
    _indexBufferPool.reserve(256);
    _texturePool.reserve(64);

    createWindow(APPLICATION_NAME, wma::WindowBackend::SDL3);
}

void CPURenderer::createWindow(const char* title, const wma::WindowBackend& wBackend)
{
    _windowManagerApi = wma::createWindowManager(
        wBackend, _windowDetails, wma::GraphicsAPI::CPU);

    _windowManagerApi->createWindow(title);

    CpuFrameBufferManager::Config cfg = {};
    cfg.width  = _windowDetails.width;
    cfg.height = _windowDetails.height;

    _frameBufferManager = std::make_unique<CpuFrameBufferManager>(
        (SDL_Window*)_windowManagerApi->getWindowInstance(), cfg);

    SDL_PropertiesID props = SDL_GetWindowProperties((SDL_Window*)_windowManagerApi->getWindowInstance());
    SDL_SetPointerProperty(props, "FrameBufferManager", this);
}

void CPURenderer::handleWindowChanges()
{
    if (!_frameBufferManager) return;
    auto* wd = _windowManagerApi->getWindowDetails();
    _frameBufferManager->resizeFramebuffer(wd->width, wd->height);
}

void CPURenderer::cleanup()
{
    _frameBufferManager.reset();
    _windowManagerApi.reset();
    _vertexBufferPool3d.clear();
    _indexBufferPool.clear();
    _texturePool.clear();
}

VertexBufferHandle CPURenderer::createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices)
{
    _vertexBufferPool3d.push_back(std::move(vertices));
    return static_cast<VertexBufferHandle>(_vertexBufferPool3d.size() - 1);
}

IndexBufferHandle CPURenderer::createIndexBuffer(std::vector<u16>&& indices)
{
    std::vector<u32> upcasted;
    upcasted.reserve(indices.size());
    for (u16 idx : indices) {
        upcasted.push_back(static_cast<u32>(idx));
    }

    _indexBufferPool.push_back(std::move(upcasted));
    return static_cast<IndexBufferHandle>(_indexBufferPool.size() - 1);
}

IndexBufferHandle CPURenderer::createIndexBuffer(std::vector<u32>&& indices)
{
    _indexBufferPool.push_back(std::move(indices));
    return static_cast<IndexBufferHandle>(_indexBufferPool.size() - 1);
}

TextureHandle CPURenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    return INVALID_HANDLE;
}

void CPURenderer::beginFrame()
{
}

void CPURenderer::beginRenderPass()
{
    if (_frameBufferManager) {
        _frameBufferManager->clear(_clearColorU32);
    }
}

void CPURenderer::endRenderPass()
{
}

void CPURenderer::endFrame()
{
    if (_frameBufferManager) {
        _frameBufferManager->renderFramebuffer();
    }
}

void CPURenderer::setTransform(const gfx::TransformUBO& ubo)
{
    _currentTransform = ubo;
}

void CPURenderer::bindVertexBuffer(VertexBufferHandle handle)
{
}

void CPURenderer::bindIndexBuffer(IndexBufferHandle handle)
{
}

void CPURenderer::bindTexture(TextureHandle handle)
{
}

void CPURenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
}

void CPURenderer::draw(u32 vertexCount, u32 instanceCount)
{
}

void CPURenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    u8 lr = static_cast<u8>(r * 255);
    u8 lg = static_cast<u8>(g * 255);
    u8 lb = static_cast<u8>(b * 255);
    u8 la = static_cast<u8>(a * 255);
    _clearColorU32 = (la << 24) | (lr << 16) | (lg << 8) | lb;
}

} // namespace cpu
} // namespace aura3d
