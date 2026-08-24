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
        return gui.isCapturingMouse();
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

//! An Input carrying one typed string, as the platform's text stream delivers it.
[[nodiscard]] ui::Input typing(std::string_view text)
{
    ui::Input input;
    input.text = text;
    return input;
}

//! An Input carrying one key press.
[[nodiscard]] ui::Input pressing(wma::Key key, bool shift = false, bool ctrl = false)
{
    ui::Input input;
    wma::KeyModifiers mods{};
    mods.shift = shift;
    mods.ctrl = ctrl;
    input.keys.push_back(ui::KeyPress{key, mods});
    return input;
}

/**
 * Runs one frame containing a single focused text field, and returns whether
 * the field reported a change. Focus is taken on the first frame with
 * setKeyboardFocusHere() so no click is needed.
 */
bool textFieldFrame(ui::Context& gui, const ui::Input& input, std::string& value, bool focusFirst)
{
    gui.newFrame(input);

    bool changed = false;

    (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
    if (focusFirst)
        gui.setKeyboardFocusHere();
    changed = gui.inputText("Field", value);
    gui.endPanel();
    
    gui.render();
    
    return changed;
}

void testTextFieldEditing()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    std::string value;

    //! First frame claims focus; nothing typed yet.
    (void)textFieldFrame(gui, ui::Input{}, value, /*focusFirst=*/true);

    AURA_CHECK(gui.isCapturingKeyboard(), "a focused text field claims the keyboard");
    AURA_CHECK(gui.isCapturingTextInput(), "a focused text field asks for platform text input");

    AURA_CHECK(textFieldFrame(gui, typing("Hi"), value, false), "typing reports a change");
    AURA_CHECK(value == "Hi", "typed text is inserted at the caret");

    //! Multi-byte input must survive intact: the caret is a byte offset, so an
    //! off-by-one here would split the sequence and corrupt the string.
    (void)textFieldFrame(gui, typing("\xC3\xA9"), value, false);
    AURA_CHECK(value == "Hi\xC3\xA9", "multi-byte UTF-8 is inserted whole");

    //! One Backspace must remove the whole two-byte character, not one byte.
    (void)textFieldFrame(gui, pressing(wma::KEY_BACKSPACE), value, false);
    AURA_CHECK(value == "Hi", "backspace deletes a whole UTF-8 character");

    (void)textFieldFrame(gui, pressing(wma::KEY_LEFT), value, false);
    (void)textFieldFrame(gui, typing("e"), value, false);
    AURA_CHECK(value == "Hei", "the caret moves left and text inserts there");

    (void)textFieldFrame(gui, pressing(wma::KEY_HOME), value, false);
    (void)textFieldFrame(gui, pressing(wma::KEY_DELETE), value, false);
    AURA_CHECK(value == "ei", "Home then Delete removes the first character");

    //! Ctrl+A then typing replaces the whole contents.
    (void)textFieldFrame(gui, pressing(wma::KEY_A, false, true), value, false);
    (void)textFieldFrame(gui, typing("X"), value, false);
    AURA_CHECK(value == "X", "typing over a select-all replaces the contents");
}

void testTextFieldEscapeReverts()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    std::string value = "original";

    (void)textFieldFrame(gui, ui::Input{}, value, /*focusFirst=*/true);
    (void)textFieldFrame(gui, typing("!"), value, false);
    AURA_CHECK(value == "original!", "the edit is applied while focused");

    (void)textFieldFrame(gui, pressing(wma::KEY_ESCAPE), value, false);
    AURA_CHECK(value == "original", "Escape reverts the edit");
    AURA_CHECK(!gui.isCapturingKeyboard(), "Escape releases keyboard focus");
}

