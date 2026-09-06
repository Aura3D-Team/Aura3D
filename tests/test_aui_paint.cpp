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
 * A stub IRenderer that records drawBatch2D() is the whole harness -- no
 * window, no GPU, no driver.
 */

#include <algorithm>
#include <span>
#include <vector>
#include <unordered_map>

#include "aura/Renderer/IRenderer.h"
#include "aura/UI/UI.hpp"
#include "aura/Core/AuraFont/FontAtlas.h"

#include "TestUtils.h"

using namespace aura3d;
using namespace aura3d::ui;

namespace {

/// An IRenderer that records 2D batches and does nothing else. Texture handles
/// are handed out 1-based, matching the real backends, so isValidHandle()
/// accepts them and the UI considers itself usable.
class RecordingRenderer final : public IRenderer {
public:
    struct Batch {
        usize vertexCount = 0;
        usize indexCount = 0;
        TextureHandle texture;
    };

    RecordingRenderer() : IRenderer(wma::WindowDetails{}) {}

    void drawBatch2D(std::span<const gfx::Vertex2D> vertices, std::span<const u32> indices,
                     TextureHandle texture) override
    {
        //! Every index must address a vertex of the slice it was submitted
        //! with. The backend hands each batch its own vertex range and a
        //! shared 0-based index pattern, and getting that wrong would read
        //! out of bounds on a real driver rather than here.
        for (const u32 index : indices)
            indicesInRange &= index < vertices.size();

        batches.push_back({vertices.size(), indices.size(), texture});
        vertexTotal += vertices.size();

        //! Kept so a test can ask what the backend actually covered, not just
        //! how much it submitted.
        quads.insert(quads.end(), vertices.begin(), vertices.end());
    }

    struct Texture { u32 width, height; std::vector<u8> rgba; };
    std::unordered_map<TextureHandle, Texture> textures;
    TextureHandle createDynamicTexture(u32 width, u32 height) override {
        const auto handle = ++_next;
        textures.emplace(handle, Texture{width, height, std::vector<u8>(usize(width) * height * 4)});
        return handle;
    }

    void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8* rgba) override
    {
        ++uploads;
        auto& texture = textures.at(handle);
        for (u32 row = 0; row < height; ++row)
            std::copy_n(rgba + usize(row) * width * 4, usize(width) * 4,
                        texture.rgba.data() + (usize(y + row) * texture.width + x) * 4);
    }

    // Sample submitted geometry AND the uploaded coverage texture. Geometry
    // alone cannot distinguish a rounded ring from its rectangular mask quad.
    float alphaAt(glm::vec2 point) const {
        usize start = 0; float alpha = 0;
        for (const auto& batch : batches) {
            const auto& texture = textures.at(batch.texture);
            for (usize base = start; base < start + batch.vertexCount; base += 4) {
                const auto& a = quads[base]; const auto& b = quads[base + 2];
                if (point.x < a.pos.x || point.y < a.pos.y || point.x >= b.pos.x || point.y >= b.pos.y) continue;
                const auto uv = a.texCoord + (b.texCoord - a.texCoord) * ((point - a.pos) / (b.pos - a.pos));
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
                low = glm::min(low, quads[base + corner].pos);
                high = glm::max(high, quads[base + corner].pos);
                alpha = std::max(alpha, quads[base + corner].color.a);
            }

            if (alpha > 0.0f && point.x > low.x && point.x < high.x && point.y > low.y &&
                point.y < high.y)
                return true;
        }

        return false;
    }

    std::vector<Batch> batches;
    std::vector<gfx::Vertex2D> quads;
    usize vertexTotal = 0;
    usize uploads = 0;
    bool indicesInRange = true;

    // Everything below is inert: the UI never calls it.
    void beginFrame() override {}
    void beginRenderPass() override {}
    void endRenderPass() override {}
    void endFrame() override {}
    void setTransform(const gfx::TransformUBO&) override {}
    void bindVertexBuffer(VertexBufferHandle) override {}
    void bindIndexBuffer(IndexBufferHandle) override {}
    void bindTexture(TextureHandle) override {}
    void drawIndexed(u32, u32) override {}
    void draw(u32, u32) override {}
    void setClearColor(f32, f32, f32, f32) override {}
    wma::IWindowManager* getWindowManager() override { return nullptr; }

    TextureHandle createSolidColorTexture(u8, u8, u8, u8) override { return {}; }
    TextureHandle createTextureFromPixels(const u8*, u32, u32) override { return {}; }

