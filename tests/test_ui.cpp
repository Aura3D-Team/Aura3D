/*
 * The built-in immediate-mode UI, driven headlessly.
 *
 * Everything aura3d::ui::Context does reaches the outside world through one
 * call -- IRenderer::drawBatch2D() -- so a stub renderer that records that call
 * is enough to test the whole module end to end: layout, hit-testing, the
 * hot/active state machine and clipping, without a window, a GPU or a driver.
 *
 * Two properties in particular are worth pinning down here because nothing
 * about them fails loudly in a running application:
 *
 *  - the UI must leave as exactly one batch. That is the entire justification
 *    for FontAtlas::solidTexelUv(); if a change made rectangles stop sampling
 *    the atlas, the UI would still look perfectly correct while quietly costing
 *    a draw call per widget.
 *
 *  - a click is a press *and* a release over the same widget. Pressing a button
 *    and dragging off it must not activate it, and neither must the release
 *    alone, which is what a naive "is the button down over this rectangle"
 *    test would report.
 */

#include <string>
#include <vector>

#include "aura/Renderer/IRenderer.h"
#include "aura/UI/AuraUI.h"

#include "TestUtils.h"

using namespace aura3d;

namespace {

/**
 * @brief An IRenderer that records 2D batches and does nothing else.
 *
 * Texture handles are handed out 1-based, matching the real backends'
 * convention, so isValidHandle() accepts them and the UI considers itself
 * usable.
 */
class RecordingRenderer final : public IRenderer {
public:
    /// One captured drawBatch2D() call.
    struct Batch {
        std::vector<gfx::Vertex2D> vertices;
        std::vector<u32> indices;
        TextureHandle texture;
    };

    RecordingRenderer() : IRenderer(wma::WindowDetails{}) {}

    void drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                     std::span<const u32> indices,
                     TextureHandle texture) override
    {
        batches.push_back({{vertices.begin(), vertices.end()},
                           {indices.begin(), indices.end()},
                           texture});
    }

    TextureHandle createDynamicTexture(u32, u32) override { return ++_nextTexture; }

    void updateTextureRegion(TextureHandle, u32, u32, u32, u32, const u8*) override {}

    //! Everything below is inert: the UI never reaches for it.
    void initialize(AuraSettings*, const JobSystem*) override {}
    void handleWindowChanges() override {}
    void cleanup() override {}
    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&&) override { return {}; }
    IndexBufferHandle createIndexBuffer(std::vector<u16>&&) override { return {}; }
    IndexBufferHandle createIndexBuffer(std::vector<u32>&&) override { return {}; }
    TextureHandle createSolidColorTexture(u8, u8, u8, u8) override { return {}; }
    TextureHandle createTextureFromPixels(const u8*, u32, u32) override { return {}; }
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
    RendererChoice getBackendType() const override { return RendererChoice::SOFTWARE; }

    std::vector<Batch> batches;

protected:
    void createWindow(const char*, const wma::WindowBackend&) override {}

private:
    TextureHandle _nextTexture{0};
};

constexpr glm::vec2 kPanelOrigin{100.0f, 100.0f};
constexpr float kPanelWidth = 200.0f;

/// Cursor placed over the first widget row of a panel at kPanelOrigin.
[[nodiscard]] glm::vec2 firstRowPoint(const ui::Style& style)
{
    return {kPanelOrigin.x + kPanelWidth * 0.5f,
            kPanelOrigin.y + style.rowHeight + style.padding + style.rowHeight * 0.5f};
}

void testSingleBatchPerFrame()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    gui.newFrame(ui::Input{});

    AURA_CHECK(gui.beginPanel("Panel", kPanelOrigin, kPanelWidth),
               "beginPanel opens the panel");

    gui.label("A label");
    bool toggle = true;
    gui.checkbox("A checkbox", toggle);
    float value = 0.5f;
    gui.sliderFloat("A slider", value, 0.0f, 1.0f);
    gui.separator();
    (void)gui.button("A button");
    gui.endPanel();

    gui.render();

    AURA_CHECK(renderer.batches.size() == 1,
               "a whole panel of mixed rectangles and text is one batch");

    if (renderer.batches.empty())
        return;

    const auto& batch = renderer.batches.front();

    AURA_CHECK(!batch.indices.empty(), "the batch carries geometry");
    AURA_CHECK(batch.indices.size() % 6 == 0, "the batch is whole quads");
    AURA_CHECK(isValidHandle(batch.texture), "the batch is drawn with the glyph atlas");

    for (const u32 index : batch.indices)
    {
        if (index >= batch.vertices.size())
        {
            AURA_CHECK(false, "every index addresses a vertex in the batch");
            return;
        }
    }
    AURA_CHECK(true, "every index addresses a vertex in the batch");
}

