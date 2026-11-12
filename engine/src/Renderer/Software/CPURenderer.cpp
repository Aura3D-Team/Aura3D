#include "aura/Renderer/Software/CPURenderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#include "aura/Core/AuraMath.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/JobSystem/JobSystem.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/aura.h"

namespace aura3d
{
namespace cpu
{

namespace
{

[[nodiscard]] bool validTextureSize(u32 width, u32 height, usize bytesPerPixel) noexcept
{
    return width > 0 && height > 0 && width <= static_cast<u32>(std::numeric_limits<i32>::max()) &&
           height <= static_cast<u32>(std::numeric_limits<i32>::max()) &&
           static_cast<usize>(width) <= std::numeric_limits<usize>::max() / height / bytesPerPixel;
}

//! sRGB byte to linear byte, rounded. Built on first use: std::pow cannot run at compile time.
const std::array<u8, 256> &srgbToLinear()
{
    static const std::array<u8, 256> table = []
    {
        std::array<u8, 256> table{};
        for (usize i = 0; i < table.size(); ++i)
        {
            const f32 c = static_cast<f32>(i) / 255.0f;
            const f32 linear = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            table[i] = static_cast<u8>(std::lround(linear * 255.0f));
        }
        return table;
    }();
    return table;
}

[[nodiscard]] u32 linearByte(f32 value) noexcept
{
    //! NaN fails the comparison and lands on 0.
    return value > 0.0f ? static_cast<u32>(std::lround(std::min(value, 1.0f) * 255.0f)) : 0u;
}

//! One level down, box-filtered: each output texel averages the 2x2 block it covers, clamped at an odd edge.
[[nodiscard]] Texture halve(const Texture &source)
{
    Texture level(std::max(source.width / 2, 1), std::max(source.height / 2, 1));
    level.wrapU = source.wrapU;
    level.wrapV = source.wrapV;
    level.nearest = source.nearest;
    for (i32 y = 0; y < level.height; ++y)
        for (i32 x = 0; x < level.width; ++x)
        {
            u32 sum[4]{};
            for (i32 dy = 0; dy < 2; ++dy)
                for (i32 dx = 0; dx < 2; ++dx)
                {
                    const u32 texel = source.texelClamped(x * 2 + dx, y * 2 + dy);
                    for (u32 c = 0; c < 4; ++c)
                        sum[c] += (texel >> (c * 8)) & 0xFFu;
                }
            u32 packed = 0;
            for (u32 c = 0; c < 4; ++c)
                packed |= ((sum[c] + 2) / 4) << (c * 8);
            level.data[static_cast<size_t>(y) * static_cast<size_t>(level.width) + static_cast<size_t>(x)] = packed;
        }
    return level;
}

} // namespace

CPURenderer::CPURenderer(const wma::WindowDetails &windowDetails) : IRenderer(windowDetails)
{
    INK_INFO << "Renderer - SOFTWARE";
}

CPURenderer::~CPURenderer()
{
    cleanup();
}

void CPURenderer::initialize(aura3d::AuraSettings *settings, const JobSystem *jobs)
{
    _vertexBufferPool3d.reserve(256);
    _indexBufferPool.reserve(256);
    _texturePool.reserve(64);

    //! Stored before createWindow(): it builds _frameBufferManager, which
    //! dispatches both rasterisation and presentation through this pool
    //! instead of owning one of its own (see CpuFrameBufferManager's
    //! constructor comment).
    _jobs = jobs;

    createWindow(settings->windowTitle().c_str(), settings->windowBackend());
}

void CPURenderer::createWindow(const char *title, const wma::WindowBackend &wBackend)
{
    _windowManagerApi = makeWindow(wBackend, _windowDetails, wma::GraphicsAPI::CPU);
    _windowManagerApi->createWindow(title);

    CpuFrameBufferManager::Config cfg;
    cfg.width = _windowDetails.width;
    cfg.height = _windowDetails.height;
    cfg.useDepthBuffer = true;

    _frameBufferManager = std::make_unique<CpuFrameBufferManager>(*_windowManagerApi, cfg, *_jobs);
    _activeFrame = _frameBufferManager.get();

    INK_INFO << "CPURenderer: rasterising across " << _frameBufferManager->getWorkerCount()
             << " worker thread(s) (shared engine pool)";
}

void CPURenderer::handleWindowChanges()
{
    if (!_frameBufferManager)
        return;

    //! Pixels, not logical units: the surface lockFramebuffer() returns is a HiDPI window's backing store.
    const wma::FramebufferSize size = _windowManagerApi->getFramebufferSize();
    _frameBufferManager->resizeFramebuffer(size.width, size.height);
}

void CPURenderer::cleanup()
{
    clearSharedResources();
    _activeFrame = nullptr;
    _passTarget = 0;
    _renderTargets.clear();
    _frameBufferManager.reset();
    _windowManagerApi.reset();
    _vertexBufferPool3d.clear();
    _indexBufferPool.clear();
    _texturePool.clear();
}

//! Pools are indexed 0-based but handles are 1-based, so that no valid handle
//! collides with the invalid-handle sentinel, and 0 stays reserved as "nothing bound".
VertexBufferHandle CPURenderer::createVertexBuffer(std::vector<gfx::Vertex3D> &&vertices)
{
    _vertexBufferPool3d.push_back(std::move(vertices));
    return static_cast<VertexBufferHandle>(_vertexBufferPool3d.size());
}

bool CPURenderer::updateVertexBuffer(VertexBufferHandle handle, std::span<const gfx::Vertex3D> vertices)
{
    if (!isValidHandle(handle) || handle.value() > _vertexBufferPool3d.size())
        return false;
    std::vector<gfx::Vertex3D> &stored = _vertexBufferPool3d[handle.value() - 1];
    if (vertices.size() > stored.size())
        return false;
    std::ranges::copy(vertices, stored.begin());
    return true;
}

IndexBufferHandle CPURenderer::createIndexBuffer(std::vector<u16> &&indices)
{
    std::vector<u32> up;
    up.reserve(indices.size());
    for (u16 i : indices)
        up.push_back(static_cast<u32>(i));
    _indexBufferPool.push_back(std::move(up));
    return static_cast<IndexBufferHandle>(_indexBufferPool.size());
}

IndexBufferHandle CPURenderer::createIndexBuffer(std::vector<u32> &&indices)
{
    _indexBufferPool.push_back(std::move(indices));
    return static_cast<IndexBufferHandle>(_indexBufferPool.size());
}

TextureHandle CPURenderer::createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height)
{
    if (!rgbaPixels || !validTextureSize(width, height, 4))
    {
        INK_ERROR << "CPURenderer: refusing invalid texture data or dimensions";
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

        tex.data[i] = (static_cast<u32>(a) << 24) | (static_cast<u32>(r) << 16) | (static_cast<u32>(g) << 8) |
                      static_cast<u32>(b);
    }

    //! Textures are stored as mip-level vectors.
    //! Handle is 1-based so that no valid handle collides with the invalid-handle sentinel.
    _texturePool.emplace_back().push_back(std::move(tex));
    return static_cast<TextureHandle>(_texturePool.size()); // 1-based
}

TextureHandle CPURenderer::createTexture(const TextureDesc &desc, std::span<const std::byte> pixels)
{
    if (desc.format == TextureFormat::Depth)
    {
        INK_ERROR << "CPURenderer: TextureFormat::Depth is for render targets, and this backend has no depth targets";
        return {};
    }
    const u32 texelBytes = bytesPerTexel(desc.format);
    if (!validTextureSize(desc.width, desc.height, texelBytes) ||
        pixels.size() != static_cast<size_t>(desc.width) * desc.height * texelBytes)
    {
        INK_ERROR << "CPURenderer: createTexture pixels do not match the descriptor's size and format";
        return {};
    }

    //! shortcut: texels are stored as linear 8-bit, so sRGB loses dark precision and half floats clamp to
    //! [0, 1]; store float texels if the software path ever has to show HDR or banding-free gradients.
    Texture base(static_cast<i32>(desc.width), static_cast<i32>(desc.height));
    base.wrapU = desc.sampler.wrapU;
    base.wrapV = desc.sampler.wrapV;
    base.nearest = desc.sampler.magFilter == TextureFilter::Nearest;
    const auto *bytes = reinterpret_cast<const u8 *>(pixels.data());
    for (size_t i = 0; i < base.data.size(); ++i)
    {
        u32 r = 0, g = 0, b = 0, a = 0;
        if (desc.format == TextureFormat::Rgba16Float)
        {
            std::array<u16, 4> halves{};
            std::memcpy(halves.data(), bytes + i * 8, sizeof(halves));
            r = linearByte(halfToFloat(halves[0]));
            g = linearByte(halfToFloat(halves[1]));
            b = linearByte(halfToFloat(halves[2]));
            a = linearByte(halfToFloat(halves[3]));
        }
        else
        {
            const u8 *texel = bytes + i * 4;
            const bool srgb = desc.format == TextureFormat::Rgba8Srgb;
            r = srgb ? srgbToLinear()[texel[0]] : texel[0];
            g = srgb ? srgbToLinear()[texel[1]] : texel[1];
            b = srgb ? srgbToLinear()[texel[2]] : texel[2];
            a = texel[3];
        }
        base.data[i] = (a << 24u) | (r << 16u) | (g << 8u) | b;
    }

    auto &chain = _texturePool.emplace_back();
    const u32 levels = desc.mipmaps ? mipLevelCount(desc.width, desc.height) : 1u;
    chain.reserve(levels);
    chain.push_back(std::move(base));
    while (chain.size() < levels)
        chain.push_back(halve(chain.back()));
    chain.front().levels = levels;
    return static_cast<TextureHandle>(_texturePool.size());
}

TextureHandle CPURenderer::createDynamicTexture(u32 width, u32 height)
{
    if (!validTextureSize(width, height, 4))
    {
        INK_ERROR << "CPURenderer: refusing invalid texture dimensions";
        return {};
    }

    //! Texture's constructor zero-fills, i.e. transparent black.
    Texture tex(static_cast<int>(width), static_cast<int>(height));
    _texturePool.emplace_back().push_back(std::move(tex));
    return static_cast<TextureHandle>(_texturePool.size()); // 1-based
}

TextureHandle CPURenderer::createCoverageTexture(u32 width, u32 height)
{
    if (!validTextureSize(width, height, 1))
    {
        INK_ERROR << "CPURenderer: refusing invalid coverage texture dimensions";
        return {};
    }

    Texture tex(static_cast<i32>(width), static_cast<i32>(height), true);
    _texturePool.emplace_back().push_back(std::move(tex));
    return static_cast<TextureHandle>(_texturePool.size());
}

void CPURenderer::updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                              const u8 *coverage)
{
    if (!coverage || width == 0 || height == 0)
        return;
    if (!isValidHandle(handle) || handle.value() > _texturePool.size())
    {
        INK_ERROR << "CPURenderer: coverage update on an unknown texture";
        return;
    }

    auto &mips = _texturePool[handle.value() - 1];
    if (mips.empty())
        return;
    Texture &tex = mips[0];
    if (tex.coverage.empty() ||
        !regionFits(x, y, width, height, static_cast<u32>(tex.width), static_cast<u32>(tex.height)))
    {
        INK_ERROR << "CPURenderer: invalid coverage texture update";
        return;
    }

    for (u32 row = 0; row < height; ++row)
        std::memcpy(tex.coverage.data() + static_cast<size_t>(y + row) * tex.width + x,
                    coverage + static_cast<size_t>(row) * width, width);
}

void CPURenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels)
{
    if (!rgbaPixels || width == 0 || height == 0)
        return;

    if (!isValidHandle(handle) || handle.value() > _texturePool.size())
    {
        INK_ERROR << "CPURenderer: updateTextureRegion on an unknown texture";
        return;
    }

    auto &mips = _texturePool[handle.value() - 1];
    if (mips.empty())
        return;

    Texture &tex = mips[0];
    if (!tex.coverage.empty() || tex.levels > 1)
    {
        INK_ERROR << "CPURenderer: RGBA update on a coverage or mipmapped texture";
        return;
    }
    if (!regionFits(x, y, width, height, static_cast<u32>(tex.width), static_cast<u32>(tex.height)))
    {
        INK_ERROR << "CPURenderer: updateTextureRegion rectangle exceeds the texture bounds";
        return;
    }

    //! Repack RGBA8 rows into the ARGB8888 words the framebuffer samples.
    for (u32 row = 0; row < height; ++row)
    {
        const u8 *src = rgbaPixels + static_cast<size_t>(row) * width * 4;
        u32 *dst = tex.data.data() + static_cast<size_t>(y + row) * tex.width + x;

        for (u32 col = 0; col < width; ++col, src += 4)
            dst[col] = (static_cast<u32>(src[3]) << 24) | (static_cast<u32>(src[0]) << 16) |
                       (static_cast<u32>(src[1]) << 8) | static_cast<u32>(src[2]);
    }
}

