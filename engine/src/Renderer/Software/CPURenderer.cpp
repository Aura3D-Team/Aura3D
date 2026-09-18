#include "aura/Renderer/Software/CPURenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/JobSystem/JobSystem.h"
#include "aura/Core/Profiling/FrameProfiler.h"

namespace aura3d {
namespace cpu {

CPURenderer::CPURenderer(const wma::WindowDetails& windowDetails) :
    IRenderer(windowDetails)
{
    INK_INFO << "Renderer - SOFTWARE";
}

CPURenderer::~CPURenderer()
{
    cleanup();
}

void CPURenderer::initialize(aura3d::AuraSettings* settings, const JobSystem* jobs)
{
    _vertexBufferPool3d.reserve(256);
    _indexBufferPool.reserve(256);
    _texturePool.reserve(64);

    //! Stored before createWindow(): it builds _frameBufferManager, which
    //! dispatches both rasterisation and presentation through this pool
    //! instead of owning one of its own (see CpuFrameBufferManager's
    //! constructor comment).
    _jobs = jobs;

    createWindow(settings->getWindowTitle().c_str(), settings->getWindowBackend());
}

void CPURenderer::createWindow(const char* title, const wma::WindowBackend& wBackend)
{
    _windowManagerApi = makeWindow(
        wBackend, _windowDetails, wma::GraphicsAPI::CPU);
    _windowManagerApi->createWindow(title);

    CpuFrameBufferManager::Config cfg;
    cfg.width  = _windowDetails.width;
    cfg.height = _windowDetails.height;
    cfg.useDepthBuffer = true;

    _frameBufferManager = std::make_unique<CpuFrameBufferManager>(*_windowManagerApi, cfg, *_jobs);

    INK_INFO << "CPURenderer: rasterising across "
             << _frameBufferManager->getWorkerCount() << " worker thread(s) (shared engine pool)";
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
    clearSharedResources();
    _frameBufferManager.reset();
    _windowManagerApi.reset();
    _vertexBufferPool3d.clear();
    _indexBufferPool.clear();
    _texturePool.clear();
}

//! Pools are indexed 0-based but handles are 1-based, so that no valid handle
//! collides with the invalid-handle sentinel, and 0 stays reserved as "nothing bound".
VertexBufferHandle CPURenderer::createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices)
{
    _vertexBufferPool3d.push_back(std::move(vertices));
    return static_cast<VertexBufferHandle>(_vertexBufferPool3d.size());
}

IndexBufferHandle CPURenderer::createIndexBuffer(std::vector<u16>&& indices)
{
    std::vector<u32> up;
    up.reserve(indices.size());
    for (u16 i : indices)
        up.push_back(static_cast<u32>(i));
    _indexBufferPool.push_back(std::move(up));
    return static_cast<IndexBufferHandle>(_indexBufferPool.size());
}

IndexBufferHandle CPURenderer::createIndexBuffer(std::vector<u32>&& indices)
{
    _indexBufferPool.push_back(std::move(indices));
    return static_cast<IndexBufferHandle>(_indexBufferPool.size());
}

TextureHandle CPURenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    const u8 pixel[4] = { r, g, b, a };
    return createTextureFromPixels(pixel, 1, 1);
}

TextureHandle CPURenderer::createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height)
{
    if (!rgbaPixels || width == 0 || height == 0)
    {
        INK_ERROR << "CPURenderer: refusing to upload an empty texture";
        return {};
    }

    Texture tex(static_cast<int>(width), static_cast<int>(height));

    //! Repack RGBA8 into the ARGB8888 words the framebuffer samples.
    const size_t texelCount = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < texelCount; ++i) 
    {
        const u8 r = rgbaPixels[i * 4 + 0];
        const u8 g = rgbaPixels[i * 4 + 1];
        const u8 b = rgbaPixels[i * 4 + 2];
        const u8 a = rgbaPixels[i * 4 + 3];

        tex.data[i] = (static_cast<u32>(a) << 24)
                    | (static_cast<u32>(r) << 16)
                    | (static_cast<u32>(g) <<  8)
                    |  static_cast<u32>(b);
    }

    //! Textures are stored as mip-level vectors.
    //! Handle is 1-based so that no valid handle collides with the invalid-handle sentinel.
    _texturePool.push_back({ std::move(tex) });
    return static_cast<TextureHandle>(_texturePool.size()); // 1-based
}