void testEmptyFrameDrawsNothing()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    gui.newFrame(ui::Input{});
    gui.render();

    AURA_CHECK(renderer.batches.empty(), "a frame with no panel submits no batch");
}

void testWidgetsOutsideAPanelAreIgnored()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    gui.newFrame(ui::Input{});
    gui.label("stray");
    bool toggle = false;
    AURA_CHECK(!gui.button("stray"), "a button outside any panel reports no click");
    AURA_CHECK(!gui.checkbox("stray", toggle), "a checkbox outside any panel reports no change");
    AURA_CHECK(!toggle, "a checkbox outside any panel leaves its value alone");
    gui.render();

    AURA_CHECK(renderer.batches.empty(), "widgets outside any panel draw nothing");
}

/**
 * @brief Runs one frame containing a single button, and reports its result.
 *
 * The UI is immediate: the widget has to be resubmitted every frame for the
 * press it owns to be carried forward, which is exactly what this models.
 */
[[nodiscard]] bool buttonFrame(ui::Context& gui, glm::vec2 mouse, bool mouseDown)
{
    gui.newFrame(ui::Input{mouse, mouseDown});

    bool clicked = false;
    if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
    {
        clicked = gui.button("Press me");
        gui.endPanel();
    }

    gui.render();
    return clicked;
}

void testButtonNeedsPressAndRelease()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    const glm::vec2 overButton = firstRowPoint(gui.style());
    const glm::vec2 away{0.0f, 0.0f};

    AURA_CHECK(!buttonFrame(gui, overButton, false), "hovering alone is not a click");
    AURA_CHECK(!buttonFrame(gui, overButton, true), "pressing alone is not a click");
    AURA_CHECK(buttonFrame(gui, overButton, false), "releasing over the button clicks it");
    AURA_CHECK(!buttonFrame(gui, overButton, false), "the click is reported once, not held");

    //! A press dragged off the widget before release is a cancel, and the
    //! release that follows must not click the button it happens to end over.
    AURA_CHECK(!buttonFrame(gui, overButton, true), "press over the button arms it");
    AURA_CHECK(!buttonFrame(gui, away, true), "dragging off the button holds it armed");
    AURA_CHECK(!buttonFrame(gui, away, false), "releasing off the button cancels the click");

    //! And a release with no press before it -- a click begun elsewhere, or
    //! begun before the UI existed -- must not count either.
    AURA_CHECK(!buttonFrame(gui, away, true), "press away from the button");
    AURA_CHECK(!buttonFrame(gui, overButton, false),
               "a release over the button without a press on it is not a click");
}

void testCheckboxTogglesOnce()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    const glm::vec2 overBox = firstRowPoint(gui.style());
    bool value = false;

    auto frame = [&](bool mouseDown) {
        gui.newFrame(ui::Input{overBox, mouseDown});
        bool changed = false;
        if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
        {
            changed = gui.checkbox("Toggle", value);
            gui.endPanel();
        }
        gui.render();
        return changed;
    };

    (void)frame(false);
    AURA_CHECK(!frame(true) && !value, "pressing does not toggle yet");
    AURA_CHECK(frame(false) && value, "releasing toggles the checkbox on");
    AURA_CHECK(!frame(false) && value, "the toggle does not repeat while idle");

    AURA_CHECK(!frame(true), "pressing again does not toggle yet");
    AURA_CHECK(frame(false) && !value, "the next click toggles it back off");
}

