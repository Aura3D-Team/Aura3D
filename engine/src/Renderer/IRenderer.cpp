#include "aura/Renderer/IRenderer.h"

#include <algorithm>
#include <limits>

#include "aura/Core/ImageLoader/ImageLoader.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/Renderer/RendererFactory.h"

namespace aura3d
{

namespace
{

[[nodiscard]] bool depthZeroToOne(RendererChoice backend) noexcept
{
    return RendererFactory::clipSpaceFor(backend) == Camera::ClipSpace::Vulkan;
}

} // namespace

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
    windowMgr->process(
        [this, onFrame = std::make_shared<move_only_function<void()>>(std::move(onFrame))]()
        {
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
        //! A backend may reissue a destroyed texture's id; its old entry must not answer for this one.
        std::erase_if(_coverageFallbacks,
                      [handle](const CoverageFallback &entry)
                      {
                          return entry.handle == handle;
                      });
        _coverageFallbacks.push_back({handle, width, height});
    }
    return handle;
}

void IRenderer::updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                            const u8 *coverage)
{
    const auto entry = std::ranges::find(_coverageFallbacks, handle, &CoverageFallback::handle);
    if (!coverage || width == 0 || height == 0 || entry == _coverageFallbacks.end() || x >= entry->width ||
        y >= entry->height || width > entry->width - x || height > entry->height - y)
        return;

    const usize count = static_cast<usize>(width) * height;
    std::vector<u8> rgba(count * 4, 255);
    for (usize i = 0; i < count; ++i)
        rgba[i * 4 + 3] = coverage[i];
    updateTextureRegion(handle, x, y, width, height, rgba.data());
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

    for (const DrawItem &item : items)
    {
        transform.model = item.model;
        setTransform(transform);

        if (isValidHandle(item.material))
            bindMaterial(item.material);

        drawMesh(item.mesh);
    }

    //! Later World batches and immediate draws use the caller's model, not the last item's.
    if (!items.empty())
        setTransform(caller);
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

    if (isValidHandle(material->albedo))
        bindTexture(material->albedo);
}

void IRenderer::setLight(const gfx::LightUBO &light)
{
    _light = light;
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
