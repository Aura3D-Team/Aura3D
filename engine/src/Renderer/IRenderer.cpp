#include "aura/Renderer/IRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/Files.h"
#include "aura/Core/ImageLoader/ImageLoader.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/Renderer/RendererFactory.h"
#include "aura/aura.h"

namespace aura3d
{

namespace
{

[[nodiscard]] bool depthZeroToOne(RendererChoice backend) noexcept
{
    return RendererFactory::clipSpaceFor(backend) == Camera::ClipSpace::Vulkan;
}

} // namespace

std::unique_ptr<wma::IWindowManager> IRenderer::makeWindow(wma::WindowBackend backend,
                                                           const wma::WindowDetails &details, wma::GraphicsAPI api)
{
    if (_windowFactory)
        return _windowFactory(backend, details, api);

    const wma::WindowBackend usable = wma::selectUsableBackend(backend);
    if (usable != backend)
        INK_WARN << "[Aura3D] window backend " << WindowBackendToString(backend)
                 << " cannot run on this machine (a system library is missing); using "
                 << WindowBackendToString(usable);
    return wma::createWindowManager(usable, details, api);
}

void IRenderer::setTransform(const gfx::TransformUBO &ubo)
{
    _currentTransform = ubo;
}

void IRenderer::bindVertexBuffer(VertexBufferHandle handle)
{
    _currentVertexBuffer = handle;
}

void IRenderer::bindIndexBuffer(IndexBufferHandle handle)
{
    _currentIndexBuffer = handle;
}

void IRenderer::bindTexture(TextureHandle handle)
{
    _currentTexture = handle;
}

void IRenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    _clearR = r;
    _clearG = g;
    _clearB = b;
    _clearA = a;
}

wma::IWindowManager *IRenderer::getWindowManager()
{
    return _windowManagerApi.get();
}

bool IRenderer::consumeResize() noexcept
{
    const wma::WindowFlags *flags = _windowManagerApi ? _windowManagerApi->getWindowFlags() : nullptr;
    return flags && _resizeWatch.consume(*flags);
}

bool IRenderer::resizePending() const noexcept
{
    const wma::WindowFlags *flags = _windowManagerApi ? _windowManagerApi->getWindowFlags() : nullptr;
    return flags && _resizeWatch.pending(*flags);
}

void IRenderer::drawBatch(const gfx::Canvas &canvas, TextureHandle texture, gfx::BatchSpace space)
{
    if (!canvas.empty())
        drawBatch(canvas.vertices(), canvas.indices(), texture, space);
}

glm::uvec2 IRenderer::renderTargetSize() const noexcept
{
    return {static_cast<u32>(std::max(_windowDetails.width, 0)), static_cast<u32>(std::max(_windowDetails.height, 0))};
}

gfx::CanvasView IRenderer::canvasView() const
{
    return {batchTransform(gfx::BatchSpace::World), glm::vec2{renderTargetSize()}, depthZeroToOne(getBackendType())};
}

glm::mat4 IRenderer::batchTransform(gfx::BatchSpace space) const
{
    if (space == gfx::BatchSpace::World)
        return _currentTransform.proj * _currentTransform.view * _currentTransform.model;

    //! Pixels with y down and depth in [0, 1], to the y-up clip space every backend shares
    //! (Vulkan's viewport is flipped to match) in this backend's depth range.
    const glm::vec2 size{renderTargetSize()};
    if (size.x <= 0.0f || size.y <= 0.0f)
        return glm::mat4{0.0f};
    const bool zeroToOne = depthZeroToOne(getBackendType());
    return {{2.0f / size.x, 0.0f, 0.0f, 0.0f},
            {0.0f, -2.0f / size.y, 0.0f, 0.0f},
            {0.0f, 0.0f, zeroToOne ? 1.0f : 2.0f, 0.0f},
            {-1.0f, 1.0f, zeroToOne ? 0.0f : -1.0f, 1.0f}};
}