void testSliderTracksTheCursor()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    const ui::Style& style = gui.style();
    const float rowY = firstRowPoint(style).y;
    const float trackLeft = kPanelOrigin.x + style.padding;
    const float trackWidth = kPanelWidth - 2.0f * style.padding;

    float value = 0.0f;

    auto frame = [&](float x, bool mouseDown) {
        gui.newFrame(ui::Input{{x, rowY}, mouseDown});
        bool changed = false;
        if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
        {
            changed = gui.sliderFloat("Value", value, 0.0f, 10.0f);
            gui.endPanel();
        }
        gui.render();
        return changed;
    };

    (void)frame(trackLeft, false);

    AURA_CHECK(frame(trackLeft + trackWidth * 0.5f, true) && std::abs(value - 5.0f) < 0.01f,
               "pressing mid-track sets the value to the midpoint");

    //! Dragging past the end pins the value to the bound instead of running
    //! past it, and keeps tracking even though the cursor has left the track.
    (void)frame(trackLeft + trackWidth * 2.0f, true);
    AURA_CHECK(std::abs(value - 10.0f) < 0.001f, "dragging past the end clamps to the maximum");

    (void)frame(trackLeft - trackWidth, true);
    AURA_CHECK(std::abs(value) < 0.001f, "dragging past the start clamps to the minimum");

    //! After release the slider must ignore the cursor entirely.
    (void)frame(trackLeft + trackWidth * 0.75f, false);
    const float afterRelease = value;
    (void)frame(trackLeft + trackWidth * 0.25f, false);
    AURA_CHECK(value == afterRelease, "a slider does not follow the cursor once released");
}

void testOutOfRangeValueIsClamped()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    float value = 99.0f;

    gui.newFrame(ui::Input{});
    if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
    {
        const bool changed = gui.sliderFloat("Value", value, 0.0f, 1.0f);
        AURA_CHECK(changed, "a value outside the slider's range is reported as changed");
        gui.endPanel();
    }
    gui.render();

    AURA_CHECK(value == 1.0f, "a value outside the slider's range is clamped into it");

    //! A degenerate range has no meaningful position to report, so the widget
    //! declines rather than dividing by zero.
    float untouched = 5.0f;
    gui.newFrame(ui::Input{});
    if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
    {
        AURA_CHECK(!gui.sliderFloat("Empty", untouched, 1.0f, 1.0f),
                   "an empty range reports no change");
        gui.endPanel();
    }
    gui.render();

    AURA_CHECK(untouched == 5.0f, "an empty range leaves the value alone");
}

void testContentIsClippedToThePanel()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    gui.newFrame(ui::Input{});
    if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
    {
        //! Far wider than the panel: without clipping its tail would spill out
        //! across the rest of the screen.
        gui.label(std::string(400, 'W'));
        gui.endPanel();
    }
    gui.render();

    if (renderer.batches.empty())
    {
        AURA_CHECK(false, "the clipped panel submitted a batch");
        return;
    }

    const float right = kPanelOrigin.x + kPanelWidth;
    bool withinPanel = true;
    for (const gfx::Vertex2D& vertex : renderer.batches.front().vertices)
    {
        if (vertex.pos.x < kPanelOrigin.x - 0.001f || vertex.pos.x > right + 0.001f)
        {
            withinPanel = false;
            break;
        }
    }

    AURA_CHECK(withinPanel, "text far wider than its panel is clipped to it");
}

void testPanelGrowsWithItsContent()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    auto panelHeight = [&](int labelCount) {
        renderer.batches.clear();
        gui.newFrame(ui::Input{});
        if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
        {
            for (int i = 0; i < labelCount; ++i)
                gui.label("row");
            gui.endPanel();
        }
        gui.render();

        //! The background is the first quad emitted, and endPanel() rewrites
        //! its bottom edge once the content's height is known.
        return renderer.batches.empty() ? 0.0f
                                        : renderer.batches.front().vertices[2].pos.y - kPanelOrigin.y;
    };

    const float oneRow = panelHeight(1);
    const float fourRows = panelHeight(4);

    AURA_CHECK(oneRow > 0.0f, "a panel has a height");
    AURA_CHECK(fourRows > oneRow, "a panel's background grows to fit its content");

    const float rowStride = gui.style().rowHeight + gui.style().itemSpacing;
    AURA_CHECK(std::abs((fourRows - oneRow) - 3.0f * rowStride) < 0.01f,
               "the background grows by exactly one row stride per row");
}