void testTextFieldRespectsMaxBytes()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    std::string value;

    gui.newFrame(ui::Input{});
    
    (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
    gui.setKeyboardFocusHere();
    (void)gui.inputText("Field", value, /*maxBytes=*/4);
    gui.endPanel();

    gui.render();

    gui.newFrame(typing("abcd"));
    
    (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
    (void)gui.inputText("Field", value, 4);
    gui.endPanel();
    
    gui.render();
    AURA_CHECK(value == "abcd", "input up to the limit is accepted");

    gui.newFrame(typing("e"));
    
    (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
    (void)gui.inputText("Field", value, 4);
    gui.endPanel();
    
    gui.render();
    AURA_CHECK(value == "abcd", "input past the limit is dropped, not truncated");
}

void testTabMovesFocusBetweenWidgets()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    std::string first;
    std::string second;

    //! Emits two fields; the first claims focus on frame one.
    const auto frame = [&](const ui::Input& input, bool focusFirst) {
        gui.newFrame(input);

        (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
        if (focusFirst)
            gui.setKeyboardFocusHere();
        (void)gui.inputText("First", first);
        (void)gui.inputText("Second", second);
        gui.endPanel();

        gui.render();
    };

    frame(ui::Input{}, true);
    frame(typing("a"), false);
    AURA_CHECK(first == "a" && second.empty(), "typing goes to the focused field");

    //! Tab moves focus on; the next frame's text must land in the second field.
    frame(pressing(wma::KEY_TAB), false);
    frame(typing("b"), false);
    AURA_CHECK(first == "a" && second == "b", "Tab moves focus to the next field");

    //! Shift+Tab moves back.
    frame(pressing(wma::KEY_TAB, /*shift=*/true), false);
    frame(typing("c"), false);
    AURA_CHECK(first == "ac" && second == "b", "Shift+Tab moves focus back");
}

void testKeyboardActivatesButtonAndCheckbox()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    bool toggled = false;
    bool clicked = false;

    const auto frame = [&](const ui::Input& input, bool focusFirst) {
        gui.newFrame(input);
        
        (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
        if (focusFirst)
            gui.setKeyboardFocusHere();
        clicked = gui.button("Go");
        (void)gui.checkbox("Flag", toggled);
        gui.endPanel();
    
        gui.render();
    };

    frame(ui::Input{}, true);
    frame(pressing(wma::KEY_ENTER), false);
    AURA_CHECK(clicked, "Enter activates the focused button");

    frame(pressing(wma::KEY_TAB), false);
    frame(pressing(wma::KEY_SPACE), false);
    AURA_CHECK(toggled, "Space toggles the focused checkbox");
}

void testScrollRegionClipsAndScrolls()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    constexpr float kViewportHeight = 60.0f;

    //! Emits far more rows than fit, so the region genuinely overflows.
    const auto frame = [&](const ui::Input& input) {
        gui.newFrame(input);
        (void)gui.beginPanel("Panel", kPanelOrigin, kPanelWidth);
        (void)gui.beginScroll("List", kViewportHeight);
        for (int i = 0; i < 20; ++i)
            gui.label("row");
        gui.endScroll();
        gui.endPanel();
        gui.render();
    };

    frame(ui::Input{});

    AURA_CHECK(renderer.batches.size() == 1, "a scroll region stays within one batch");

    //! The panel must be sized by the viewport, not by the content: that is the
    //! whole point of a fixed-height region inside an auto-height panel.
    const ui::Style& style = gui.style();
    const float expectedBottom = kPanelOrigin.y + style.rowHeight + style.padding
                               + kViewportHeight + style.padding;

    float lowest = 0.0f;
    for (const auto& vertex : renderer.batches.front().vertices)
        lowest = std::max(lowest, vertex.pos.y);

    AURA_CHECK(lowest <= expectedBottom + 1.0f,
               "content taller than the region is clipped to it, not drawn past it");

    //! Scrolling down then back up must return to the original geometry.
    ui::Input wheel;
    wheel.scroll = -3.0f;
    frame(wheel);
    const size_t scrolledCount = renderer.batches.back().vertices.size();

    wheel.scroll = 10.0f; //! Far more than needed; the offset clamps at zero.
    frame(wheel);

    AURA_CHECK(scrolledCount > 0 && !renderer.batches.back().vertices.empty(),
               "scrolling keeps emitting geometry");
}

void testNewWidgetsStayInOneBatch()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    int choice = 0;
    int radio = 1;
    std::string text = "abc";
    float number = 1.5f;

    const std::string_view items[] = {"Alpha", "Beta", "Gamma"};

    gui.newFrame(ui::Input{});
    (void)gui.beginPanel("Everything", kPanelOrigin, kPanelWidth);

    (void)gui.inputText("Name", text);
    (void)gui.inputFloat("Scale", number);
    (void)gui.dropdown("Mode", choice, items);
    (void)gui.radioButton("One", radio, 1);
    (void)gui.selectable("A row", false);

    //! Unlike beginPanel/beginScroll above, these three genuinely gate whether
    //! their contents should run: a collapsed header or closed tree node means
    //! "do not emit this", not merely "misused outside a panel".
    if (gui.collapsingHeader("Section"))
        gui.label("inside");

    if (gui.treeNode("Node", true))
    {
        gui.label("child");
        gui.treePop();
    }

    if (gui.beginTabBar("Tabs"))
    {
        if (gui.tabItem("First"))
            gui.label("first page");
        (void)gui.tabItem("Second");
        gui.endTabBar();
    }

    gui.button("Hover me");
    gui.tooltip("explanation");

    gui.setNextItemWidth(60.0f);
    gui.button("Left");
    gui.sameLine();
    gui.button("Right");

    gui.endPanel();
    gui.render();

    AURA_CHECK(renderer.batches.size() == 1,
               "every widget kind together still submits exactly one batch");

    if (renderer.batches.empty())
        return;

    const auto& batch = renderer.batches.front();
    AURA_CHECK(batch.indices.size() % 6 == 0, "the batch is whole quads");

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

void testSameLineSharesARow()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    //! Two rows of one widget are taller than one row of two, which is the
    //! observable consequence of sameLine() actually packing them.
    const auto panelHeight = [&](bool packed) {
        gui.newFrame(ui::Input{});
        (void)gui.beginPanel(packed ? "Packed" : "Stacked", kPanelOrigin, kPanelWidth);
        gui.setNextItemWidth(60.0f);
        gui.button("A");
        if (packed)
            gui.sameLine();
        gui.button("B");
        gui.endPanel();
        gui.render();

        float lowest = 0.0f;
        for (const auto& vertex : renderer.batches.back().vertices)
            lowest = std::max(lowest, vertex.pos.y);
        return lowest;
    };

    const float stacked = panelHeight(false);
    const float packed = panelHeight(true);

    AURA_CHECK(packed < stacked, "sameLine keeps the second widget on the first's row");
}