bool CPURenderer::acquireFrame()
{
    //! Every window backend reallocates its surface on resize; the planes follow here, before the frame records.
    if (consumeResize())
        handleWindowChanges();
    return _frameBufferManager != nullptr;
}

void CPURenderer::openRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    _activeFrame = _frameBufferManager.get();
    _passTarget = 0;
    if (_activeFrame)
        _activeFrame->clear(_clearColorU32);
}

void CPURenderer::openRenderPass(RenderTargetHandle target)
{
    if (!isValidHandle(target))
        return openRenderPass();
    AURA_FRAME_SCOPE(FramePhase::BeginPass);
    if (target.value() > _renderTargets.size())
        return;
    _activeFrame = _renderTargets[target.value() - 1].frame.get();
    _passTarget = target.value();
    _activeFrame->clear(_clearColorU32);
}

RendererCaps CPURenderer::caps() const noexcept
{
    return RendererCaps::RenderTargets | RendererCaps::Compute;
}

std::vector<std::byte> *CPURenderer::bufferOf(BufferHandle handle) noexcept
{
    return isValidHandle(handle) && handle.value() <= _buffers.size() ? &_buffers[handle.value() - 1] : nullptr;
}

BufferHandle CPURenderer::createBuffer(usize bytes, std::span<const std::byte> initial)
{
    if (bytes == 0 || initial.size() > bytes)
        return {};
    std::vector<std::byte> &buffer = _buffers.emplace_back(bytes, std::byte{0});
    std::ranges::copy(initial, buffer.begin());
    return BufferHandle{static_cast<u32>(_buffers.size())};
}