void testWantsMouseFollowsThePanel()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    auto frame = [&](glm::vec2 mouse) {
        gui.newFrame(ui::Input{mouse, false});
        if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
        {
            gui.label("row");
            gui.endPanel();
        }
        gui.render();
        return gui.wantsMouse();
    };

    (void)frame(kPanelOrigin + glm::vec2{10.0f, 10.0f});
    AURA_CHECK(frame(kPanelOrigin + glm::vec2{10.0f, 10.0f}),
               "the UI claims the mouse while the cursor is over a panel");

    (void)frame({0.0f, 0.0f});
    AURA_CHECK(!frame({0.0f, 0.0f}),
               "the UI releases the mouse when the cursor leaves every panel");
}

void testPanelPositionSurvivesFrames()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    auto topLeft = [&](glm::vec2 defaultPosition) {
        renderer.batches.clear();
        gui.newFrame(ui::Input{});
        if (gui.beginPanel("Panel", defaultPosition, kPanelWidth))
            gui.endPanel();
        gui.render();
        return renderer.batches.empty() ? glm::vec2{-1.0f} : renderer.batches.front().vertices[0].pos;
    };

    const glm::vec2 first = topLeft(kPanelOrigin);
    AURA_CHECK(first == kPanelOrigin, "a panel is placed at its default position first");

    //! defaultPosition places the panel once and is ignored afterwards, so a
    //! panel a user has dragged is not yanked back every frame.
    const glm::vec2 second = topLeft({500.0f, 500.0f});
    AURA_CHECK(second == kPanelOrigin, "a placed panel ignores later default positions");
}

void testTitleBarDragMovesTheWholePanel()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    //! Middle of the title bar, which is the panel's top row.
    const glm::vec2 overTitle{kPanelOrigin.x + kPanelWidth * 0.5f,
                              kPanelOrigin.y + gui.style().rowHeight * 0.5f};

    auto frame = [&](glm::vec2 mouse, bool mouseDown) {
        renderer.batches.clear();
        gui.newFrame(ui::Input{mouse, mouseDown});
        if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
        {
            gui.label("row");
            gui.endPanel();
        }
        gui.render();
    };

    frame(overTitle, false);
    frame(overTitle, true);

    const glm::vec2 delta{60.0f, 35.0f};
    frame(overTitle + delta, true);

    if (renderer.batches.empty())
    {
        AURA_CHECK(false, "the dragged panel submitted a batch");
        return;
    }

    const auto& vertices = renderer.batches.front().vertices;

    //! The background is the first quad, the title bar the second. Both must
    //! have moved by the same amount: hit-testing the drag against where the
    //! bar *was* while drawing it there too leaves the title trailing the body.
    const glm::vec2 background = vertices[0].pos;
    const glm::vec2 title = vertices[4].pos;

    AURA_CHECK(background == kPanelOrigin + delta,
               "dragging the title bar moves the panel by the cursor's delta");
    AURA_CHECK(title == background,
               "the title bar moves with the panel body rather than trailing it");

    //! The grab offset is held for the whole drag, so the panel tracks the
    //! cursor rather than re-centring on it.
    frame(overTitle + delta * 2.0f, true);
    AURA_CHECK(renderer.batches.front().vertices[0].pos == kPanelOrigin + delta * 2.0f,
               "the panel keeps its grab offset across the drag");

    //! And releasing leaves it where it was dropped.
    frame(overTitle + delta * 2.0f, false);
    frame({0.0f, 0.0f}, false);
    AURA_CHECK(renderer.batches.front().vertices[0].pos == kPanelOrigin + delta * 2.0f,
               "a dropped panel stays where it was left");
}

} // namespace

int main()
{
    testSingleBatchPerFrame();
    testEmptyFrameDrawsNothing();
    testWidgetsOutsideAPanelAreIgnored();
    testButtonNeedsPressAndRelease();
    testCheckboxTogglesOnce();
    testSliderTracksTheCursor();
    testOutOfRangeValueIsClamped();
    testContentIsClippedToThePanel();
    testPanelGrowsWithItsContent();
    testWantsMouseFollowsThePanel();
    testPanelPositionSurvivesFrames();
    testTitleBarDragMovesTheWholePanel();

    AURA_TEST_MAIN_RETURN();
}