    void initialize(AuraSettings*, const JobSystem*) override {}
    void handleWindowChanges() override {}
    void cleanup() override {}
    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&&) override { return {}; }
    IndexBufferHandle createIndexBuffer(std::vector<u16>&&) override { return {}; }
    IndexBufferHandle createIndexBuffer(std::vector<u32>&&) override { return {}; }
    RendererChoice getBackendType() const override { return RendererChoice::SOFTWARE; }
    void createWindow(const char*, const wma::WindowBackend&) override {}

private:
    TextureHandle _next{0};
};

struct Harness {
    RecordingRenderer renderer;
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};
    DrawList list;
    DrawListRenderer backend{renderer, shaper};

    explicit Harness(glm::vec2 size = {400.0f, 300.0f}) { root.resize(size); }

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

[[nodiscard]] usize countType(const DrawList& list, DrawCommandType type)
{
    return static_cast<usize>(std::ranges::count_if(
        list.commands(), [type](const DrawCommand& command) { return command.type == type; }));
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
    auto& page = harness.root.setContent<Column>();
    page.layout().padding = Thickness::all(12.0f);
    page.setSpacing(8.0f);

    page.add<Label>("Renderer");
    page.add<Button>("Reload shaders");
    page.add<CheckBox>("Wireframe");
    page.add<Slider>(0.0f, 4.0f);
    page.add<ProgressBar>(0.5f);
    page.add<Separator>();

    harness.frame();

    AURA_CHECK(harness.renderer.batches.size() == 1,
               "a UI of surfaces and text at one size is exactly one draw call");
    AURA_CHECK(harness.renderer.indicesInRange, "and every index addresses its own batch");

    AURA_CHECK(countType(harness.list, DrawCommandType::Text) == 3,
               "with one text command per labelled control and no more");
}

void testSecondFontSizeCostsOneMoreBatch()
{
    Harness harness;
    auto& page = harness.root.setContent<Column>();

    page.add<Label>("Heading").setFontSize(28.0f);
    page.add<Label>("Body").setFontSize(13.0f);

    harness.frame();

    //! Two rasterization sizes are two atlas pages, so two textures. What
    //! matters is that it is two and not one per widget.
    AURA_CHECK(harness.renderer.batches.size() <= 3,
               "a second text size adds a batch, not a batch per widget");
    AURA_CHECK(harness.renderer.uploads > 0, "and its glyph page is uploaded");
}

void testRoundedCornersAreConstantGeometry()
{
    Harness harness;

    auto& square = harness.root.setContent<Column>().add<Widget>();
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
    auto& page = harness.root.setContent<Column>();
    page.add<Button>("Idle");

    harness.frame();
    const usize quads = harness.backend.quadCount();

    AURA_CHECK(!harness.root.needsPaint(), "a painted tree is clean");

    //! Nothing changed, so paint() declines to re-record and the backend
    //! re-submits the buffers it already built.
    AURA_CHECK(!harness.frame(), "an idle frame re-records nothing");

    AURA_CHECK(harness.backend.quadCount() == quads,
               "and submits the same geometry it already had");
    AURA_CHECK(harness.renderer.batches.size() == 1, "and still costs one draw call");
}

void testInvalidationTriggersRepaint()
{
    Harness harness;
    auto& label = harness.root.setContent<Column>().add<Label>("before");

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

    auto& scroll = harness.root.setContent<ScrollView>();
    scroll.setSmooth(false);

    auto& column = scroll.setContent<Column>();
    for (int i = 0; i < 40; ++i)
    {
        auto& row = column.add<Widget>();
        row.layout().height = Length::px(30.0f);
        row.style().fill(glm::vec4{1.0f});
    }

    harness.frame();
    const usize visible = harness.list.size();

    AURA_CHECK(visible < 40, "rows scrolled out of view are never recorded");

    for (const DrawCommand& command : harness.list.commands())
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
    auto& button = harness.root.setContent<Column>().add<Button>("Fade");

    harness.frame();

    f32 enabledAlpha = 0.0f;
    for (const DrawCommand& command : harness.list.commands())
    {
        if (command.type == DrawCommandType::Rect && command.color.a > enabledAlpha)
            enabledAlpha = command.color.a;
    }

    button.setEnabled(false);
    harness.frame();

    f32 disabledAlpha = 0.0f;
    for (const DrawCommand& command : harness.list.commands())
    {
        if (command.type == DrawCommandType::Rect && command.color.a > disabledAlpha)
            disabledAlpha = command.color.a;
    }

    AURA_CHECK(disabledAlpha < enabledAlpha, "a disabled control is drawn faded");
}

