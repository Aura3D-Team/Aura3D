/*
 * The rendering path: what a widget records, and what the backend turns it
 * into.
 *
 * Two things here fail invisibly in a running application, which is why they
 * are pinned by a test rather than by looking at the screen:
 *
 *  - the UI must leave as **one draw call**. Solid rectangles sample the glyph
 *    atlas' opaque cell so panels, borders and text share a texture; if that
 *    broke, the UI would still look perfectly correct while quietly costing a
 *    draw call per widget.
 *
 *  - a rounded rectangle must cost **constant geometry**. The nine-slice
 *    against the atlas' corner mask is bounded whatever the radius; a
 *    regression to per-scanline caps would be invisible until a profile.
 *
 * A stub IRenderer that records drawBatch() is the whole harness -- no
 * window, no GPU, no driver.
 */

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/UI/UI.hpp"

#include "TestUtils.h"

using namespace aura3d;
using namespace aura3d::ui;

namespace
{

/// An IRenderer that records batches and does nothing else. Texture handles
/// are handed out 1-based, matching the real backends, so isValidHandle()
/// accepts them and the UI considers itself usable.
class RecordingRenderer final : public IRenderer
{
  public:
    struct Batch
    {
        usize vertexCount = 0;
        usize indexCount = 0;
        TextureHandle texture;
    };

    RecordingRenderer() : IRenderer(wma::WindowDetails{})
    {
    }

    using IRenderer::drawBatch;
    void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices, TextureHandle texture,
                   gfx::BatchSpace space = gfx::BatchSpace::Screen) override
    {
        for (const u32 index : indices)
            indicesInRange &= index < vertices.size();
        if (space == gfx::BatchSpace::Screen)
        {
            batches.push_back({vertices.size(), indices.size(), texture});
            vertexTotal += vertices.size();
            quads.insert(quads.end(), vertices.begin(), vertices.end());
        }
        else
        {
            sceneBatches.push_back({vertices.size(), indices.size(), texture});
            scene.insert(scene.end(), vertices.begin(), vertices.end());
        }
    }

    [[nodiscard]] glm::uvec2 renderTargetSize() const noexcept override
    {
        return target;
    }

    //! Where @p position lands in render-target pixels and [0, 1] depth under the current transform.
    [[nodiscard]] glm::vec3 toScreen(glm::vec3 position) const
    {
        const glm::vec4 clip =
            _currentTransform.proj * _currentTransform.view * _currentTransform.model * glm::vec4(position, 1.0f);
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return {(ndc.x + 1.0f) * 0.5f * static_cast<f32>(target.x), (1.0f - ndc.y) * 0.5f * static_cast<f32>(target.y),
                (ndc.z + 1.0f) * 0.5f};
    }

    using IRenderer::batchTransform;

    struct Texture
    {
        u32 width, height;
        std::vector<u8> rgba;
    };
    std::unordered_map<TextureHandle, Texture> textures;
    TextureHandle createDynamicTexture(u32 width, u32 height) override
    {
        if (failTextures)
            return {};
        const auto handle = ++_next;
        textures.emplace(handle, Texture{width, height, std::vector<u8>(usize(width) * height * 4)});
        return handle;
    }

