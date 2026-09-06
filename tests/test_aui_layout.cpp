/*
 * AuraUI's layout engine, driven with no renderer and no window.
 *
 * Everything measure/arrange does is a pure function of the tree and the
 * constraints, so a UIRoot over a bitmap-font shaper is the whole harness.
 * That is the point of keeping UIRoot free of the platform: the layout engine
 * is testable at full fidelity, which is the only way flex and grid arithmetic
 * stays correct as the toolkit grows.
 */

#include <cmath>

#include "aura/UI/UI.hpp"

#include "TestUtils.h"

using namespace aura3d;
using namespace aura3d::ui;

namespace {

[[nodiscard]] bool near(f32 a, f32 b, f32 tolerance = 0.51f)
{
    return std::fabs(a - b) <= tolerance;
}

[[nodiscard]] bool sameRect(const Rect& rect, f32 x, f32 y, f32 width, f32 height)
{
    return near(rect.min.x, x) && near(rect.min.y, y) && near(rect.width(), width) &&
           near(rect.height(), height);
}

/// A root over the embedded bitmap font: no font file, no GPU, deterministic
/// metrics on every platform the tests run on.
struct Harness {
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};

    explicit Harness(glm::vec2 size = {400.0f, 300.0f}) { root.resize(size); }

    void layout() { root.update(0.0f); }
};

void testLengths()
{
    Harness harness;
    auto& page = harness.root.setContent<Column>();

    auto& fixed = page.add<Widget>();
    fixed.layout().width = Length::px(120.0f);
    fixed.layout().height = Length::px(40.0f);
    fixed.layout().hAlign = Alignment::Start;

    auto& percent = page.add<Widget>();
    percent.layout().width = Length::percent(50.0f);
    percent.layout().height = Length::px(10.0f);
    percent.layout().hAlign = Alignment::Start;

    harness.layout();

    AURA_CHECK(sameRect(fixed.bounds(), 0.0f, 0.0f, 120.0f, 40.0f),
               "Length::px pins both axes exactly");
    AURA_CHECK(near(percent.bounds().width(), 200.0f),
               "Length::percent resolves against the parent's content width");
}

void testMarginAndPadding()
{
    Harness harness;
    auto& page = harness.root.setContent<Column>();
    page.layout().padding = Thickness::all(10.0f);

    auto& child = page.add<Widget>();
    child.layout().margin = Thickness::all(5.0f);
    child.layout().height = Length::px(20.0f);

    harness.layout();

    AURA_CHECK(sameRect(child.bounds(), 15.0f, 15.0f, 370.0f, 20.0f),
               "padding and margin both inset, and stack");
}

void testAlignment()
{
    Harness harness;
    auto& page = harness.root.setContent<Column>();

    auto& centred = page.add<Widget>();
    centred.layout().width = Length::px(100.0f);
    centred.layout().height = Length::px(20.0f);
    centred.layout().hAlign = Alignment::Center;

    auto& trailing = page.add<Widget>();
    trailing.layout().width = Length::px(100.0f);
    trailing.layout().height = Length::px(20.0f);
    trailing.layout().hAlign = Alignment::End;

    harness.layout();

    AURA_CHECK(near(centred.bounds().min.x, 150.0f), "Alignment::Center centres within the slot");
    AURA_CHECK(near(trailing.bounds().min.x, 300.0f), "Alignment::End pushes to the far edge");
    AURA_CHECK(near(centred.bounds().width(), 100.0f),
               "an explicit Length survives a stretching parent");
}

void testFlexShares()
{
    Harness harness;
    auto& row = harness.root.setContent<Row>();
    row.setSpacing(10.0f);

    auto& left = row.add<Widget>();
    left.layout().width = Length::fill();

    auto& right = row.add<Widget>();
    right.layout().width = Length::fill();

    harness.layout();

    AURA_CHECK(near(left.bounds().width(), 195.0f) && near(right.bounds().width(), 195.0f),
               "two Fill children split the row evenly, minus the spacing");
    AURA_CHECK(near(right.bounds().min.x, 205.0f), "spacing sits between the children");

    right.layout().width = Length::fill(3.0f);
    harness.layout();

    AURA_CHECK(near(left.bounds().width(), 97.5f) && near(right.bounds().width(), 292.5f),
               "Fill weights split the leftover in proportion");
}