void IRenderer::run(move_only_function<void()> onFrame)
{
    auto *windowMgr = getWindowManager();
    if (!windowMgr)
        return;

    _running = true;
    const AuraSettings *settings = AuraSettings::get();
    const std::filesystem::path shaders = settings->get(setting::pathShaders);
    //! Shared because process() may copy its callable; only watched when asked to, so release builds never stat.
    auto watcher = settings->get(setting::rendererShaderHotReload) ? std::make_shared<FileWatcher>(shaders) : nullptr;
    windowMgr->process(
        [this, watcher, shaders, onFrame = std::make_shared<move_only_function<void()>>(std::move(onFrame))]()
        {
            if (watcher && watcher->changed())
                INK_INFO << "Shaders under " << shaders.string()
                         << (reloadShaders(shaders) ? " reloaded" : " changed; some failed to reload (see above)");
            beginFrame();
            (*onFrame)();
            endFrame();
        });
    _running = false;
}

/*
 * Everything below is backend-independent: decoding an image, pairing a
 * vertex/index buffer into a mesh and tracking materials look identical for
 * Vulkan, OpenGL and the software rasteriser. They are implemented once here on
 * top of each backend's primitives (createTextureFromPixels, createVertexBuffer,
 * bindTexture, ...) instead of being repeated three times. Backends may still
 * override any of them where they can do better.
 */

bool IRenderer::reloadShaders(const std::filesystem::path &)
{
    return false;
}

TextureHandle IRenderer::createTextureFromFile(const std::string &path)
{
    ImageData image = ImageLoader::loadRGBA(path);

    if (!image.valid())
    {
        INK_WARN << "createTextureFromFile: '" << path << "' unavailable; substituting the checkerboard fallback";
        image = ImageLoader::makeCheckerboard();
    }

    return createTextureFromPixels(image.pixels.data(), image.width, image.height);
}

TextureHandle IRenderer::createTextureFromFile(const std::string &path, TextureDesc desc)
{
    if (desc.format == TextureFormat::Rgba16Float || desc.format == TextureFormat::Depth)
    {
        INK_ERROR << "createTextureFromFile: '" << path << "' decodes to 8 bits; "
                  << (desc.format == TextureFormat::Depth ? "Depth is for render targets"
                                                          : "Rgba16Float needs its own pixels");
        return {};
    }
    ImageData image = ImageLoader::loadRGBA(path);
    if (!image.valid())
    {
        INK_WARN << "createTextureFromFile: '" << path << "' unavailable; substituting the checkerboard fallback";
        image = ImageLoader::makeCheckerboard();
    }
    desc.width = static_cast<u32>(image.width);
    desc.height = static_cast<u32>(image.height);
    return createTexture(desc, std::as_bytes(std::span(image.pixels)));
}

TextureHandle IRenderer::createTexture(const TextureDesc &desc, std::span<const std::byte> pixels)
{
    //! For renderers that predate TextureDesc: the pixels arrive, the format and sampler do not.
    if (desc.format == TextureFormat::Rgba16Float || desc.format == TextureFormat::Depth ||
        pixels.size() != static_cast<usize>(desc.width) * desc.height * bytesPerTexel(desc.format))
    {
        INK_ERROR << "createTexture: this renderer takes RGBA8 pixels of exactly width * height * 4 bytes";
        return {};
    }
    return createTextureFromPixels(reinterpret_cast<const u8 *>(pixels.data()), desc.width, desc.height);
}

RenderTargetHandle IRenderer::createRenderTarget(const RenderTargetDesc &)
{
    INK_ERROR << "createRenderTarget: this renderer has no offscreen targets";
    return {};
}

bool IRenderer::resizeRenderTarget(RenderTargetHandle, u32, u32)
{
    return false;
}

BufferHandle IRenderer::createBuffer(usize, std::span<const std::byte>)
{
    INK_ERROR << "createBuffer: this renderer has no compute (see RendererCaps::Compute)";
    return {};
}

bool IRenderer::updateBuffer(BufferHandle, usize, std::span<const std::byte>)
{
    return false;
}

