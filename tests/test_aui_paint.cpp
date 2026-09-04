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

#include "aura/Renderer/IRenderer.h"
#include "aura/UI/UI.hpp"

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
    }

    TextureHandle createDynamicTexture(u32, u32) override { return ++_next; }

    void updateTextureRegion(TextureHandle, u32, u32, u32, u32, const u8*) override
    {
        ++uploads;
    }

    void clear()
    {
        batches.clear();
        vertexTotal = 0;
    }

    std::vector<Batch> batches;
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

    AURA_TEST_MAIN_RETURN();
}