bool CPURenderer::updateBuffer(BufferHandle handle, usize offset, std::span<const std::byte> bytes)
{
    std::vector<std::byte> *buffer = bufferOf(handle);
    if (!buffer || offset > buffer->size() || bytes.size() > buffer->size() - offset)
        return false;
    std::ranges::copy(bytes, buffer->begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

bool CPURenderer::readBuffer(BufferHandle handle, usize offset, std::span<std::byte> out)
{
    std::vector<std::byte> *buffer = bufferOf(handle);
    if (!buffer || offset > buffer->size() || out.size() > buffer->size() - offset)
        return false;
    std::copy_n(buffer->begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
    return true;
}

ComputeHandle CPURenderer::createCompute(const ComputeDesc &desc)
{
    if (!desc.cpu)
    {
        INK_ERROR << "CPURenderer: a compute kernel needs ComputeDesc::cpu to run on the software backend";
        return {};
    }
    _kernels.push_back({.buffers = desc.buffers, .cpu = desc.cpu});
    return ComputeHandle{static_cast<u32>(_kernels.size())};
}

void CPURenderer::dispatch(ComputeHandle kernel, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                           std::span<const std::byte> constants)
{
    if (!isValidHandle(kernel) || kernel.value() > _kernels.size() || !_jobs)
        return;
    const ComputeDesc &desc = _kernels[kernel.value() - 1];
    std::vector<std::span<std::byte>> bound;
    for (const BufferHandle handle : buffers)
    {
        std::vector<std::byte> *buffer = bufferOf(handle);
        if (!buffer)
            return;
        bound.emplace_back(*buffer);
    }
    if (bound.size() < desc.buffers)
        return;
    const u64 total = u64{groups.x} * groups.y * groups.z;
    if (total == 0 || total > static_cast<u64>(std::numeric_limits<i32>::max()))
        return;
    _jobs->dispatch(static_cast<i32>(total),
                    [&](i32 begin, i32 end)
                    {
                        for (auto g = static_cast<u32>(begin); g < static_cast<u32>(end); ++g)
                            desc.cpu(
                                {bound, {g % groups.x, g / groups.x % groups.y, g / (groups.x * groups.y)}, constants});
                    });
}

void CPURenderer::drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                            BufferHandle arguments, usize argumentOffset)
{
    DrawIndexedIndirect draw;
    if (!readBuffer(arguments, argumentOffset, std::as_writable_bytes(std::span(&draw, 1))) || draw.indexCount == 0)
        return;
    const std::vector<std::byte> *source = bufferOf(instances);
    const usize available = source ? source->size() / sizeof(InstanceTransform) : 0;
    if (draw.firstInstance >= available)
        return;
    const usize count = std::min<usize>(draw.instanceCount, available - draw.firstInstance);
    std::vector<glm::mat4> models(count);
    for (usize i = 0; i < count; ++i)
        std::memcpy(&models[i], source->data() + (draw.firstInstance + i) * sizeof(InstanceTransform),
                    sizeof(glm::mat4));
    drawMeshInstanced(mesh, material, models);
}

RenderTargetHandle CPURenderer::createRenderTarget(const RenderTargetDesc &desc)
{
    if (desc.format == TextureFormat::Depth)
    {
        INK_ERROR << "CPURenderer: depth-only render targets are not supported";
        return {};
    }
    if (!_jobs || !validTextureSize(desc.width, desc.height, 4))
    {
        INK_ERROR << "CPURenderer: invalid render target dimensions";
        return {};
    }
    RenderTarget target;
    CpuFrameBufferManager::Config config;
    config.width = static_cast<i32>(desc.width);
    config.height = static_cast<i32>(desc.height);
    config.useDepthBuffer = desc.depth;
    target.frame = std::make_unique<CpuFrameBufferManager>(config, *_jobs);
    target.srgb = desc.format == TextureFormat::Rgba8Srgb;
    target.depth = desc.depth;
    _texturePool.emplace_back().emplace_back(config.width, config.height);
    target.colour = static_cast<TextureHandle>(_texturePool.size());
    _renderTargets.push_back(std::move(target));
    return RenderTargetHandle{static_cast<u32>(_renderTargets.size())};
}

bool CPURenderer::resizeRenderTarget(RenderTargetHandle handle, u32 width, u32 height)
{
    if (!_jobs || !isValidHandle(handle) || handle.value() > _renderTargets.size() ||
        !validTextureSize(width, height, 4))
        return false;
    RenderTarget &target = _renderTargets[handle.value() - 1];
    CpuFrameBufferManager::Config config;
    config.width = static_cast<i32>(width);
    config.height = static_cast<i32>(height);
    config.useDepthBuffer = target.depth;
    if (_activeFrame == target.frame.get())
        return false;
    target.frame = std::make_unique<CpuFrameBufferManager>(config, *_jobs);
    _texturePool[target.colour.value() - 1] = {Texture(config.width, config.height)};
    return true;
}

glm::uvec2 CPURenderer::targetSize(RenderTargetHandle target) const
{
    if (!isValidHandle(target) || target.value() > _renderTargets.size())
        return glm::uvec2{0};
    const Texture &colour = _texturePool[_renderTargets[target.value() - 1].colour.value() - 1].front();
    return {static_cast<u32>(colour.width), static_cast<u32>(colour.height)};
}

TextureHandle CPURenderer::renderTargetTexture(RenderTargetHandle target) const
{
    return isValidHandle(target) && target.value() <= _renderTargets.size() ? _renderTargets[target.value() - 1].colour
                                                                            : TextureHandle{};
}

bool CPURenderer::readPixels(RenderTargetHandle target, std::span<u8> rgba)
{
    if (!isValidHandle(target) || target.value() > _renderTargets.size())
        return false;
    RenderTarget &chosen = _renderTargets[target.value() - 1];
    std::vector<u32> words(rgba.size() / 4);
    if (rgba.size() % 4 != 0 ||
        words.size() != static_cast<size_t>(chosen.frame->getWidth()) * static_cast<size_t>(chosen.frame->getHeight()))
        return false;
    chosen.frame->readColor(words);
    //! The plane holds sRGB-encoded words; a linear target reports what the GPUs store, the decoded value.
    for (size_t i = 0; i < words.size(); ++i)
    {
        const u32 w = words[i];
        const auto channel = [&](u32 shift)
        {
            const auto byte = static_cast<u8>((w >> shift) & 0xFFu);
            return chosen.srgb ? byte : srgbToLinear()[byte];
        };
        rgba[i * 4 + 0] = channel(16);
        rgba[i * 4 + 1] = channel(8);
        rgba[i * 4 + 2] = channel(0);
        rgba[i * 4 + 3] = static_cast<u8>(w >> 24u);
    }
    return true;
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

    if (!_activeFrame)
        return;
    _activeFrame->flush();
    if (_passTarget != 0)
    {
        //! Into the target's texture, as the linear words this backend samples.
        RenderTarget &target = _renderTargets[_passTarget - 1];
        Texture &texture = _texturePool[target.colour.value() - 1][0];
        target.frame->readColor({texture.data.data(), texture.data.size()});
        const std::array<u8, 256> &decode = srgbToLinear();
        for (u32 &w : texture.data)
            w = (w & 0xFF000000u) | (static_cast<u32>(decode[(w >> 16u) & 0xFFu]) << 16u) |
                (static_cast<u32>(decode[(w >> 8u) & 0xFFu]) << 8u) | decode[w & 0xFFu];
    }
    _activeFrame = _frameBufferManager.get();
    _passTarget = 0;
}

bool CPURenderer::presentFrame()
{
    bool presented = false;
    {
        AURA_FRAME_SCOPE(FramePhase::Present);
        //! False when there was no surface to copy into: that frame never reached the screen.
        presented = _frameBufferManager && _frameBufferManager->renderFramebuffer();
        //! On a vsynced window this is what holds the loop to the display, so it is part of the present.
        if (presented && _windowManagerApi)
            _windowManagerApi->swapBuffers();
    }

    //! Braced above so the present scope has closed before the frame does.
    AURA_FRAME_END();
    return presented;
}

void CPURenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    //! Linear, like the GPU backends' clear values, which their sRGB targets encode.
    _clearColorU32 = CpuFrameBufferManager::packLinearColor({r, g, b, a});
}

namespace
{

[[nodiscard]] glm::vec3 safeNormal(glm::vec3 value) noexcept
{
    const f32 length2 = glm::dot(value, value);
    return length2 > 0 ? value / std::sqrt(length2) : glm::vec3{0};
}

//! Lambert from the directional lights plus the ambient ones; point and spot lights need RendererCaps::Pbr.
struct VertexLighting
{
    std::array<glm::vec3, gfx::kMaxLights> toLight{};
    std::array<glm::vec3, gfx::kMaxLights> radiance{};
    usize directional = 0;
    glm::vec3 ambient{0.0f};

    explicit VertexLighting(std::span<const gfx::Light> lights) noexcept
    {
        for (const gfx::Light &light : lights)
            if (light.type == gfx::LightType::Ambient)
                ambient += light.color * light.intensity;
            else if (light.type == gfx::LightType::Directional)
            {
                toLight[directional] = safeNormal(-light.direction);
                radiance[directional++] = light.color * light.intensity;
            }
    }
};

[[nodiscard]] ClipVertex transformVertex(const gfx::Vertex3D &v, const glm::mat4 &mvp, const glm::mat3 &normal,
                                         const VertexLighting &lighting) noexcept
{
    const glm::vec3 n = safeNormal(normal * v.normal);
    glm::vec3 light = lighting.ambient;
    for (usize i = 0; i < lighting.directional; ++i)
        light += lighting.radiance[i] * std::max(glm::dot(n, lighting.toLight[i]), 0.0f);
    return {mvp * glm::vec4(v.pos, 1), v.texCoord, glm::vec4(glm::vec3(v.color) * light, v.color.a)};
}

} // anonymous namespace

const Texture *CPURenderer::_resolveTexture(TextureHandle handle) const
{
    // Handles are 1-based; anything else means "no texture".
    if (!isValidHandle(handle) || handle.value() > _texturePool.size())
        return nullptr;

    const auto &mips = _texturePool[handle.value() - 1];
    return mips.empty() ? nullptr : &mips[0];
}

void CPURenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (!_activeFrame)
        return;

    if (!isValidHandle(_currentVertexBuffer) || _currentVertexBuffer.value() > _vertexBufferPool3d.size())
        return;

    if (!isValidHandle(_currentIndexBuffer) || _currentIndexBuffer.value() > _indexBufferPool.size())
        return;

    const auto &verts = _vertexBufferPool3d[_currentVertexBuffer.value() - 1];
    const auto &indices = _indexBufferPool[_currentIndexBuffer.value() - 1];

    const Texture *texture = _resolveTexture(_currentTexture);

    const float W = static_cast<float>(_activeFrame->getWidth());
    const float H = static_cast<float>(_activeFrame->getHeight());
    const glm::mat4 MVP = _currentTransform.proj * _currentTransform.view * _currentTransform.model;
    const glm::mat3 normalMatrix = gfx::normalMatrixOf(_currentTransform.model);

    const u32 safeCount = std::min(indexCount, static_cast<u32>(indices.size()));
    const u32 triCount = safeCount / 3;

    if (++_drawStamp == 0)
    {
        for (auto &vertex : _transformed)
            vertex.stamp = 0;
        ++_drawStamp;
    }
    // Keep constructed entries across smaller draws; the stamp invalidates
    // their contents without reinitializing an entire vertex buffer.
    if (_transformed.size() < verts.size())
        _transformed.resize(verts.size());
    const VertexLighting lighting(_lights);
    const auto vertex = [&](u32 index) -> const ClipVertex &
    {
        auto &cached = _transformed[index];
        if (cached.stamp != _drawStamp)
        {
            cached.vertex = transformVertex(verts[index], MVP, normalMatrix, lighting);
            cached.stamp = _drawStamp;
        }
        return cached.vertex;
    };
    for (usize t = 0; t < triCount; ++t)
    {
        const u32 i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
            continue;
        clipTriangle(vertex(i0), vertex(i1), vertex(i2), W, H,
                     [&](const ScreenTriangle &triangle)
                     {
                         _activeFrame->queueTriangle(triangle, texture);
                     });
    }

    (void)instanceCount;
}

void CPURenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (!_activeFrame)
        return;

    if (!isValidHandle(_currentVertexBuffer) || _currentVertexBuffer.value() > _vertexBufferPool3d.size())
        return;

    const auto &verts = _vertexBufferPool3d[_currentVertexBuffer.value() - 1];

    const Texture *texture = _resolveTexture(_currentTexture);

    const float W = static_cast<float>(_activeFrame->getWidth());
    const float H = static_cast<float>(_activeFrame->getHeight());
    const glm::mat4 MVP = _currentTransform.proj * _currentTransform.view * _currentTransform.model;
    const glm::mat3 normalMatrix = gfx::normalMatrixOf(_currentTransform.model);

    const u32 safeCount = std::min(vertexCount, static_cast<u32>(verts.size()));
    const u32 triCount = safeCount / 3;

    const VertexLighting lighting(_lights);
    for (usize t = 0; t < triCount; ++t)
    {
        const auto a = transformVertex(verts[t * 3], MVP, normalMatrix, lighting);
        const auto b = transformVertex(verts[t * 3 + 1], MVP, normalMatrix, lighting);
        const auto c = transformVertex(verts[t * 3 + 2], MVP, normalMatrix, lighting);
        clipTriangle(a, b, c, W, H,
                     [&](const ScreenTriangle &triangle)
                     {
                         _activeFrame->queueTriangle(triangle, texture);
                     });
    }

    (void)instanceCount;
}