    void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgba) override
    {
        ++uploads;
        auto &texture = textures.at(handle);
        for (u32 row = 0; row < height; ++row)
            std::copy_n(rgba + usize(row) * width * 4, usize(width) * 4,
                        texture.rgba.data() + (usize(y + row) * texture.width + x) * 4);
    }

    // Sample submitted geometry AND the uploaded coverage texture. Geometry
    // alone cannot distinguish a rounded ring from its rectangular mask quad.
    float alphaAt(glm::vec2 point) const
    {
        usize start = 0;
        float alpha = 0;
        for (const auto &batch : batches)
        {
            const auto &texture = textures.at(batch.texture);
            for (usize base = start; base < start + batch.vertexCount; base += 4)
            {
                const auto &a = quads[base];
                const auto &b = quads[base + 2];
                if (point.x < a.pos.x || point.y < a.pos.y || point.x >= b.pos.x || point.y >= b.pos.y)
                    continue;
                const auto uv =
                    a.texCoord + (b.texCoord - a.texCoord) * ((point - glm::vec2(a.pos)) / glm::vec2(b.pos - a.pos));
                const u32 x = std::min(u32(std::max(0.f, uv.x * texture.width)), texture.width - 1);
                const u32 y = std::min(u32(std::max(0.f, uv.y * texture.height)), texture.height - 1);
                const float coverage = texture.rgba[(usize(y) * texture.width + x) * 4 + 3] / 255.f * a.color.a;
                alpha = coverage + alpha * (1 - coverage);
            }
            start += batch.vertexCount;
        }
        return alpha;
    }

    void clear()
    {
        batches.clear();
        quads.clear();
        sceneBatches.clear();
        scene.clear();
        vertexTotal = 0;
    }

    /// True when any emitted quad covers @p point with a visible colour.
    [[nodiscard]] bool covers(glm::vec2 point) const
    {
        for (usize base = 0; base + 4 <= quads.size(); base += 4)
        {
            glm::vec2 low = quads[base].pos;
            glm::vec2 high = low;
            f32 alpha = 0.0f;

            for (usize corner = 0; corner < 4; ++corner)
            {
                low = glm::min(low, glm::vec2(quads[base + corner].pos));
                high = glm::max(high, glm::vec2(quads[base + corner].pos));
                alpha = std::max(alpha, quads[base + corner].color.a);
            }

            if (alpha > 0.0f && point.x > low.x && point.x < high.x && point.y > low.y && point.y < high.y)
                return true;
        }

        return false;
    }

    std::vector<Batch> batches;
    std::vector<gfx::BatchVertex> quads;
    std::vector<Batch> sceneBatches;
    std::vector<gfx::BatchVertex> scene;
    glm::uvec2 target{200, 100};
    usize vertexTotal = 0;
    usize uploads = 0;
    bool indicesInRange = true;
    bool failTextures = false;

    /// Set to act as a backend whose swapchain was rebuilt under a clean tree.
    bool frameLost = false;
    [[nodiscard]] bool needsFrame() const noexcept override
    {
        return frameLost;
    }

    // Everything below is inert: the UI never calls it.
    void beginFrame() override
    {
    }
    void beginRenderPass() override
    {
    }
    void endRenderPass() override
    {
    }
    void endFrame() override
    {
    }
    void setTransform(const gfx::TransformUBO &ubo) override
    {
        _currentTransform = ubo;
    }
    void bindVertexBuffer(VertexBufferHandle) override
    {
    }
    void bindIndexBuffer(IndexBufferHandle) override
    {
    }
    void bindTexture(TextureHandle) override
    {
    }
    void drawIndexed(u32, u32) override
    {
    }
    void draw(u32, u32) override
    {
    }
    void setClearColor(f32, f32, f32, f32) override
    {
    }
    wma::IWindowManager *getWindowManager() override
    {
        return nullptr;
    }

    TextureHandle createSolidColorTexture(u8, u8, u8, u8) override
    {
        return {};
    }
    TextureHandle createTextureFromPixels(const u8 *, u32, u32) override
    {
        return {};
    }

    void initialize(AuraSettings *, const JobSystem *) override
    {
    }
    void handleWindowChanges() override
    {
    }
    void cleanup() override
    {
    }
    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D> &&) override
    {
        return {};
    }
    IndexBufferHandle createIndexBuffer(std::vector<u16> &&) override
    {
        return {};
    }
    IndexBufferHandle createIndexBuffer(std::vector<u32> &&) override
    {
        return {};
    }
    RendererChoice getBackendType() const override
    {
        return backend;
    }
    void createWindow(const char *, const wma::WindowBackend &) override
    {
    }

    RendererChoice backend = RendererChoice::SOFTWARE;

  private:
    TextureHandle _next{0};
};

struct Harness
{
    RecordingRenderer renderer;
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};
    DrawList list;
    DrawListRenderer backend{renderer, shaper};

    explicit Harness(glm::vec2 size = {400.0f, 300.0f})
    {
        root.resize(size);
    }

    /// One whole frame: layout, record, build, submit.
    /// @return True when the tree re-recorded, which is what an idle frame
    ///         must stop doing.
    bool frame(f32 deltaSeconds = 0.0f)
    {
        root.update(deltaSeconds);

        const bool recorded = root.paint(list);
        if (recorded)
            backend.build(list, root.scale());

        renderer.clear();
        backend.submit();

        return recorded;
    }
};

[[nodiscard]] usize countType(const DrawList &list, DrawCommandType type)
{
    return static_cast<usize>(std::ranges::count_if(list.commands(),
                                                    [type](const DrawCommand &command)
                                                    {
                                                        return command.type == type;
                                                    }));
}

void testClipping()
{
    DrawList list;
    list.begin(Rect::fromSize({0.0f, 0.0f}, {100.0f, 100.0f}));

    list.fillRect(Rect::fromSize({10.0f, 10.0f}, {20.0f, 20.0f}), glm::vec4{1.0f});
    AURA_CHECK(list.size() == 1, "a visible rectangle is recorded");

    list.fillRect(Rect::fromSize({500.0f, 500.0f}, {20.0f, 20.0f}), glm::vec4{1.0f});
    AURA_CHECK(list.size() == 1, "one entirely outside the surface is dropped");

    list.fillRect(Rect::fromSize({10.0f, 10.0f}, {20.0f, 20.0f}), glm::vec4{0.0f});
    AURA_CHECK(list.size() == 1, "and so is a fully transparent one");

    {
        const ClipScope clipped(list, Rect::fromSize({0.0f, 0.0f}, {40.0f, 40.0f}));
        AURA_CHECK(list.clip().max.x == 40.0f, "a clip scope narrows the clip");

        list.fillRect(Rect::fromSize({60.0f, 10.0f}, {10.0f, 10.0f}), glm::vec4{1.0f});
        AURA_CHECK(list.size() == 1, "a rectangle outside the clip is dropped");
    }

    AURA_CHECK(list.clip().max.x == 100.0f, "and the clip is restored on the way out");

    list.popClip();
    AURA_CHECK(list.clip().max.x == 100.0f, "an unbalanced pop cannot take the surface clip");
}