/*
 * Retained widget state is swept, and the sweep is the dangerous half: dropping
 * an entry silently resets whatever the user did to that widget. These three
 * pin both sides -- what must go, and what must never.
 *
 * They rely on RetainedMap's policy constants (a 256-entry threshold, a
 * 256-frame interval and a 1024-frame idle window) only through the shape of
 * the numbers below, which are chosen to clear all three by a wide margin.
 */

//! Widget identity is positional as well as textual, so anything checked
//! across frames has to be submitted at the same index in the panel every
//! time. Every helper here puts its subject first.
void testLiveStateIsNeverSwept()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    constexpr int kGenerated = 300;  // comfortably past the sweep threshold
    constexpr int kFrames = 1400;    // and past the idle window, twice over

    std::vector<std::string> generated;
    for (int i = 0; i < kGenerated; ++i)
        generated.push_back("generated_" + std::to_string(i));

    // One frame that mints a few hundred ids nothing will ever ask for again,
    // plus two that start open.
    gui.newFrame(ui::Input{});
    if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
    {
        (void)gui.collapsingHeader("keeper", true);
        (void)gui.collapsingHeader("abandoned", true);
        for (const std::string& label : generated)
            (void)gui.collapsingHeader(label, true);
        gui.endPanel();
    }
    gui.render();

    // Then a long run in which only "keeper" is ever submitted. Asking for it
    // with defaultOpen=false proves the answer is remembered, not defaulted.
    bool openThroughout = true;
    for (int frame = 0; frame < kFrames; ++frame)
    {
        renderer.batches.clear();

        gui.newFrame(ui::Input{});
        if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
        {
            openThroughout = gui.collapsingHeader("keeper", false) && openThroughout;
            gui.endPanel();
        }
        gui.render();
    }

    AURA_CHECK(openThroughout, "a submitted widget keeps its state on every frame of a long run");

    gui.newFrame(ui::Input{});
    bool keeper = false;
    bool abandoned = true;
    if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
    {
        keeper = gui.collapsingHeader("keeper", false);
        abandoned = gui.collapsingHeader("abandoned", false);
        gui.endPanel();
    }
    gui.render();

    AURA_CHECK(keeper, "state of a widget still being submitted survives the sweep");
    AURA_CHECK(!abandoned, "state of a widget nothing has asked for in a long time is swept");
}