void testScaleMultipliesVerticesNotLayout()
{
    Harness harness;
    auto& panel = harness.root.setContent<Column>().add<Widget>();
    panel.layout().width = Length::px(100.0f);
    panel.layout().height = Length::px(50.0f);
    panel.style().fill(glm::vec4{1.0f});

    harness.frame();
    const Rect logical = panel.bounds();

    harness.root.setScale(2.0f);
    harness.frame();

    AURA_CHECK(panel.bounds().width() == logical.width(),
               "raising the device-pixel ratio does not move the layout");
    AURA_CHECK(harness.renderer.batches.size() >= 1, "and the frame still submits");
}

void testAnimationDirtiesFrames()
{
    Harness harness;
    auto& button = harness.root.setContent<Column>().add<Button>("Hover");

    harness.frame();
    harness.root.pointerMoved(button.bounds().center());

    AURA_CHECK(harness.frame(0.016f) && harness.frame(0.016f),
               "a running hover transition re-records every frame");

    //! Long enough for the transition to finish, after which the tree settles.
    bool settled = false;
    for (int i = 0; i < 60 && !settled; ++i)
        settled = !harness.frame(0.016f);

    AURA_CHECK(settled, "and stops once it has run out");
}

void testAtlasSurvivesScaleAndFirstFrameMasks()
{
    Harness harness;
    auto& label = harness.root.setContent<Column>().add<Label>("Retained text");
    label.style().fill(glm::vec4{1.0f}).rounded(8.0f);
    harness.frame();
    const u32 pageIndex = label.shaped().page;
    FontAtlas* atlas = harness.shaper.page(pageIndex);
    AURA_CHECK(atlas && !atlas->takeDirtyUpload(), "first-frame corner masks have already been uploaded");
    harness.root.setScale(2.0f);
    harness.frame();
    AURA_CHECK(harness.shaper.page(pageIndex) == atlas,
               "DPI changes preserve pages referenced by retained glyph runs");
}

void testOutlineWithoutFillDrawsAnOutline()
{
    Harness harness;
    auto& page = harness.root.setContent<Column>();

    auto& box = page.add<Widget>();
    box.layout().width = Length::px(120.0f);
    box.layout().height = Length::px(40.0f);
    box.style().outline(glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}, 2.0f).rounded(10.0f);

    harness.frame();

    //! A border with no fill used to reach the backend as one rounded rect in
    //! the border colour: the two-pass form relies on the fill covering its
    //! middle, and there was no fill. Every focus ring drew as a solid block.
    AURA_CHECK(!harness.renderer.covers(box.bounds().center()),
               "an outline with no fill leaves its middle uncovered");

    AURA_CHECK(harness.renderer.covers({box.bounds().center().x, box.bounds().min.y + 1.0f}),
               "and still covers its own edge");
}

void testTranslucentFillDoesNotShowItsBorder()
{
    Harness harness;
    auto& page = harness.root.setContent<Column>();

    auto& box = page.add<Widget>();
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
            low = glm::min(low, harness.renderer.quads[base + corner].pos);
            high = glm::max(high, harness.renderer.quads[base + corner].pos);
        }

        const glm::vec4 color = harness.renderer.quads[base].color;
        const bool isBorder = color.a > 0.5f && color.g > 0.5f && color.r < 0.5f;

        if (isBorder && high.x - low.x > 100.0f && high.y - low.y > 30.0f)
            solidBorderInside = true;
    }

    AURA_CHECK(!solidBorderInside,
               "a translucent fill does not leave its border painted underneath it");
}

void testStyleMetricsInvalidateLayout()
{
    Harness harness;
    auto& button = harness.root.setContent<Column>().add<Button>("Resize");
    harness.frame();
    button.style().height = 80;
    harness.frame();
    AURA_CHECK(button.bounds().height() >= 80, "style height changes remeasure controls");
}

void testRoundedOutlineCoverage()
{
    for (float scale : {1.f, 1.5f, 2.f}) {
        Harness harness;
        harness.list.begin(Rect::fromSize({0, 0}, {120, 40}));
        harness.list.drawRect(Rect::fromSize({0, 0}, {120, 40}), glm::vec4{0},
                              glm::vec4{1}, 1.f, Corners::all(10.f));
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

} // namespace

int main()
{
    testClipping();
    testSingleBatch();
    testSecondFontSizeCostsOneMoreBatch();
    testRoundedCornersAreConstantGeometry();
    testIdleFrameRebuildsNothing();
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