void testSingleBatch()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();
    page.layout().padding = Thickness::all(12.0f);
    page.setSpacing(8.0f);

    page.add<Label>("Renderer");
    page.add<Button>("Reload shaders");
    page.add<CheckBox>("Wireframe");
    page.add<Slider>(0.0f, 4.0f);
    page.add<ProgressBar>(0.5f);
    page.add<Separator>();

    harness.frame();

    AURA_CHECK(harness.renderer.batches.size() == 1, "a UI of surfaces and text at one size is exactly one draw call");
    AURA_CHECK(harness.renderer.indicesInRange, "and every index addresses its own batch");

    AURA_CHECK(countType(harness.list, DrawCommandType::Text) == 3,
               "with one text command per labelled control and no more");
}

void testFontSizesShareBatch()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();

    for (const f32 size : {10.f, 11.f, 12.f, 13.f, 14.f, 15.f, 16.f, 18.f, 20.f, 24.f, 28.f})
        page.add<Label>("Heading and body").setFontSize(size);

    harness.frame();

    AURA_CHECK(harness.renderer.batches.size() == 1, "eleven requested font sizes share one draw call");
    AURA_CHECK(harness.renderer.textures.size() == 1, "font sizes share one texture allocation");
    AURA_CHECK(harness.renderer.uploads > 0, "and its glyph page is uploaded");
}

void testCoverageCompatibility()
{
    RecordingRenderer renderer;
    const auto handle = renderer.createCoverageTexture(3, 2);
    const auto &pixels = renderer.textures.at(handle).rgba;
    AURA_CHECK(pixels[0] == 255 && pixels[3] == 0 && pixels[23] == 0,
               "coverage fallback initializes transparent white, preserving filtering at cell edges");
    const std::array<u8, 3> patch{0, 127, 255};
    renderer.updateCoverageTextureRegion(handle, 0, 1, 3, 1, patch.data());
    AURA_CHECK(pixels[15] == 0 && pixels[19] == 127 && pixels[23] == 255 && pixels[20] == 255,
               "odd-width coverage uploads preserve white RGB and each alpha byte");
    const usize uploads = renderer.uploads;
    renderer.updateCoverageTextureRegion(handle, UINT32_MAX, 0, 3, 1, patch.data());
    renderer.updateCoverageTextureRegion(handle, 1, 0, UINT32_MAX, 1, patch.data());
    AURA_CHECK(renderer.uploads == uploads, "invalid fallback coverage updates are rejected before reading pixels");
}