TextureHandle CPURenderer::createDynamicTexture(u32 width, u32 height)
{
    if (width == 0 || height == 0)
    {
        INK_ERROR << "CPURenderer: refusing to allocate a zero-sized dynamic texture";
        return {};
    }

    //! Texture's constructor zero-fills, i.e. transparent black.
    Texture tex(static_cast<int>(width), static_cast<int>(height));
    _texturePool.push_back({ std::move(tex) });
    return static_cast<TextureHandle>(_texturePool.size()); // 1-based
}

void CPURenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y,
                                      u32 width, u32 height, const u8* rgbaPixels)
{
    if (!rgbaPixels || width == 0 || height == 0)
        return;

    if (!isValidHandle(handle) || handle.value() > _texturePool.size())
    {
        INK_ERROR << "CPURenderer: updateTextureRegion on an unknown texture";
        return;
    }

    auto& mips = _texturePool[handle.value() - 1];
    if (mips.empty())
        return;

    Texture& tex = mips[0];
    if (x > static_cast<u32>(tex.width) || y > static_cast<u32>(tex.height) ||
        width > static_cast<u32>(tex.width) - x || height > static_cast<u32>(tex.height) - y)
    {
        INK_ERROR << "CPURenderer: updateTextureRegion rectangle exceeds the texture bounds";
        return;
    }

    //! Repack RGBA8 rows into the ARGB8888 words the framebuffer samples.
    for (u32 row = 0; row < height; ++row)
    {
        const u8* src = rgbaPixels + static_cast<size_t>(row) * width * 4;
        u32* dst = tex.data.data() + static_cast<size_t>(y + row) * tex.width + x;

        for (u32 col = 0; col < width; ++col)
        {
            dst[col] = (static_cast<u32>(src[col * 4 + 3]) << 24)
                     | (static_cast<u32>(src[col * 4 + 0]) << 16)
                     | (static_cast<u32>(src[col * 4 + 1]) <<  8)
                     |  static_cast<u32>(src[col * 4 + 2]);
        }
    }
}

void CPURenderer::beginFrame()
{
    // Empty
}

void CPURenderer::beginRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    if (_frameBufferManager)
        _frameBufferManager->clear(_clearColorU32);
}

void CPURenderer::endRenderPass()
{
    /*
     * The frame's whole triangle queue is rasterised here, in one parallel
     * dispatch. This is the pass boundary in exactly the sense the GPU
     * backends use it -- the point past which no further geometry can arrive --
     * which is what makes it the right place to stop deferring. Present still
     * happens in endFrame().
     *
     * Which also makes EndPass the phase that owns essentially all of this
     * backend's cost: on the GPU backends the same scope closes a command
     * buffer, here it runs the rasteriser. Reading a phase breakdown across
     * backends means reading what each phase does on that backend, not
     * comparing the numbers directly.
     */
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (_frameBufferManager)
        _frameBufferManager->flush();
}

void CPURenderer::endFrame()
{
    {
        AURA_FRAME_SCOPE(FramePhase::Present);

        if (_frameBufferManager)
            _frameBufferManager->renderFramebuffer();
    }

    //! Braced above so the present scope has closed before the frame does.
    AURA_FRAME_END();
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

[[nodiscard]] glm::vec3 safeNormal(glm::vec3 value) noexcept
{
    const f32 length2 = glm::dot(value, value);
    return length2 > 0 ? value / std::sqrt(length2) : glm::vec3{0};
}

[[nodiscard]] ClipVertex transformVertex(const gfx::Vertex3D& v, const glm::mat4& mvp,
                                         const glm::mat3& normal, const gfx::LightUBO& light,
                                         glm::vec3 toLight) noexcept
{
    const f32 diffuse = std::max(glm::dot(safeNormal(normal * v.normal), toLight), 0.0f);
    const glm::vec3 color = glm::vec3(v.color) * glm::vec3(light.color) *
                           (light.ambient + diffuse * light.intensity);
    return {mvp * glm::vec4(v.pos, 1), v.texCoord, glm::vec4(color, v.color.a)};
}

} // anonymous namespace

const Texture* CPURenderer::_resolveTexture(TextureHandle handle) const
{
    // Handles are 1-based; anything else means "no texture".
    if (!isValidHandle(handle) || handle.value() > _texturePool.size())
        return nullptr;

    const auto& mips = _texturePool[handle.value() - 1];
    return mips.empty() ? nullptr : &mips[0];
}

void CPURenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (!_frameBufferManager)
        return;

    if (!isValidHandle(_boundVertexBuffer) || _boundVertexBuffer.value() > _vertexBufferPool3d.size())
        return;

    if (!isValidHandle(_boundIndexBuffer) || _boundIndexBuffer.value() > _indexBufferPool.size())
        return;

    const auto& verts = _vertexBufferPool3d[_boundVertexBuffer.value() - 1];
    const auto& indices = _indexBufferPool   [_boundIndexBuffer.value() - 1];

    const Texture* texture = _resolveTexture(_boundTexture);

    const float W = static_cast<float>(_frameBufferManager->getWidth());
    const float H = static_cast<float>(_frameBufferManager->getHeight());
    const glm::mat4 MVP = _currentTransform.proj * _currentTransform.view * _currentTransform.model;
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(_currentTransform.model)));

    const u32 safeCount = std::min(indexCount, static_cast<u32>(indices.size()));
    const u32 triCount  = safeCount / 3;

    if (++_drawStamp == 0)
    {
        for (auto& vertex : _transformed) vertex.stamp = 0;
        ++_drawStamp;
    }
    _transformed.resize(verts.size());
    const glm::vec3 toLight = safeNormal(-_light.direction);
    const auto vertex = [&](u32 index) -> const ClipVertex& {
        auto& cached = _transformed[index];
        if (cached.stamp != _drawStamp)
        {
            cached.vertex = transformVertex(verts[index], MVP, normalMatrix, _light, toLight);
            cached.stamp = _drawStamp;
        }
        return cached.vertex;
    };
    for (u32 t = 0; t < triCount; ++t)
    {
        const u32 i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;
        clipTriangle(vertex(i0), vertex(i1), vertex(i2), W, H,
            [&](const ScreenTriangle& triangle) { _frameBufferManager->queueTriangle(triangle, texture); });
    }

    (void)instanceCount;
}

void CPURenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (!_frameBufferManager)
        return;

    if (!isValidHandle(_boundVertexBuffer) || _boundVertexBuffer.value() > _vertexBufferPool3d.size())
        return;

    const auto& verts = _vertexBufferPool3d[_boundVertexBuffer.value() - 1];

    const Texture* texture = _resolveTexture(_boundTexture);

    const float W   = static_cast<float>(_frameBufferManager->getWidth());
    const float H   = static_cast<float>(_frameBufferManager->getHeight());
    const glm::mat4 MVP = _currentTransform.proj * _currentTransform.view * _currentTransform.model;
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(_currentTransform.model)));

    const u32 safeCount = std::min(vertexCount, static_cast<u32>(verts.size()));
    const u32 triCount  = safeCount / 3;

    const glm::vec3 toLight = safeNormal(-_light.direction);
    for (u32 t = 0; t < triCount; ++t)
    {
        const auto a = transformVertex(verts[t * 3], MVP, normalMatrix, _light, toLight);
        const auto b = transformVertex(verts[t * 3 + 1], MVP, normalMatrix, _light, toLight);
        const auto c = transformVertex(verts[t * 3 + 2], MVP, normalMatrix, _light, toLight);
        clipTriangle(a, b, c, W, H,
            [&](const ScreenTriangle& triangle) { _frameBufferManager->queueTriangle(triangle, texture); });
    }

    (void)instanceCount;
}

void CPURenderer::drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                              std::span<const u32> indices,
                              TextureHandle texture)
{
    AURA_FRAME_SCOPE(FramePhase::RecordOverlay);

    if (!_frameBufferManager || vertices.empty() || indices.empty())
        return;

    const Texture* sampled = _resolveTexture(texture);

    /*
     * No projection matrix is needed here: the batch already arrives in window
     * pixels, which is precisely the software rasteriser's own coordinate space.
     * What the GPU backends express as an orthographic matrix is, on this path,
     * simply the absence of a transform.
     */
    const auto toScreenVertex = [](const gfx::Vertex2D& v) noexcept {
        ScreenVertex sv;
        sv.x = v.pos.x;
        sv.y = v.pos.y;
        sv.z = 0.0f;
        sv.invW = 1.0f; //! Orthographic: no perspective division.
        sv.uv = v.texCoord;
        sv.color = v.color;
        return sv;
    };

    const size_t triCount = indices.size() / 3;

    for (size_t t = 0; t < triCount; ++t)
    {
        const u32 i0 = indices[t * 3 + 0];
        const u32 i1 = indices[t * 3 + 1];
        const u32 i2 = indices[t * 3 + 2];

        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;

        /*
         * No back-face test here, matching the GPU overlay pipelines, which
         * disable culling outright (see overlayOptions.cullBackFaces). UI
         * quads carry no meaningful winding and a glyph must draw whichever
         * way its two triangles happen to be wound.
         */
        _frameBufferManager->queueTriangle({toScreenVertex(vertices[i0]),
                                       toScreenVertex(vertices[i1]),
                                       toScreenVertex(vertices[i2])}, sampled, true);
    }

}

} // namespace cpu
} // namespace aura3d