glm::uvec2 CPURenderer::renderTargetSize() const noexcept
{
    if (!_activeFrame)
        return glm::uvec2{0};
    return {static_cast<u32>(std::max(_activeFrame->getWidth(), 0)),
            static_cast<u32>(std::max(_activeFrame->getHeight(), 0))};
}

void CPURenderer::drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                            TextureHandle texture, gfx::BatchSpace space)
{
    AURA_FRAME_SCOPE(space == gfx::BatchSpace::Screen ? FramePhase::RecordOverlay : FramePhase::RecordScene);
    if (!_activeFrame || vertices.empty() || indices.empty())
        return;

    const Texture *sampled = _resolveTexture(texture);
    // Screen batches already use the rasterizer's pixel and depth coordinates.
    if (space == gfx::BatchSpace::Screen)
    {
        _activeFrame->queueScreenTriangles(vertices, indices, sampled);
        return;
    }

    const f32 width = static_cast<f32>(_activeFrame->getWidth());
    const f32 height = static_cast<f32>(_activeFrame->getHeight());
    const glm::mat4 transform = batchTransform(space);
    const auto emit = [&](const ScreenTriangle &triangle)
    {
        _activeFrame->queueTriangle(triangle, sampled, CpuFrameBufferManager::RasterMode::Batch);
    };
    const auto toClip = [&](const gfx::BatchVertex &v) -> ClipVertex
    {
        return {transform * glm::vec4(v.pos, 1), v.texCoord, v.color};
    };

    for (usize t = 0; t + 2 < indices.size(); t += 3)
    {
        const u32 i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;
        clipTriangle(toClip(vertices[i0]), toClip(vertices[i1]), toClip(vertices[i2]), width, height, emit, false);
    }
}

} // namespace cpu
} // namespace aura3d