void testRendererPrimitives()
{
    RecordingRenderer renderer;
    const glm::vec4 tint{.2f, .4f, .8f, .5f};
    gfx::Canvas shapes;
    //! Draws what was added since the last call as one batch.
    const auto submit = [&](gfx::BatchSpace space = gfx::BatchSpace::Screen)
    {
        renderer.drawBatch(shapes, {}, space);
        shapes.clear();
    };

    const std::array<std::array<glm::vec2, 2>, 3> segments{
        {{{{10, 10}, {30, 10}}}, {{{40, 10}, {40, 30}}}, {{{30, 40}, {10, 20}}}}};
    for (const auto &[from, to] : segments)
        shapes.line(from, to, tint, 4);
    submit();
    AURA_CHECK(renderer.batches.size() == 1 && renderer.vertexTotal == 12 && renderer.indicesInRange,
               "horizontal, vertical and reversed diagonal lines in one batch are one draw of a quad each");
    bool geometry = renderer.quads.size() == 12;
    for (usize i = 0; geometry && i < segments.size(); ++i)
    {
        const auto &a = renderer.quads[i * 4];
        const auto &b = renderer.quads[i * 4 + 1];
        const auto &c = renderer.quads[i * 4 + 2];
        const auto &d = renderer.quads[i * 4 + 3];
        geometry &= glm::length(glm::vec2(a.pos + d.pos) * .5f - segments[i][0]) < .001f &&
                    glm::length(glm::vec2(b.pos + c.pos) * .5f - segments[i][1]) < .001f &&
                    std::fabs(glm::length(d.pos - a.pos) - 4.0f) < .001f && a.color == tint;
    }
    AURA_CHECK(geometry && !isValidHandle(renderer.batches.front().texture),
               "line quads preserve endpoints, width and straight-alpha tint without allocating textures");

    renderer.clear();
    shapes.line(glm::vec2{5, 6}, glm::vec2{15, 6}, tint);
    submit();
    AURA_CHECK(renderer.vertexTotal == 4 && renderer.batches.front().indexCount == 6,
               "single lines use the portable triangle path at the default one-pixel width");

    const f32 aspect = static_cast<f32>(renderer.target.x) / static_cast<f32>(renderer.target.y);
    renderer.setTransform(
        {glm::mat4{1.0f}, glm::mat4{1.0f}, glm::perspectiveRH_NO(glm::radians(90.0f), aspect, 0.1f, 100.0f)});
    renderer.clear();
    const gfx::CanvasView view = renderer.canvasView();
    shapes.line(glm::vec2{0, 0}, glm::vec2{0, 0}, tint);
    shapes.line(glm::vec2{0, 0}, glm::vec2{1, 1}, tint, -1);
    shapes.line(glm::vec2{std::numeric_limits<f32>::infinity(), 0}, glm::vec2{1, 1}, tint);
    shapes.line(view, glm::vec3{0, 0, -5}, glm::vec3{0, 0, -5}, tint);
    shapes.line(view, glm::vec3{0, 0, 5}, glm::vec3{1, 0, 5}, tint);
    shapes.line(view, glm::vec3{0, 0, -2}, glm::vec3{0, 0, -20}, tint);
    shapes.rect({}, {-1, 4}, tint);
    shapes.triangle(glm::vec2{0, 0}, glm::vec2{1, 1}, glm::vec2{2, 2}, tint);
    shapes.triangle(glm::vec3{0, 0, -1}, glm::vec3{1, 1, -2}, glm::vec3{2, 2, -3}, tint);
    AURA_CHECK(shapes.empty(), "degenerate, nonfinite, behind-camera and end-on primitives add nothing");
    submit();
    AURA_CHECK(renderer.batches.empty() && renderer.sceneBatches.empty(), "an empty batch draws nothing");

    shapes.rect({10, 20}, {30, 40}, tint);
    shapes.triangle(glm::vec2{0, 0}, glm::vec2{10, 0}, glm::vec2{0, 10}, tint);
    shapes.triangle(glm::vec2{0, 10}, glm::vec2{10, 0}, glm::vec2{0, 0}, tint);
    submit();
    AURA_CHECK(renderer.batches.size() == 1 && renderer.vertexTotal == 10 && renderer.indicesInRange &&
                   renderer.quads[0].pos == glm::vec3(10, 20, 0) && renderer.quads[2].pos == glm::vec3(40, 60, 0),
               "rectangles and either triangle winding submit valid untextured geometry");

    //! A segment receding from the camera stays four pixels wide at both ends, at its own depth.
    renderer.clear();
    shapes.line(renderer.canvasView(), glm::vec3{-1, -1, -2}, glm::vec3{1, -1, -30}, tint, 4);
    submit();
    bool constantWidth = renderer.batches.size() == 1 && renderer.quads.size() == 4 &&
                         renderer.batches.front().indexCount == 6 && renderer.indicesInRange;
    if (constantWidth)
    {
        const auto at = [&](usize i)
        {
            return renderer.quads[i].pos;
        };
        const glm::vec3 nearEnd = renderer.toScreen({-1, -1, -2});
        const glm::vec3 farEnd = renderer.toScreen({1, -1, -30});
        constantWidth = std::fabs(glm::length(glm::vec2(at(3) - at(0))) - 4.0f) < .01f &&
                        std::fabs(glm::length(glm::vec2(at(2) - at(1))) - 4.0f) < .01f &&
                        glm::length((at(0) + at(3)) * .5f - nearEnd) < .01f &&
                        glm::length((at(1) + at(2)) * .5f - farEnd) < .01f && nearEnd.z > 0.0f &&
                        nearEnd.z < farEnd.z && farEnd.z < 1.0f && renderer.quads[0].color == tint &&
                        !isValidHandle(renderer.batches.front().texture);
    }
    AURA_CHECK(constantWidth, "a 3D line is one screen quad at its depth, as wide in pixels near as far");

    renderer.clear();
    gfx::TransformUBO singular{glm::mat4{1}, glm::mat4{1}, glm::mat4{1}};
    singular.model[2][2] = 0;
    renderer.setTransform(singular);
    shapes.line(renderer.canvasView(), glm::vec3{-.5f, 0, 0}, glm::vec3{.5f, 0, 0}, tint, 4);
    submit();
    AURA_CHECK(renderer.quads.size() == 4 &&
                   std::fabs(glm::length(glm::vec2(renderer.quads[3].pos - renderer.quads[0].pos)) - 4) < .01f,
               "a line under a singular model transform keeps its pixel width, needing no inverse");
    renderer.setTransform({glm::mat4{1}, glm::mat4{1}, glm::perspectiveRH_NO(glm::radians(90.0f), aspect, .1f, 100.f)});

    //! Crossing the camera plane beside the eye: only the part in front survives, as a valid quad.
    renderer.clear();
    shapes.line(renderer.canvasView(), glm::vec3{-1, 0, -5}, glm::vec3{3, 0, 5}, tint, 2);
    submit();
    bool clipped = renderer.batches.size() == 1 && renderer.quads.size() == 4;
    for (usize i = 0; clipped && i < renderer.quads.size(); ++i)
        clipped &= std::isfinite(renderer.quads[i].pos.x) && renderer.quads[i].pos.z > -1e-4f;
    AURA_CHECK(clipped, "a 3D line through the camera is clipped to the part in front of it");

    //! Under a window-pixel orthographic transform both overloads draw the same quad.
    const auto [width, height] = std::array{static_cast<f32>(renderer.target.x), static_cast<f32>(renderer.target.y)};
    renderer.setTransform({glm::mat4{1.0f}, glm::mat4{1.0f}, glm::orthoRH_NO(0.0f, width, height, 0.0f, -1.0f, 1.0f)});
    renderer.clear();
    shapes.line(glm::vec2{30, 40}, glm::vec2{10, 20}, tint, 3);
    shapes.line(renderer.canvasView(), glm::vec3{30, 40, 0}, glm::vec3{10, 20, 0}, tint, 3);
    submit();
    bool matching = renderer.quads.size() == 8;
    for (usize i = 0; matching && i < 4; ++i)
        matching = glm::length(glm::vec2(renderer.quads[i].pos - renderer.quads[i + 4].pos)) < .01f;
    AURA_CHECK(matching, "the 3D overload reduces to the 2D one under a pixel-space transform");

    //! drawMeshes() hands the caller's model back, so World geometry after it is not moved by the last item.
    const glm::mat4 world = renderer.batchTransform(gfx::BatchSpace::World);
    IRenderer::DrawItem moved{};
    moved.model[3] = glm::vec4{100.0f, 0.0f, 0.0f, 1.0f};
    renderer.drawMeshes(std::span{&moved, 1});
    renderer.clear();
    shapes.line(glm::vec2{30, 40}, glm::vec2{10, 20}, tint, 3);
    shapes.line(renderer.canvasView(), glm::vec3{30, 40, 0}, glm::vec3{10, 20, 0}, tint, 3);
    submit();
    bool unmoved = renderer.batchTransform(gfx::BatchSpace::World) == world && renderer.quads.size() == 8;
    for (usize i = 0; unmoved && i < 4; ++i)
        unmoved = glm::length(glm::vec2(renderer.quads[i].pos - renderer.quads[i + 4].pos)) < .01f;
    AURA_CHECK(unmoved, "World lines after drawMeshes() use the caller's model, not the last mesh's");

    renderer.clear();
    shapes.triangle(glm::vec3{0, 0, 0}, glm::vec3{10, 0, 0}, glm::vec3{0, 10, 0}, tint);
    submit(gfx::BatchSpace::World);
    AURA_CHECK(renderer.sceneBatches.size() == 1 && renderer.scene.size() == 3 && renderer.batches.empty() &&
                   renderer.scene[1].pos == glm::vec3(10, 0, 0),
               "a World batch goes through the scene path, not the screen one");
}

