#include "aura/Renderer/Software/CPURenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include "aura/aura.h"

namespace aura3d {
namespace cpu {

CPURenderer::CPURenderer(const wma::WindowDetails& windowDetails) :
    IRenderer(windowDetails),
    _workerPool(std::thread::hardware_concurrency())
{
    INK_INFO << "Renderer - SOFTWARE";
}

CPURenderer::~CPURenderer()
{
    cleanup();
}

void CPURenderer::initialize(aura3d::AuraSettings* settings)
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

    CpuFrameBufferManager::Config cfg;
    cfg.width  = _windowDetails.width;
    cfg.height = _windowDetails.height;
    cfg.useDepthBuffer = true;

    _frameBufferManager = std::make_unique<CpuFrameBufferManager>(
        static_cast<SDL_Window*>(_windowManagerApi->getWindowInstance()), cfg);
}

void CPURenderer::handleWindowChanges()
{
    if (!_frameBufferManager) 
        return;
    
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
    std::vector<u32> up;
    up.reserve(indices.size());
    for (u16 i : indices) 
        up.push_back(static_cast<u32>(i));
    _indexBufferPool.push_back(std::move(up));
    return static_cast<IndexBufferHandle>(_indexBufferPool.size() - 1);
}

IndexBufferHandle CPURenderer::createIndexBuffer(std::vector<u32>&& indices)
{
    _indexBufferPool.push_back(std::move(indices));
    return static_cast<IndexBufferHandle>(_indexBufferPool.size() - 1);
}

TextureHandle CPURenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    // Encode as ARGB8888 to match the framebuffer pixel format.
    const u32 argb = (static_cast<u32>(a) << 24)
                   | (static_cast<u32>(r) << 16)
                   | (static_cast<u32>(g) <<  8)
                   |  static_cast<u32>(b);

    Texture tex(1, 1);
    tex.data[0] = argb;

    // Textures are stored as mip-level vectors.
    // Handle is 1-based so that 0 remains INVALID_HANDLE.
    _texturePool.push_back({ std::move(tex) });
    return static_cast<TextureHandle>(_texturePool.size()); // 1-based
}

void CPURenderer::beginFrame()
{
    // Empty
}

void CPURenderer::beginRenderPass()
{
    if (_frameBufferManager)
        _frameBufferManager->clear(_clearColorU32);
}

void CPURenderer::endRenderPass()
{
    // Nothing: present happens in endFrame.
}

void CPURenderer::endFrame()
{
    if (_frameBufferManager)
        _frameBufferManager->renderFramebuffer();
}

void CPURenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    const u8 lr = static_cast<u8>(r * 255.0f);
    const u8 lg = static_cast<u8>(g * 255.0f);
    const u8 lb = static_cast<u8>(b * 255.0f);
    const u8 la = static_cast<u8>(a * 255.0f);
    _clearColorU32 = (static_cast<u32>(la) << 24)
                   | (static_cast<u32>(lr) << 16)
                   | (static_cast<u32>(lg) <<  8)
                   |  static_cast<u32>(lb);
}

void CPURenderer::setTransform(const gfx::TransformUBO& ubo)
{
    _currentTransform = ubo;
}

void CPURenderer::bindVertexBuffer(VertexBufferHandle handle)
{
    _boundVertexBuffer = handle;
}

void CPURenderer::bindIndexBuffer(IndexBufferHandle handle)
{
    _boundIndexBuffer = handle;
}

void CPURenderer::bindTexture(TextureHandle handle)
{
    _boundTexture = handle;
}