bool IRenderer::readBuffer(BufferHandle, usize, std::span<std::byte>)
{
    return false;
}

ComputeHandle IRenderer::createCompute(const ComputeDesc &)
{
    INK_ERROR << "createCompute: this renderer has no compute (see RendererCaps::Compute)";
    return {};
}

void IRenderer::dispatch(ComputeHandle, std::span<const BufferHandle>, glm::uvec3, std::span<const std::byte>)
{
}

void IRenderer::drawMeshInstancedIndirect(MeshHandle, MaterialHandle, BufferHandle, BufferHandle, usize)
{
}

u32 IRenderer::meshIndexCount(MeshHandle mesh) const
{
    const MeshRecord *record = getMesh(mesh);
    return record ? record->indexCount : 0;
}

glm::uvec2 IRenderer::targetSize(RenderTargetHandle) const
{
    return glm::uvec2{0};
}

TextureHandle IRenderer::renderTargetTexture(RenderTargetHandle) const
{
    return {};
}

void IRenderer::beginRenderPass(RenderTargetHandle target)
{
    if (isValidHandle(target))
        INK_ERROR << "beginRenderPass: this renderer has no offscreen targets";
    else
        beginRenderPass();
}

bool IRenderer::readPixels(RenderTargetHandle, std::span<u8>)
{
    return false;
}

TextureHandle IRenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    const std::array<u8, 4> texel{r, g, b, a};
    return createTextureFromPixels(texel.data(), 1, 1);
}

TextureHandle IRenderer::createCheckerboardTexture(u32 size)
{
    const ImageData image = ImageLoader::makeCheckerboard(size);
    return createTextureFromPixels(image.pixels.data(), image.width, image.height);
}

TextureHandle IRenderer::createCoverageTexture(u32 width, u32 height)
{
    if (width == 0 || height == 0 || static_cast<u64>(width) * height > std::numeric_limits<usize>::max() / 4)
        return {};

    std::vector<u8> rgba(static_cast<usize>(width) * height * 4, 255);
    for (usize i = 3; i < rgba.size(); i += 4)
        rgba[i] = 0;
    const TextureHandle handle = createDynamicTexture(width, height);
    if (isValidHandle(handle))
    {
        updateTextureRegion(handle, 0, 0, width, height, rgba.data());
        _coverageFallbacks[handle] = {width, height};
    }
    return handle;
}

void IRenderer::updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                            const u8 *coverage)
{
    const auto entry = _coverageFallbacks.find(handle);
    if (!coverage || width == 0 || height == 0 || entry == _coverageFallbacks.end())
        return;
    const glm::uvec2 size = entry->second;
    if (!regionFits(x, y, width, height, size.x, size.y))
        return;

    const usize count = static_cast<usize>(width) * height;
    std::vector<u8> rgba(count * 4, 255);
    for (usize i = 0; i < count; ++i)
        rgba[i * 4 + 3] = coverage[i];
    updateTextureRegion(handle, x, y, width, height, rgba.data());
}

bool IRenderer::updateMesh(MeshHandle mesh, std::span<const gfx::Vertex3D> vertices)
{
    MeshRecord *record = isValidHandle(mesh) && mesh.value() <= _meshes.size() ? &_meshes[mesh.value() - 1] : nullptr;
    if (!record || vertices.size() != record->vertexCount || !updateVertexBuffer(record->vertexBuffer, vertices))
        return false;
    record->bounds = boundsOf(vertices);
    return true;
}

bool IRenderer::updateVertexBuffer(VertexBufferHandle, std::span<const gfx::Vertex3D>)
{
    return false;
}

MeshHandle IRenderer::createMesh(const gfx::Mesh3D &mesh)
{
    //! The caller keeps its mesh, so its arrays are copied here and the copies
    //! are what gets moved onward.
    return createMesh(gfx::Mesh3D{mesh.vertices, mesh.indices});
}