void testScreenSpaceSharesClipConvention()
{
    //! Every backend presents y-up clip space (Vulkan through its flipped viewport), so pixels
    //! map identically; only depth differs: [-1, 1] on OpenGL and CPU, [0, 1] on Vulkan and Metal.
    RecordingRenderer renderer;
    const glm::vec2 size{renderer.target};
    const auto same = [](const glm::vec4 &a, const glm::vec4 &b)
    {
        return glm::all(glm::lessThan(glm::abs(a - b), glm::vec4{1e-5f}));
    };
    bool matching = true;
    for (const RendererChoice backend :
         {RendererChoice::OPENGL, RendererChoice::SOFTWARE, RendererChoice::VULKAN, RendererChoice::METAL})
    {
        renderer.backend = backend;
        const glm::mat4 screen = renderer.batchTransform(gfx::BatchSpace::Screen);
        const f32 nearPlane = backend == RendererChoice::VULKAN || backend == RendererChoice::METAL ? 0.0f : -1.0f;
        matching &= same(screen * glm::vec4{0, 0, 0, 1}, {-1, 1, nearPlane, 1}) &&
                    same(screen * glm::vec4{size, 1, 1}, {1, -1, 1, 1});
    }
    AURA_CHECK(matching, "screen pixels and depth map onto each backend's y-up clip space and depth range");
}