namespace {

/*
 * Transform a single Vertex3D through the MVP matrix into a ScreenVertex.
 * Returns false (by setting invW < 0) when the vertex is behind the camera.
 */
[[nodiscard]]
CpuFrameBufferManager::ScreenVertex projectVertex(const gfx::Vertex3D&  v,
                                                  const glm::mat4& MVP,
                                                  float W,
                                                  float H) noexcept
{
    CpuFrameBufferManager::ScreenVertex sv;

    const glm::vec4 clip = MVP * glm::vec4(v.pos, 1.0f);

    if (clip.w <= 0.0f) 
    {
        // behind camera
        sv.invW = -1.0f; 
        return sv;
    }

    const float invW = 1.0f / clip.w;
    const float nx = clip.x * invW;   // NDC X ∈ [−1, 1]
    const float ny = clip.y * invW;   // NDC Y ∈ [−1, 1]
    const float nz = clip.z * invW;   // NDC Z ∈ [−1, 1]

    sv.x = (nx  + 1.0f) * 0.5f * W;
    sv.y = (1.0f - (ny + 1.0f) * 0.5f) * H;  // flip Y (screen Y grows down)
    sv.z = (nz  + 1.0f) * 0.5f;               // remap [−1,1] → [0,1]
    sv.invW = invW;
    sv.uv = v.texCoord;
    sv.color= v.color;

    return sv;
}

} // anonymous namespace

void CPURenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (!_frameBufferManager) 
        return;

    if (_boundVertexBuffer >= _vertexBufferPool3d.size()) 
        return;

    if (_boundIndexBuffer  >= _indexBufferPool.size())    
        return;

    const auto& verts   = _vertexBufferPool3d[_boundVertexBuffer];
    const auto& indices = _indexBufferPool   [_boundIndexBuffer];

    // Resolve optional texture (1-based handle, 0 = INVALID_HANDLE)
    const Texture* texture = nullptr;
    if (_boundTexture > 0 && _boundTexture <= static_cast<TextureHandle>(_texturePool.size())) 
    {
        const auto& mips = _texturePool[_boundTexture - 1];
        if (!mips.empty()) texture = &mips[0];
    }

    const float W = static_cast<float>(_frameBufferManager->getWidth());
    const float H = static_cast<float>(_frameBufferManager->getHeight());
    const glm::mat4 MVP = _currentTransform.proj * _currentTransform.view * _currentTransform.model;

    const u32 safeCount = std::min(indexCount, static_cast<u32>(indices.size()));
    const u32 triCount  = safeCount / 3;

    for (u32 t = 0; t < triCount; ++t)
    {
        const u32 i0 = indices[t * 3    ];
        const u32 i1 = indices[t * 3 + 1];
        const u32 i2 = indices[t * 3 + 2];

        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
            continue;

        const auto sv0 = projectVertex(verts[i0], MVP, W, H);
        const auto sv1 = projectVertex(verts[i1], MVP, W, H);
        const auto sv2 = projectVertex(verts[i2], MVP, W, H);

        // Skip triangles with any vertex behind the near plane.
        if (sv0.invW < 0.0f || sv1.invW < 0.0f || sv2.invW < 0.0f) 
            continue;

        _frameBufferManager->drawTriangle(sv0, sv1, sv2, texture);
    }

    (void)instanceCount;
}

void CPURenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (!_frameBufferManager)  
        return;
    
    if (_boundVertexBuffer >= _vertexBufferPool3d.size()) 
        return;

    const auto& verts = _vertexBufferPool3d[_boundVertexBuffer];

    const Texture* texture = nullptr;
    if (_boundTexture > 0 && _boundTexture <= static_cast<TextureHandle>(_texturePool.size())) 
    {
        const auto& mips = _texturePool[_boundTexture - 1];
        if (!mips.empty()) texture = &mips[0];
    }

    const float W   = static_cast<float>(_frameBufferManager->getWidth());
    const float H   = static_cast<float>(_frameBufferManager->getHeight());
    const glm::mat4 MVP = _currentTransform.proj * _currentTransform.view * _currentTransform.model;

    const u32 safeCount = std::min(vertexCount, static_cast<u32>(verts.size()));
    const u32 triCount  = safeCount / 3;

    for (u32 t = 0; t < triCount; ++t)
    {
        const auto sv0 = projectVertex(verts[t * 3    ], MVP, W, H);
        const auto sv1 = projectVertex(verts[t * 3 + 1], MVP, W, H);
        const auto sv2 = projectVertex(verts[t * 3 + 2], MVP, W, H);

        if (sv0.invW < 0.0f || sv1.invW < 0.0f || sv2.invW < 0.0f) 
            continue;

        _frameBufferManager->drawTriangle(sv0, sv1, sv2, texture);
    }

    (void)instanceCount;
}

} // namespace cpu
} // namespace aura3d
