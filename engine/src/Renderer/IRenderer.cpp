#include "aura/Renderer/IRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include "aura/Core/ImageLoader/ImageLoader.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/Renderer/RendererFactory.h"

namespace aura3d
{

namespace
{

constexpr std::array<u32, 6> kQuadIndices{0, 1, 2, 2, 3, 0};
constexpr std::array<u32, 3> kTriangleIndices{0, 1, 2};

template <glm::length_t N, glm::qualifier Q> [[nodiscard]] bool finite(const glm::vec<N, f32, Q> &value) noexcept
{
    for (glm::length_t i = 0; i < N; ++i)
        if (!std::isfinite(value[i]))
            return false;
    return true;
}

[[nodiscard]] bool visible(const glm::vec4 &color) noexcept
{
    return color.a > 0.0f && finite(color);
}

[[nodiscard]] bool depthZeroToOne(RendererChoice backend) noexcept
{
    return RendererFactory::clipSpaceFor(backend) == Camera::ClipSpace::Vulkan;
}

//! Keeps the part of [a, b] on the non-negative side of a plane, given each end's signed distance.
[[nodiscard]] bool clipSegment(glm::vec4 &a, glm::vec4 &b, f32 da, f32 db) noexcept
{
    if (da < 0.0f && db < 0.0f)
        return false;
    if (da < 0.0f)
        a = glm::mix(a, b, da / (da - db));
    else if (db < 0.0f)
        b = glm::mix(b, a, db / (db - da));
    return true;
}

//! Screen-space quad @p width pixels wide around [@p from, @p to], flat-ended; nullopt draws nothing.
[[nodiscard]] std::optional<std::array<gfx::BatchVertex, 4>> lineQuad(glm::vec3 from, glm::vec3 to,
                                                                      const glm::vec4 &color, f32 width) noexcept
{
    const f32 length = std::hypot(to.x - from.x, to.y - from.y);
    if (!finite(from) || !finite(to) || !visible(color) || !(width > 0.0f) || !std::isfinite(width) ||
        !(length > 0.0f) || !std::isfinite(length))
        return std::nullopt;

    const glm::vec3 normal{glm::vec2{from.y - to.y, to.x - from.x} * (width * 0.5f / length), 0.0f};
    return std::array<gfx::BatchVertex, 4>{
        {{from - normal, {}, color}, {to - normal, {}, color}, {to + normal, {}, color}, {from + normal, {}, color}}};
}

} // namespace

void IRenderer::drawLine(glm::vec2 from, glm::vec2 to, const glm::vec4 &color, f32 width)
{
    if (const auto quad = lineQuad({from, 0.0f}, {to, 0.0f}, color, width))
        drawBatch(*quad, kQuadIndices, {});
}

void IRenderer::drawLine(glm::vec3 from, glm::vec3 to, const glm::vec4 &color, f32 width)
{
    const glm::vec2 target{renderTargetSize()};
    if (target.x <= 0.0f || target.y <= 0.0f)
        return;

    const glm::mat4 transform = batchTransform(gfx::BatchSpace::World);
    glm::vec4 a = transform * glm::vec4(from, 1.0f);
    glm::vec4 b = transform * glm::vec4(to, 1.0f);

    //! Clipped to the near plane, and to w > 0 so the divide is defined; the rasterizer clips
    //! the rest. Widening the projected segment in pixels keeps the width at any depth.
    const bool zeroToOne = depthZeroToOne(getBackendType());
    constexpr f32 kMinW = 1e-6f;
    if (!clipSegment(a, b, zeroToOne ? a.z : a.z + a.w, zeroToOne ? b.z : b.z + b.w) ||
        !clipSegment(a, b, a.w - kMinW, b.w - kMinW))
        return;

    const auto toScreen = [&](const glm::vec4 &clip)
    {
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec3{(ndc.x + 1.0f) * 0.5f * target.x, (1.0f - ndc.y) * 0.5f * target.y,
                         zeroToOne ? ndc.z : (ndc.z + 1.0f) * 0.5f};
    };
    if (const auto quad = lineQuad(toScreen(a), toScreen(b), color, width))
        drawBatch(*quad, kQuadIndices, {});
}

void IRenderer::fillRect(glm::vec2 origin, glm::vec2 size, const glm::vec4 &color)
{
    const glm::vec2 end = origin + size;
    if (!finite(origin) || !finite(end) || !(size.x > 0.0f) || !(size.y > 0.0f) || !visible(color))
        return;

    const std::array<gfx::BatchVertex, 4> quad{{{{origin, 0.0f}, {}, color},
                                                {{end.x, origin.y, 0.0f}, {}, color},
                                                {{end, 0.0f}, {}, color},
                                                {{origin.x, end.y, 0.0f}, {}, color}}};
    drawBatch(quad, kQuadIndices, {});
}

void IRenderer::fillTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4 &color)
{
    if (!finite(a) || !finite(b) || !finite(c) || !visible(color))
        return;

    //! In double, so rounding cannot call a thin valid triangle degenerate.
    const f64 area = (static_cast<f64>(b.x) - a.x) * (static_cast<f64>(c.y) - a.y) -
                     (static_cast<f64>(b.y) - a.y) * (static_cast<f64>(c.x) - a.x);
    if (area == 0.0)
        return;

    const std::array<gfx::BatchVertex, 3> triangle{
        {{{a, 0.0f}, {}, color}, {{b, 0.0f}, {}, color}, {{c, 0.0f}, {}, color}}};
    drawBatch(triangle, kTriangleIndices, {});
}

void IRenderer::fillTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec4 &color)
{
    if (!finite(a) || !finite(b) || !finite(c) || !visible(color) ||
        glm::cross(glm::dvec3(b) - glm::dvec3(a), glm::dvec3(c) - glm::dvec3(a)) == glm::dvec3(0.0))
        return;

    const std::array<gfx::BatchVertex, 3> triangle{{{a, {}, color}, {b, {}, color}, {c, {}, color}}};
    drawBatch(triangle, kTriangleIndices, {}, gfx::BatchSpace::World);
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

    gfx::TransformUBO transform = _currentTransform;

    for (const DrawItem &item : items)
    {
        transform.model = item.model;
        setTransform(transform);

        if (isValidHandle(item.material))
            bindMaterial(item.material);

        drawMesh(item.mesh);
    }
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