void testSharedAtlasUploads()
{
    AtlasTextShaper shaper;
    ShapedText text;
    shaper.shape("A", TextStyle{}, kUnbounded, text);
    DrawList list;
    list.begin(Rect::fromSize({}, {100, 40}));
    list.drawText(text, {}, glm::vec4{1});
    RecordingRenderer first, second;
    DrawListRenderer a(first, shaper), b(second, shaper);
    a.build(list);
    b.build(list);
    AURA_CHECK(first.textures.begin()->second.rgba == second.textures.begin()->second.rgba,
               "a second backend uploads glyphs whose dirty rectangle was already consumed");

    shaper.shape("B", TextStyle{}, kUnbounded, text);
    a.build(list);
    shaper.shape("C", TextStyle{}, kUnbounded, text);
    b.build(list);
    a.build(list);
    AURA_CHECK(first.textures.begin()->second.rgba == second.textures.begin()->second.rgba,
               "interleaved atlas consumers catch up on previously consumed updates");
    const usize uploads = first.uploads;
    a.build(list);
    AURA_CHECK(first.uploads == uploads, "an unchanged atlas requires no upload");

    RecordingRenderer unavailable;
    unavailable.failTextures = true;
    DrawListRenderer retry(unavailable, shaper);
    shaper.shape("D", TextStyle{}, kUnbounded, text);
    retry.build(list);
    AURA_CHECK(shaper.page(text.page)->dirty(), "failed texture creation keeps pending glyphs");
    unavailable.failTextures = false;
    retry.build(list);
    a.build(list);
    AURA_CHECK(first.textures.begin()->second.rgba == unavailable.textures.begin()->second.rgba,
               "retrying texture creation uploads the complete atlas");
}

void testInvisibleImageKeepsBatch()
{
    Harness harness;
    harness.list.begin(Rect::fromSize({}, {100, 40}));
    harness.list.fillRect(Rect::fromSize({}, {10, 10}), glm::vec4{1});
    harness.list.drawImage(Rect::fromSize({20.1f, 0}, {.1f, 10}), TextureHandle{99});
    harness.list.fillRect(Rect::fromSize({30, 0}, {10, 10}), glm::vec4{1});
    harness.backend.build(harness.list);
    AURA_CHECK(harness.backend.quadCount() == 2 && harness.backend.batchCount() == 1,
               "an image that snaps to zero pixels does not split visible geometry into batches");
}

void testRoundedCornersAreConstantGeometry()
{
    Harness harness;

    auto &square = harness.root.setContent<Column>().add<Widget>();
    square.layout().width = Length::px(200.0f);
    square.layout().height = Length::px(200.0f);
    square.style().fill(glm::vec4{1.0f}).rounded(0.0f);

    harness.frame();
    const usize squareQuads = harness.backend.quadCount();

    square.style().rounded(48.0f);
    harness.frame();
    const usize roundedQuads = harness.backend.quadCount();

    AURA_CHECK(squareQuads == 1, "a square rectangle is one quad");
    AURA_CHECK(roundedQuads > 1 && roundedQuads <= 12,
               "and a rounded one is a bounded nine-slice, not one span per scanline");

    square.style().rounded(8.0f);
    harness.frame();

    AURA_CHECK(harness.backend.quadCount() <= roundedQuads,
               "a smaller radius costs no more geometry than a larger one");
}

void testIdleFrameRebuildsNothing()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();
    page.add<Button>("Idle");

    harness.frame();
    const usize quads = harness.backend.quadCount();

    AURA_CHECK(!harness.root.needsPaint(), "a painted tree is clean");

    //! Nothing changed, so paint() declines to re-record and the backend
    //! re-submits the buffers it already built.
    AURA_CHECK(!harness.frame(), "an idle frame re-records nothing");

    AURA_CHECK(harness.backend.quadCount() == quads, "and submits the same geometry it already had");
    AURA_CHECK(harness.renderer.batches.size() == 1, "and still costs one draw call");
}

void testLostFrameIsRedrawnWhileIdle()
{
    RecordingRenderer renderer;
    UIView view(renderer);
    view.root().setContent<Column>().add<Label>("idle");

    view.update(0.0f);
    AURA_CHECK(view.needsDraw(), "a view draws its first frame");
    view.draw();
    view.update(0.0f);
    AURA_CHECK(!view.needsDraw(), "an idle view skips its frame");

    renderer.frameLost = true;
    view.update(0.0f);
    AURA_CHECK(view.needsDraw(), "a frame the renderer lost is drawn again under a clean tree");
    view.draw();

    renderer.frameLost = false;
    view.update(0.0f);
    AURA_CHECK(!view.needsDraw(), "and the view idles once the frame reaches the screen");
}

void testInvalidationTriggersRepaint()
{
    Harness harness;
    auto &label = harness.root.setContent<Column>().add<Label>("before");

    harness.frame();
    AURA_CHECK(!harness.root.needsPaint(), "clean after a frame");

    label.text = "after";
    AURA_CHECK(harness.root.needsPaint(), "a text change dirties the tree");

    harness.frame();
    AURA_CHECK(label.shaped().glyphs.size() == 5, "and the label re-shapes to the new string");
}