MeshHandle IRenderer::createMesh(gfx::Mesh3D &&mesh)
{
    if (mesh.empty())
    {
        INK_WARN << "createMesh: refusing to upload an empty mesh";
        return {};
    }

    MeshRecord record;
    record.indexCount = static_cast<u32>(mesh.indices.size());
    record.vertexCount = static_cast<u32>(mesh.vertices.size());
    record.bounds = boundsOf(mesh.vertices);

    /*
     * Moved, not copied. createVertexBuffer/createIndexBuffer already take
     * their arrays by rvalue reference
     */
    record.vertexBuffer = createVertexBuffer(std::move(mesh.vertices));
    record.indexBuffer = createIndexBuffer(std::move(mesh.indices));

    if (!isValidHandle(record.vertexBuffer) || !isValidHandle(record.indexBuffer))
    {
        INK_ERROR << "createMesh: backend failed to allocate the buffer pair";
        return {};
    }

    _meshes.push_back(record);
    return static_cast<MeshHandle>(_meshes.size()); // 1-based
}

void IRenderer::drawMesh(MeshHandle mesh, TextureHandle texture)
{
    const MeshRecord *record = getMesh(mesh);
    if (!record)
        return;

    bindVertexBuffer(record->vertexBuffer);
    bindIndexBuffer(record->indexBuffer);

    if (isValidHandle(texture))
        bindTexture(texture);

    drawIndexed(record->indexCount);
}

void IRenderer::drawMeshes(std::span<const DrawItem> items)
{
    /*
     * The straightforward serial reading of the batch, and the definition of
     * what any overriding backend must reproduce. A backend that records the
     * items concurrently is still expected to produce this exact draw order.
     *
     * setTransform() takes a full TransformUBO, so the caller's view/proj have
     * to be preserved while only the model matrix varies per item; the last
     * value the caller set is read back here rather than being re-derived.
     *
     * Scoped as RecordScene here rather than in each backend: this body *is*
     * the scene-recording phase for OpenGL and the software rasteriser, which
     * both inherit it. VulkanRenderer overrides drawMeshes() and opens its own
     * RecordScene scope around the threaded version, so the phase means the
     * same thing on all three and is never double-counted.
     */
    AURA_FRAME_SCOPE(FramePhase::RecordScene);

    const gfx::TransformUBO caller = _currentTransform;
    gfx::TransformUBO transform = caller;
    const Frustum frustum = viewFrustum();

    for (const DrawItem &item : items)
    {
        //! Bound even for a culled item: later items without a material draw with it.
        if (isValidHandle(item.material))
            bindMaterial(item.material);
        const MeshRecord *mesh = getMesh(item.mesh);
        if (!mesh || culled(*mesh, item.model, frustum))
            continue;

        transform.model = item.model;
        setTransform(transform);
        drawMesh(item.mesh);
    }

    //! Later World batches and immediate draws use the caller's model, not the last item's.
    if (!items.empty())
        setTransform(caller);
}

void IRenderer::drawMeshInstanced(MeshHandle mesh, MaterialHandle material, std::span<const glm::mat4> models)
{
    _instanceItems.clear();
    for (const glm::mat4 &model : models)
        //! The material on the first item only: later items keep it.
        _instanceItems.push_back({mesh, _instanceItems.empty() ? material : MaterialHandle{}, model});
    drawMeshes(_instanceItems);
}

Frustum IRenderer::viewFrustum() const noexcept
{
    return Frustum::fromMatrix(_currentTransform.proj * _currentTransform.view, depthZeroToOne(getBackendType()));
}

bool IRenderer::culled(const MeshRecord &mesh, const glm::mat4 &model, const Frustum &frustum) noexcept
{
    return !visible(frustum, transformed(mesh.bounds, model));
}

Aabb IRenderer::meshBounds(MeshHandle mesh) const
{
    const MeshRecord *record = getMesh(mesh);
    return record ? record->bounds : Aabb{};
}

MaterialHandle IRenderer::createMaterial(const Material &material)
{
    _materials.push_back(material);
    return static_cast<MaterialHandle>(_materials.size()); // 1-based
}