void testFixedAndFillMix()
{
    Harness harness;
    auto& row = harness.root.setContent<Row>();

    auto& sidebar = row.add<Widget>();
    sidebar.layout().width = Length::px(80.0f);

    auto& body = row.add<Widget>();
    body.layout().width = Length::fill();

    harness.layout();

    AURA_CHECK(near(sidebar.bounds().width(), 80.0f) && near(body.bounds().width(), 320.0f),
               "a Fill child takes what the fixed ones left");
}

void testMinMax()
{
    Harness harness;
    auto& row = harness.root.setContent<Row>();

    auto& capped = row.add<Widget>();
    capped.layout().width = Length::fill();
    capped.layout().maxWidth = 150.0f;

    harness.layout();

    AURA_CHECK(near(capped.bounds().width(), 150.0f), "maxWidth caps a Fill child");

    auto& floored = row.add<Widget>();
    floored.layout().minWidth = 60.0f;
    harness.layout();

    AURA_CHECK(floored.bounds().width() >= 60.0f, "minWidth holds a child open");
}

void testCollapsedTakesNoSpace()
{
    Harness harness;
    auto& column = harness.root.setContent<Column>();
    column.setSpacing(4.0f);

    auto& first = column.add<Widget>();
    first.layout().height = Length::px(20.0f);

    auto& hidden = column.add<Widget>();
    hidden.layout().height = Length::px(20.0f);

    auto& last = column.add<Widget>();
    last.layout().height = Length::px(20.0f);

    harness.layout();
    AURA_CHECK(near(last.bounds().min.y, 48.0f), "three rows stack with their spacing");

    hidden.setVisibility(Visibility::Collapsed);
    harness.layout();

    AURA_CHECK(near(last.bounds().min.y, 24.0f), "a Collapsed child takes no room at all");
    AURA_CHECK(hidden.bounds().empty(), "and is arranged into nothing");

    hidden.setVisibility(Visibility::Hidden);
    harness.layout();

    AURA_CHECK(near(last.bounds().min.y, 48.0f), "a Hidden child keeps its space");
}

void testIntrinsicSizing()
{
    Harness harness;
    auto& column = harness.root.setContent<Column>();
    column.layout().hAlign = Alignment::Start;
    column.layout().width = Length::automatic();

    auto& label = column.add<Label>("Hello");
    label.layout().hAlign = Alignment::Start;

    harness.layout();

    AURA_CHECK(label.bounds().width() > 0.0f && label.bounds().height() > 0.0f,
               "a label reports the size of its own text");
    AURA_CHECK(near(column.bounds().width(), label.bounds().width()),
               "an auto-width column is exactly as wide as its content");
}

void testWrappingHeightFollowsWidth()
{
    Harness harness;
    auto& page = harness.root.setContent<Column>();

    auto& label = page.add<Label>("the quick brown fox jumps over the lazy dog");
    label.setWrap(TextWrap::Word);

    harness.root.resize({400.0f, 300.0f});
    harness.layout();
    const f32 wide = label.bounds().height();

    harness.root.resize({120.0f, 300.0f});
    harness.layout();
    const f32 narrow = label.bounds().height();

    AURA_CHECK(narrow > wide, "a wrapping label gets taller as it gets narrower");
    AURA_CHECK(label.shaped().lines.size() > 1, "and reports more than one line");
}

void testGrid()
{
    Harness harness;
    auto& grid = harness.root.setContent<Grid>();
    grid.setColumns({Length::px(100.0f), Length::fill()});
    grid.setRows({Length::px(30.0f), Length::px(30.0f)});
    grid.setSpacing(10.0f);

    auto& a = grid.addAt<Widget>(0, 0);
    auto& b = grid.addAt<Widget>(0, 1);
    auto& c = grid.addAt<Widget>(1, 0);

    harness.layout();

    AURA_CHECK(sameRect(a.bounds(), 0.0f, 0.0f, 100.0f, 30.0f), "a fixed cell is exactly its track");
    AURA_CHECK(sameRect(b.bounds(), 110.0f, 0.0f, 290.0f, 30.0f),
               "a Fill column takes the rest of the row");
    AURA_CHECK(near(c.bounds().min.y, 40.0f), "the second row clears the first plus the spacing");

    b.layout().cell.columnSpan = 2;
    b.layout().cell.column = 0;
    harness.layout();

    AURA_CHECK(near(b.bounds().width(), 400.0f), "a span covers its tracks and the gap between");
}