//! The threshold is what keeps the sweep off ordinary UIs entirely: a panel of
//! hand-written widgets never reaches it, so a section left collapsed for ten
//! minutes still remembers what was open inside it.
void testSmallUIsAreNeverSwept()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    constexpr int kFrames = 3000;

    gui.newFrame(ui::Input{});
    if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
    {
        (void)gui.collapsingHeader("Advanced", true);
        (void)gui.treeNode("hidden", true);
        gui.endPanel();
    }
    gui.render();

    // "hidden" is not submitted again for the whole run -- exactly what happens
    // to everything under a collapsed header.
    for (int frame = 0; frame < kFrames; ++frame)
    {
        renderer.batches.clear();

        gui.newFrame(ui::Input{});
        if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
        {
            (void)gui.collapsingHeader("Advanced", false);
            gui.endPanel();
        }
        gui.render();
    }

    gui.newFrame(ui::Input{});
    bool hidden = false;
    if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
    {
        (void)gui.collapsingHeader("Advanced", false);
        hidden = gui.treeNode("hidden", false);
        if (hidden)
            gui.treePop();
        gui.endPanel();
    }
    gui.render();

    AURA_CHECK(hidden, "a small UI is never swept, however long it runs");
}

//! A panel's position is retained state too, so it is subject to the same rule.
void testPanelPositionSurvivesALongRun()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    constexpr glm::vec2 kDefault{40.0f, 60.0f};
    constexpr int kFrames = 3000;

    const auto topLeft = [&renderer]() {
        glm::vec2 corner{1.0e9f, 1.0e9f};
        for (const auto& vertex : renderer.batches.back().vertices)
        {
            corner.x = std::min(corner.x, vertex.pos.x);
            corner.y = std::min(corner.y, vertex.pos.y);
        }
        return corner;
    };

    // Drag the panel somewhere that is not its default, then leave it alone.
    const glm::vec2 grab = kDefault + glm::vec2{20.0f, 5.0f};
    for (int step = 0; step < 3; ++step)
    {
        gui.newFrame(ui::Input{step == 0 ? grab : grab + glm::vec2{120.0f, 90.0f}, true});
        if (gui.beginPanel("Draggable", kDefault, kPanelWidth))
            gui.endPanel();
        gui.render();
    }

    gui.newFrame(ui::Input{});
    if (gui.beginPanel("Draggable", kDefault, kPanelWidth))
        gui.endPanel();
    gui.render();

    const glm::vec2 dropped = topLeft();
    AURA_CHECK(dropped != kDefault, "the panel actually moved off its default position");

    for (int frame = 0; frame < kFrames; ++frame)
    {
        //! The last frame's batch has to survive: topLeft() reads it.
        if (frame + 1 < kFrames)
            renderer.batches.clear();

        gui.newFrame(ui::Input{});
        if (gui.beginPanel("Draggable", kDefault, kPanelWidth))
            gui.endPanel();
        gui.render();
    }

    AURA_CHECK(topLeft() == dropped, "a panel does not snap back to its default over a long run");
}

//! newFrame() with no arguments is the path applications actually take. With no
//! window attached it sees no cursor and no clicks, but it must still open a
//! frame -- and it now hands its input buffers over rather than copying them,
//! which is easy to get wrong in a way that only shows up on the second frame.
void testArgumentlessNewFrameOpensAFrame()
{
    RecordingRenderer renderer;
    ui::Context gui(&renderer);

    size_t previousVertices = 0;
    bool stable = true;

    for (int frame = 0; frame < 4; ++frame)
    {
        gui.newFrame();
        if (gui.beginPanel("Panel", kPanelOrigin, kPanelWidth))
        {
            gui.label("row");
            (void)gui.button("press");
            gui.endPanel();
        }
        gui.render();

        const size_t vertices =
            renderer.batches.empty() ? 0 : renderer.batches.back().vertices.size();

        if (frame > 0)
            stable = stable && vertices == previousVertices;
        previousVertices = vertices;
    }

    AURA_CHECK(previousVertices > 0, "newFrame() with no window attached still draws");
    AURA_CHECK(stable, "an unattached newFrame() produces the same frame every time");
    AURA_CHECK(!gui.isCapturingKeyboard(), "an unattached context claims no keyboard");
}

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
    testTextFieldEditing();
    testTextFieldEscapeReverts();
    testTextFieldRespectsMaxBytes();
    testTabMovesFocusBetweenWidgets();
    testKeyboardActivatesButtonAndCheckbox();
    testScrollRegionClipsAndScrolls();
    testNewWidgetsStayInOneBatch();
    testSameLineSharesARow();
    testLiveStateIsNeverSwept();
    testSmallUIsAreNeverSwept();
    testPanelPositionSurvivesALongRun();
    testArgumentlessNewFrameOpensAFrame();

    AURA_TEST_MAIN_RETURN();
}