void IRenderer::bindMaterial(MaterialHandle handle)
{
    const Material *material = getMaterial(handle);
    if (!material)
        return;

    _currentMaterial = *material;
    _currentMaterialHandle = handle;

    if (isValidHandle(material->albedo))
        bindTexture(material->albedo);
}

GpuTimingStats IRenderer::gpuTiming() const noexcept
{
    return {};
}

void IRenderer::setGpuTimingEnabled(bool enabled) noexcept
{
    _gpuTimingEnabled = enabled;
}

bool IRenderer::gpuTimingEnabled() const noexcept
{
    return _gpuTimingEnabled;
}

void IRenderer::setLights(std::span<const gfx::Light> lights)
{
    if (lights.size() > gfx::kMaxLights)
        INK_WARN << "setLights: " << lights.size() << " lights, the shaders take " << gfx::kMaxLights;
    _lights.assign(lights.begin(),
                   lights.begin() + static_cast<std::ptrdiff_t>(std::min<usize>(lights.size(), gfx::kMaxLights)));
}

std::span<const gfx::Light> IRenderer::lights() const noexcept
{
    return _lights;
}

RendererCaps IRenderer::caps() const noexcept
{
    return RendererCaps::None;
}

PipelineHandle IRenderer::createPipeline(const PipelineDesc &)
{
    INK_ERROR << "createPipeline: this renderer takes no shaders of its own (see RendererCaps::CustomPipelines)";
    return {};
}

PipelineLimits IRenderer::pipelineLimits() const noexcept
{
    return {};
}

void IRenderer::drawPipeline(const PipelineDraw &)
{
}

glm::mat4 IRenderer::clipCorrection(RenderTargetHandle) const noexcept
{
    return glm::mat4{1.0f};
}

gfx::LightingUBO IRenderer::lighting() const noexcept
{
    gfx::LightingUBO block;
    block.count = static_cast<u32>(_lights.size());
    for (usize i = 0; i < _lights.size(); ++i)
    {
        const gfx::Light &light = _lights[i];
        const bool spot = light.type == gfx::LightType::Spot;
        //! A directional light's direction goes as given: shadeDirectional() normalises it.
        const glm::vec3 direction =
            light.type == gfx::LightType::Directional ? light.direction : glm::normalize(light.direction);
        block.lights[i] = {glm::vec4(light.position, light.range), glm::vec4(light.color, light.intensity),
                           glm::vec4(direction, spot ? std::cos(light.outerAngle) : -1.0f),
                           glm::vec4(spot ? std::cos(std::min(light.innerAngle, light.outerAngle)) : -1.0f,
                                     static_cast<f32>(light.type), 0.0f, 0.0f)};
    }
    return block;
}

gfx::MaterialUniforms IRenderer::materialUniforms(const Material &material) noexcept
{
    gfx::MaterialUniforms uniforms;
    uniforms.tint = material.tint;
    uniforms.emissive = material.emissive;
    uniforms.roughness = material.roughness;
    uniforms.metallic = material.metallic;
    uniforms.maps = (isValidHandle(material.normalMap) ? gfx::MaterialUniforms::kNormalMap : 0u) |
                    (isValidHandle(material.metallicRoughness) ? gfx::MaterialUniforms::kMetallicRoughnessMap : 0u) |
                    (isValidHandle(material.emissiveMap) ? gfx::MaterialUniforms::kEmissiveMap : 0u);
    return uniforms;
}

const IRenderer::MeshRecord *IRenderer::getMesh(MeshHandle handle) const
{
    if (!isValidHandle(handle) || handle.value() > _meshes.size())
        return nullptr;

    return &_meshes[handle.value() - 1];
}

const Material *IRenderer::getMaterial(MaterialHandle handle) const
{
    if (!isValidHandle(handle) || handle.value() > _materials.size())
        return nullptr;

    return &_materials[handle.value() - 1];
}

void IRenderer::clearSharedResources()
{
    _coverageFallbacks.clear();
    _meshes.clear();
    _materials.clear();
    _currentMaterial = Material{};
}

} // namespace aura3d
