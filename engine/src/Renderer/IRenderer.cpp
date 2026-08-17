#include "aura/Renderer/IRenderer.h"

#include "aura/Core/ImageLoader/ImageLoader.h"
#include "aura/Core/Profiling/FrameProfiler.h"

namespace aura3d {

void IRenderer::run(move_only_function<void()> onFrame)
{
    auto* windowMgr = getWindowManager();
    if (!windowMgr) return;

    auto runCleanup = [this]() { cleanup(); };
    windowMgr->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{ runCleanup, nullptr });

    _running = true;
    windowMgr->process([&]() {
        beginFrame();
        onFrame();
        endFrame();
    });
}

/*
 * Everything below is backend-independent: decoding an image, pairing a
 * vertex/index buffer into a mesh and tracking materials look identical for
 * Vulkan, OpenGL and the software rasteriser. They are implemented once here on
 * top of each backend's primitives (createTextureFromPixels, createVertexBuffer,
 * bindTexture, ...) instead of being repeated three times. Backends may still
 * override any of them where they can do better.
 */

TextureHandle IRenderer::createTextureFromFile(const std::string& path)
{
    ImageData image = ImageLoader::loadRGBA(path);

    if (!image.valid()) 
    {
        INK_WARN << "createTextureFromFile: '" << path
                 << "' unavailable; substituting the checkerboard fallback";
        image = ImageLoader::makeCheckerboard();
    }

    return createTextureFromPixels(image.pixels.data(), image.width, image.height);
}

TextureHandle IRenderer::createCheckerboardTexture(u32 size)
{
    const ImageData image = ImageLoader::makeCheckerboard(size);
    return createTextureFromPixels(image.pixels.data(), image.width, image.height);
}

MeshHandle IRenderer::createMesh(const gfx::Mesh3D& mesh)
{
    //! The caller keeps its mesh, so its arrays are copied here and the copies
    //! are what gets moved onward.
    return createMesh(gfx::Mesh3D{mesh.vertices, mesh.indices});
}

MeshHandle IRenderer::createMesh(gfx::Mesh3D&& mesh)
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
    record.indexBuffer  = createIndexBuffer(std::move(mesh.indices));

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
    const MeshRecord* record = getMesh(mesh);
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

    gfx::TransformUBO transform = _currentTransform;

    for (const DrawItem& item : items)
    {
        transform.model = item.model;
        setTransform(transform);

        if (isValidHandle(item.material))
            bindMaterial(item.material);

        drawMesh(item.mesh);
    }
}

MaterialHandle IRenderer::createMaterial(const Material& material)
{
    _materials.push_back(material);
    return static_cast<MaterialHandle>(_materials.size()); // 1-based
}

void IRenderer::bindMaterial(MaterialHandle handle)
{
    const Material* material = getMaterial(handle);
    if (!material)
        return;

    _currentMaterial = *material;

    if (isValidHandle(material->albedo))
        bindTexture(material->albedo);
}

void IRenderer::setLight(const gfx::LightUBO& light)
{
    _light = light;
}

const IRenderer::MeshRecord* IRenderer::getMesh(MeshHandle handle) const
{
    if (!isValidHandle(handle) || handle.value() > _meshes.size())
        return nullptr;

    return &_meshes[handle.value() - 1];
}

const Material* IRenderer::getMaterial(MaterialHandle handle) const
{
    if (!isValidHandle(handle) || handle.value() > _materials.size())
        return nullptr;

    return &_materials[handle.value() - 1];
}

void IRenderer::clearSharedResources()
{
    _meshes.clear();
    _materials.clear();
    _currentMaterial = Material{};
}

} // namespace aura3d