void testScrolledContentIsClipped()
{
    Harness harness({200.0f, 100.0f});

    auto &scroll = harness.root.setContent<ScrollView>();
    scroll.setSmooth(false);

    auto &column = scroll.setContent<Column>();
    for (int i = 0; i < 40; ++i)
    {
        auto &row = column.add<Widget>();
        row.layout().height = Length::px(30.0f);
        row.style().fill(glm::vec4{1.0f});
    }

    harness.frame();
    const usize visible = harness.list.size();

    AURA_CHECK(visible < 40, "rows scrolled out of view are never recorded");

    for (const DrawCommand &command : harness.list.commands())
    {
        if (command.type != DrawCommandType::Rect)
            continue;

        AURA_CHECK(!intersect(command.bounds, command.clip).empty(),
                   "every recorded command has something left after its clip");
        break;
    }
}

void testDisabledIsDrawnFaded()
{
    Harness harness;
    auto &button = harness.root.setContent<Column>().add<Button>("Fade");

    harness.frame();

    f32 enabledAlpha = 0.0f;
    for (const DrawCommand &command : harness.list.commands())
    {
        if (command.type == DrawCommandType::Rect && command.color.a > enabledAlpha)
            enabledAlpha = command.color.a;
    }

    button.setEnabled(false);
    harness.frame();

    f32 disabledAlpha = 0.0f;
    for (const DrawCommand &command : harness.list.commands())
    {
        if (command.type == DrawCommandType::Rect && command.color.a > disabledAlpha)
            disabledAlpha = command.color.a;
    }

    AURA_CHECK(disabledAlpha < enabledAlpha, "a disabled control is drawn faded");
}

void testScaleMultipliesVerticesNotLayout()
{
    Harness harness;
    auto &panel = harness.root.setContent<Column>().add<Widget>();
    panel.layout().width = Length::px(100.0f);
    panel.layout().height = Length::px(50.0f);
    panel.style().fill(glm::vec4{1.0f});

    harness.frame();
    const Rect logical = panel.bounds();

    harness.root.setScale(2.0f);
    harness.frame();

    AURA_CHECK(panel.bounds().width() == logical.width(), "raising the device-pixel ratio does not move the layout");
    AURA_CHECK(harness.renderer.batches.size() >= 1, "and the frame still submits");
}

void testAnimationDirtiesFrames()
{
    Harness harness;
    auto &button = harness.root.setContent<Column>().add<Button>("Hover");

    harness.frame();
    harness.root.pointerMoved(button.bounds().center());

    AURA_CHECK(harness.frame(0.016f) && harness.frame(0.016f), "a running hover transition re-records every frame");

    //! Long enough for the transition to finish, after which the tree settles.
    bool settled = false;
    for (int i = 0; i < 60 && !settled; ++i)
        settled = !harness.frame(0.016f);

    AURA_CHECK(settled, "and stops once it has run out");
}

void testAtlasSurvivesScaleAndFirstFrameMasks()
{
    Harness harness;
    auto &label = harness.root.setContent<Column>().add<Label>("Retained text");
    label.style().fill(glm::vec4{1.0f}).rounded(8.0f);
    harness.frame();
    const u32 pageIndex = label.shaped().page;
    FontAtlas *atlas = harness.shaper.page(pageIndex);
    AURA_CHECK(atlas && !atlas->dirty(), "first-frame corner masks have already been uploaded");
    harness.root.setScale(2.0f);
    harness.frame();
    AURA_CHECK(harness.shaper.page(pageIndex) == atlas, "DPI changes preserve pages referenced by retained glyph runs");
}

void testOutlineWithoutFillDrawsAnOutline()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();

    auto &box = page.add<Widget>();
    box.layout().width = Length::px(120.0f);
    box.layout().height = Length::px(40.0f);
    box.style().outline(glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}, 2.0f).rounded(10.0f);

    harness.frame();

    //! A border with no fill used to reach the backend as one rounded rect in
    //! the border colour: the two-pass form relies on the fill covering its
    //! middle, and there was no fill. Every focus ring drew as a solid block.
    AURA_CHECK(!harness.renderer.covers(box.bounds().center()), "an outline with no fill leaves its middle uncovered");

    AURA_CHECK(harness.renderer.covers({box.bounds().center().x, box.bounds().min.y + 1.0f}),
               "and still covers its own edge");
}