void testGridAutoColumn()
{
    Harness harness;
    auto& grid = harness.root.setContent<Grid>();
    grid.setColumns({Length::automatic(), Length::fill()});

    auto& label = grid.addAt<Label>(0, 0, "Name");
    grid.addAt<Widget>(0, 1);

    harness.layout();

    AURA_CHECK(label.bounds().width() > 0.0f && near(grid.bounds().width(), 400.0f),
               "an Auto column sizes to its widest child");
}

void testScrollView()
{
    Harness harness({200.0f, 100.0f});

    auto& scroll = harness.root.setContent<ScrollView>();
    auto& column = scroll.setContent<Column>();

    for (int i = 0; i < 10; ++i)
        column.add<Widget>().layout().height = Length::px(30.0f);

    harness.layout();

    AURA_CHECK(near(scroll.contentSize().y, 300.0f), "the content measures its full height");
    AURA_CHECK(near(scroll.maxOffset().y, 200.0f),
               "the scrollable range is content minus viewport");

    scroll.scrollTo({0.0f, 50.0f});
    harness.layout();

    AURA_CHECK(near(column.bounds().min.y, -50.0f), "scrolling moves the content up");

    scroll.scrollTo({0.0f, 10000.0f});
    harness.layout();

    AURA_CHECK(near(scroll.offset().y, 200.0f), "the offset is clamped to the range");

    AURA_CHECK(scroll.contentInsets().right > 0.0f,
               "a vertical scroll view reserves its scrollbar gutter");
}

void testSpacerPushes()
{
    Harness harness;
    auto& row = harness.root.setContent<Row>();

    auto& first = row.add<Widget>();
    first.layout().width = Length::px(50.0f);

    row.add<Spacer>();

    auto& last = row.add<Widget>();
    last.layout().width = Length::px(50.0f);

    harness.layout();

    AURA_CHECK(near(last.bounds().min.x, 350.0f), "a Spacer pushes what follows to the far end");
}

void testMainAlignment()
{
    Harness harness;
    auto& row = harness.root.setContent<Row>();
    row.setMainAlignment(Alignment::Center);

    auto& only = row.add<Widget>();
    only.layout().width = Length::px(100.0f);

    harness.layout();

    AURA_CHECK(near(only.bounds().min.x, 150.0f),
               "mainAlignment centres a group that does not fill the axis");
}

void testLayoutRunsOnlyWhenDirty()
{
    Harness harness;
    auto& column = harness.root.setContent<Column>();
    auto& child = column.add<Widget>();

    harness.layout();
    AURA_CHECK(!column.layoutDirty(), "a clean pass clears the dirty flag");

    child.layout().height = Length::px(10.0f);
    AURA_CHECK(column.layoutDirty(), "invalidation propagates to the ancestors");
}

void testDetachKeepsSubtree()
{
    Harness harness;
    auto& column = harness.root.setContent<Column>();

    auto& outer = column.add<Column>();
    auto& inner = outer.add<Label>("kept");

    std::unique_ptr<Widget> taken = column.detach(outer);

    AURA_CHECK(taken != nullptr && taken->childCount() == 1,
               "a detached subtree comes back intact");
    AURA_CHECK(inner.root() == nullptr, "and is disconnected from the root");

    column.adopt(std::move(taken));
    harness.layout();

    AURA_CHECK(inner.root() == &harness.root, "re-adopting reconnects the whole subtree");
}

} // namespace

int main()
{
    testLengths();
    testMarginAndPadding();
    testAlignment();
    testFlexShares();
    testFixedAndFillMix();
    testMinMax();
    testCollapsedTakesNoSpace();
    testIntrinsicSizing();
    testWrappingHeightFollowsWidth();
    testGrid();
    testGridAutoColumn();
    testScrollView();
    testSpacerPushes();
    testMainAlignment();
    testLayoutRunsOnlyWhenDirty();
    testDetachKeepsSubtree();

    AURA_TEST_MAIN_RETURN();
}