void testTranslucentFillDoesNotShowItsBorder()
{
    Harness harness;
    auto &page = harness.root.setContent<Column>();

    auto &box = page.add<Widget>();
    box.layout().width = Length::px(120.0f);
    box.layout().height = Length::px(40.0f);
    box.style()
        .fill(glm::vec4{1.0f, 1.0f, 1.0f, 0.07f})
        .outline(glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f)
        .rounded(10.0f);

    harness.frame();

    //! The border used to be laid down across the whole rectangle and then
    //! "covered" by the fill. A 7%-alpha fill covers nothing, so a bordered
    //! field read as a solid block of its border colour.
    bool solidBorderInside = false;

    for (usize base = 0; base + 4 <= harness.renderer.quads.size(); base += 4)
    {
        glm::vec2 low = harness.renderer.quads[base].pos;
        glm::vec2 high = low;

        for (usize corner = 0; corner < 4; ++corner)
        {
            low = glm::min(low, glm::vec2(harness.renderer.quads[base + corner].pos));
            high = glm::max(high, glm::vec2(harness.renderer.quads[base + corner].pos));
        }

        const glm::vec4 color = harness.renderer.quads[base].color;
        const bool isBorder = color.a > 0.5f && color.g > 0.5f && color.r < 0.5f;

        if (isBorder && high.x - low.x > 100.0f && high.y - low.y > 30.0f)
            solidBorderInside = true;
    }

    AURA_CHECK(!solidBorderInside, "a translucent fill does not leave its border painted underneath it");
}

void testStyleMetricsInvalidateLayout()
{
    Harness harness;
    auto &button = harness.root.setContent<Column>().add<Button>("Resize");
    harness.frame();
    button.style().height = 80;
    harness.frame();
    AURA_CHECK(button.bounds().height() >= 80, "style height changes remeasure controls");
}

void testRoundedOutlineCoverage()
{
    for (float scale : {1.f, 1.5f, 2.f})
    {
        Harness harness;
        harness.list.begin(Rect::fromSize({0, 0}, {120, 40}));
        harness.list.drawRect(Rect::fromSize({0, 0}, {120, 40}), glm::vec4{0}, glm::vec4{1}, 1.f, Corners::all(10.f));
        harness.backend.build(harness.list, scale);
        harness.backend.submit();
        AURA_CHECK(harness.renderer.alphaAt(glm::vec2{.5f, .5f} * scale) < .01f,
                   "a rounded outline leaves the square's outside corner transparent at every scale");
        AURA_CHECK(harness.renderer.alphaAt(glm::vec2{5.5f, 1.5f} * scale) > .2f,
                   "the border follows its curved corner instead of leaving a notch");
        AURA_CHECK(harness.renderer.alphaAt(glm::vec2{8.5f, 8.5f} * scale) < .01f,
                   "the corner mask does not paint inside the rounded ring");
        AURA_CHECK(harness.renderer.alphaAt(glm::vec2{60.5f, .5f} * scale) > .99f,
                   "the straight edge joins the curved outline");
    }
}

void testGlyphsStayOnDevicePixels()
{
    for (const f32 scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        RecordingRenderer renderer;
        AtlasTextShaper shaper;
        shaper.setScale(scale);
        ShapedText text;
        shaper.shape("Aura3D Wi", TextStyle{.pixelSize = 14.3f}, kUnbounded, text);
        DrawList list;
        list.begin(Rect::fromSize({}, {400, 100}));
        list.drawText(text, {10.35f, 6.7f}, {1, 1, 1, 1});
        DrawListRenderer backend(renderer, shaper);
        backend.build(list, scale);
        backend.submit();

        bool aligned = !renderer.quads.empty();
        for (const auto &vertex : renderer.quads)
            aligned &= glm::all(glm::lessThan(glm::abs(vertex.pos - glm::round(vertex.pos)), glm::vec3{0.001f}));
        AURA_CHECK(aligned, "fractional layout snaps glyph edges to device pixels");

        const auto *atlas = shaper.page(text.page);
        bool exact = atlas != nullptr;
        for (usize i = 0; atlas && i + 2 < renderer.quads.size(); i += 4)
        {
            const auto &top = renderer.quads[i];
            const auto &bottom = renderer.quads[i + 2];
            const glm::vec2 texels = (bottom.texCoord - top.texCoord) * glm::vec2{atlas->width(), atlas->height()};
            exact &= glm::all(glm::lessThan(glm::abs(glm::vec2(bottom.pos - top.pos) - texels), glm::vec2{0.001f}));
        }
        AURA_CHECK(exact, "each glyph atlas texel maps to one device pixel");
    }
}

} // namespace

int main()
{
    testRendererPrimitives();
    testScreenSpaceSharesClipConvention();
    testCoverageCompatibility();
    testSharedAtlasUploads();
    testInvisibleImageKeepsBatch();
    testGlyphsStayOnDevicePixels();
    testClipping();
    testSingleBatch();
    testFontSizesShareBatch();
    testRoundedCornersAreConstantGeometry();
    testIdleFrameRebuildsNothing();
    testLostFrameIsRedrawnWhileIdle();
    testInvalidationTriggersRepaint();
    testScrolledContentIsClipped();
    testDisabledIsDrawnFaded();
    testScaleMultipliesVerticesNotLayout();
    testAnimationDirtiesFrames();
    testAtlasSurvivesScaleAndFirstFrameMasks();
    testStyleMetricsInvalidateLayout();
    testOutlineWithoutFillDrawsAnOutline();
    testTranslucentFillDoesNotShowItsBorder();
    testRoundedOutlineCoverage();

    AURA_TEST_MAIN_RETURN();
}
