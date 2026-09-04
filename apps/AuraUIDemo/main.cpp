// AuraUIDemo -- the AuraUI component gallery.
//
// One page per widget family, each with something live to poke at. It is the
// toolkit's showcase, its manual test, and the running version of the examples
// in docs/14-auraui-toolkit.md.
//
// Everything here is built once, before the loop. There is no per-frame UI
// code at all: `ui.render()` is the whole frame.

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "aura/Core/Engine.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/UI/UI.hpp"

using namespace aura3d;
using namespace aura3d::ui;

namespace {

/// A quieter label, for the small-caps headings that group a card's rows.
[[nodiscard]] Style heading(const Theme& theme)
{
    glm::vec4 muted = theme[Part::Label].text;
    muted.a *= 0.55f;

    return Style{}.textColor(muted);
}

/// A titled, rounded card -- the gallery's unit of grouping.
Column& card(Widget& parent, std::string_view title, const Theme& theme)
{
    auto& panel = parent.add<Column>();
    panel.style().fill(theme.palette().window).rounded(10.0f);
    panel.layout().padding = Thickness::all(18.0f);
    panel.setSpacing(12.0f);

    auto& caption = panel.add<Label>(std::string{title});
    caption.setFontSize(12.0f);
    caption.style() = heading(theme);

    panel.add<Separator>();
    return panel;
}

/// A scrolling page of cards. Every tab is one of these.
Column& page(TabView& tabs, std::string title)
{
    auto& scroll = tabs.addTab(std::move(title)).add<ScrollView>();
    scroll.layout().height = Length::fill();

    auto& column = scroll.setContent<Column>();
    column.layout().padding = Thickness::all(18.0f);
    column.setSpacing(16.0f);
    return column;
}

/// A label whose text follows a slider, through a binding rather than a
/// hand-written callback.
Label& readout(Widget& parent, Slider& source, const char* format)
{
    auto& label = parent.add<Label>("");
    label.setAlign(Align::Right);

    //! Leaked deliberately: the binding must outlive this function and both
    //! ends live as long as the tree does. A widget that owned the connection
    //! would keep it in a ScopedConnection member.
    static std::vector<ScopedConnection> keep;
    keep.push_back(label.text.bindFrom(source.value, [format](f32 value) {
        std::array<char, 32> buffer{};
        std::snprintf(buffer.data(), buffer.size(), format, static_cast<double>(value));
        return std::string{buffer.data()};
    }));

    return label;
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void buildButtons(Column& into, const Theme& theme)
{
    {
        auto& panel = card(into, "BUTTONS", theme);

        auto& row = panel.add<Row>();
        row.setSpacing(10.0f);

        auto& normal = row.add<Button>("Normal");
        normal.layout().width = Length::fill();
        normal.setTooltip("An ordinary button.\nHover text lives in the overlay layer.");

        auto& accent = row.add<Button>("Primary");
        accent.layout().width = Length::fill();
        accent.style().fill(theme.palette().accent);

        auto& off = row.add<Button>("Disabled");
        off.layout().width = Length::fill();
        off.setEnabled(false);

        auto& status = panel.add<Label>("Nothing pressed yet.");
        status.style().textColor(theme.palette().accent);

        normal.clicked.connect([&status] { status.text = "Normal pressed."; });
        accent.clicked.connect([&status] { status.text = "Primary pressed."; });
    }

    {
        auto& panel = card(into, "TOGGLES", theme);

        auto& vsync = panel.add<CheckBox>("V-Sync", true);
        auto& wireframe = panel.add<CheckBox>("Wireframe");
        wireframe.setTooltip("Draws edges only.");

        panel.add<Separator>();

        auto& quality = panel.add<Row>();
        quality.setSpacing(16.0f);

        static RadioGroup group;
        for (const char* name : {"Low", "Medium", "High"})
            group.add(quality.add<RadioButton>(name));
        group.select(1);

        auto& state = panel.add<Label>("");
        state.style() = heading(theme);

        const auto refresh = [&] {
            state.text = std::string{"vsync="} + (vsync.checked.get() ? "on" : "off") +
                         "  wireframe=" + (wireframe.checked.get() ? "on" : "off") +
                         "  quality=" + std::to_string(group.selected());
        };

        vsync.checked.changed().connect([refresh](bool) { refresh(); });
        wireframe.checked.changed().connect([refresh](bool) { refresh(); });
        group.selectionChanged.connect([refresh](int) { refresh(); });
        refresh();
    }

    {
        auto& panel = card(into, "SLIDERS", theme);

        auto& exposure = panel.add<Slider>(0.0f, 4.0f);
        exposure.value = 1.0f;
        readout(panel, exposure, "exposure  %.2f");

        auto& stepped = panel.add<Slider>(0.0f, 10.0f);
        stepped.setStep(1.0f);
        stepped.value = 3.0f;
        readout(panel, stepped, "steps of 1  %.0f");

        panel.add<Separator>();

        auto& progress = panel.add<ProgressBar>(0.0f);
        progress.layout().height = Length::px(8.0f);

        //! Bound rather than pushed: the bar follows the slider with no
        //! callback of its own.
        static ScopedConnection link =
            progress.value.bindFrom(exposure.value, [](f32 v) { return v / 4.0f; });
    }
}

void buildSelection(Column& into, const Theme& theme)
{
    {
        auto& panel = card(into, "DROP-DOWN", theme);

        auto& form = panel.add<Grid>();
        form.setColumns({Length::automatic(), Length::fill()});
        form.setSpacing(12.0f);

        form.addAt<Label>(0, 0, "Backend").layout().vAlign = Alignment::Center;
        auto& backend = form.addAt<Dropdown>(
            0, 1, std::vector<std::string>{"Vulkan", "OpenGL", "Metal", "Software"});
        backend.selected = 0;

        form.addAt<Label>(1, 0, "Preset").layout().vAlign = Alignment::Center;
        auto& preset = form.addAt<Dropdown>(
            1, 1, std::vector<std::string>{"Performance", "Balanced", "Quality"});
        preset.setPlaceholder("Choose a preset");

        auto& chosen = panel.add<Label>("");
        chosen.style().textColor(theme.palette().accent);

        const auto refresh = [&] {
            chosen.text = backend.text() + "  /  " +
                          (preset.selected.get() < 0 ? std::string{"(none)"} : preset.text());
        };

        backend.selected.changed().connect([refresh](int) { refresh(); });
        preset.selected.changed().connect([refresh](int) { refresh(); });
        refresh();

        panel.add<Label>("Space opens it; arrows move; Enter takes it; Escape closes.")
            .style() = heading(theme);
    }

    {
        auto& panel = card(into, "LIST", theme);

        auto& scroll = panel.add<ScrollView>();
        scroll.layout().height = Length::px(140.0f);

        auto& list = scroll.setContent<Column>();

        static int selected = -1;
        for (int i = 0; i < 24; ++i)
        {
            auto& row = list.add<Selectable>("Entry " + std::to_string(i));
            row.setDetail(std::to_string(i * 17) + " kb");

            row.activated.connect([&list, i] {
                selected = i;
                for (usize k = 0; k < list.childCount(); ++k)
                    static_cast<Selectable&>(list.childAt(k)).selected = static_cast<int>(k) == i;
            });
        }
    }
}

void buildText(Column& into, const Theme& theme)
{
    {
        auto& panel = card(into, "FIELDS", theme);

        auto& form = panel.add<Grid>();
        form.setColumns({Length::automatic(), Length::fill()});
        form.setSpacing(12.0f);

        form.addAt<Label>(0, 0, "Name").layout().vAlign = Alignment::Center;
        auto& name = form.addAt<TextField>(0, 1, "untitled");
        name.setPlaceholder("Scene name");

        form.addAt<Label>(1, 0, "Read-only").layout().vAlign = Alignment::Center;
        auto& locked = form.addAt<TextField>(1, 1, "cannot be edited");
        locked.setReadOnly(true);

        auto& echo = panel.add<Label>("");
        echo.style().textColor(theme.palette().accent);

        //! The whole point of Property::bind: no callback, no glue.
        static ScopedConnection link = echo.text.bindFrom(
            name.text, [](const std::string& value) { return "you typed: " + value; });

        name.submitted.connect([&echo] { echo.text = "submitted with Enter."; });
    }

    {
        auto& panel = card(into, "TYPOGRAPHY", theme);

        panel.add<Label>("Heading").setFontSize(28.0f);
        panel.add<Label>("Subheading").setFontSize(18.0f);
        panel.add<Label>("Body text at the theme's default size.");

        panel.add<Separator>();

        auto& wrapped = panel.add<Label>(
            "This paragraph wraps to whatever width it is given. Shaping, kerning and word "
            "breaking all happen in the text engine, and the caret arithmetic every field "
            "uses is answered from the same shaped result.");
        wrapped.setWrap(TextWrap::Word);
        wrapped.style() = heading(theme);
    }
}

void buildContainers(Column& into, const Theme& theme)
{
    {
        auto& panel = card(into, "FLEX", theme);

        panel.add<Label>("Three fill children share the row by weight.")
            .style() = heading(theme);

        auto& row = panel.add<Row>();
        row.setSpacing(8.0f);

        const std::array<f32, 3> weights = {1.0f, 2.0f, 1.0f};
        for (usize i = 0; i < weights.size(); ++i)
        {
            auto& cell = row.add<Label>("fill(" + std::to_string(static_cast<int>(weights[i])) + ")");
            cell.setAlign(Align::Center);
            cell.layout().width = Length::fill(weights[i]);
            cell.layout().padding = Thickness::symmetric(0.0f, 10.0f);
            cell.style().fill(theme.palette().surface).rounded(6.0f);
        }
    }

    {
        auto& panel = card(into, "GRID", theme);

        auto& grid = panel.add<Grid>();
        grid.setColumns({Length::px(70.0f), Length::fill(), Length::fill(2.0f)});
        grid.setSpacing(8.0f);

        const std::array<const char*, 9> cells = {"70px", "fill", "fill(2)", "a", "b",
                                                  "c",    "d",    "e",       "f"};

        for (usize i = 0; i < cells.size(); ++i)
        {
            auto& cell = grid.addAt<Label>(static_cast<u16>(i / 3), static_cast<u16>(i % 3),
                                           cells[i]);
            cell.setAlign(Align::Center);
            cell.layout().padding = Thickness::symmetric(0.0f, 8.0f);
            cell.style().fill(theme.palette().surface).rounded(4.0f);
        }
    }

    {
        auto& panel = card(into, "SECTIONS", theme);

        auto& graphics = panel.add<CollapsingHeader>("Graphics");
        graphics.content().layout().padding = Thickness::symmetric(8.0f, 6.0f);
        graphics.content().setSpacing(6.0f);
        graphics.content().add<CheckBox>("Bloom", true);
        graphics.content().add<CheckBox>("Motion blur");
        graphics.content().add<Slider>(30.0f, 240.0f).value = 60.0f;

        auto& audio = panel.add<CollapsingHeader>("Audio", false);
        audio.content().layout().padding = Thickness::symmetric(8.0f, 6.0f);
        audio.content().add<Slider>(0.0f, 1.0f).value = 0.8f;
    }
}

void buildOverlays(Column& into, UIRoot& root, const Theme& theme)
{
    {
        auto& panel = card(into, "MENUS", theme);

        panel.add<Label>("Every one of these is the same overlay mechanism.")
            .style() = heading(theme);

        auto& row = panel.add<Row>();
        row.setSpacing(10.0f);

        auto& log = panel.add<Label>("");
        log.style().textColor(theme.palette().accent);

        auto& context = row.add<Button>("Context menu");
        context.layout().width = Length::fill();
        context.clicked.connect([&root, &context, &log] {
            auto& menu = root.overlay().open<Menu>(
                OverlayDesc{.anchor = context.bounds(), .placement = Placement::Below});

            menu.addItem("Rename", "F2").activated.connect([&log] { log.text = "Rename."; });
            menu.addItem("Duplicate", "Ctrl+D").activated.connect([&log] {
                log.text = "Duplicate.";
            });
            menu.addSeparator();

            menu.addSubmenu("Export", [&log](Menu& sub) {
                sub.addItem("PNG").activated.connect([&log] { log.text = "Exported PNG."; });
                sub.addItem("JPEG").activated.connect([&log] { log.text = "Exported JPEG."; });
            });

            menu.addSeparator();
            menu.addItem("Delete").activated.connect([&log] { log.text = "Deleted."; });
        });

        auto& dialog = row.add<Button>("Modal dialog");
        dialog.layout().width = Length::fill();
        dialog.clicked.connect([&root, &log, &theme] {
            auto& box = root.overlay().open<Column>(OverlayDesc{
                .placement = Placement::Center,
                .modal = true,
                .dismissOnOutsideClick = false,
            });

            box.style().fill(theme.palette().overlay).outline(theme.palette().border, 1.0f)
                .rounded(10.0f);
            box.layout().padding = Thickness::all(20.0f);
            box.layout().minWidth = 280.0f;
            box.setSpacing(12.0f);

            box.add<Label>("Discard changes?").setFontSize(18.0f);
            box.add<Label>("Everything outside this dialog is inert while it is up. "
                           "Tab stays inside it, and Escape closes it.")
                .setWrap(TextWrap::Word);

            auto& buttons = box.add<Row>();
            buttons.setSpacing(10.0f);
            buttons.add<Spacer>();

            const OverlayLayer::Id id = root.overlay().lastId();

            auto& cancel = buttons.add<Button>("Cancel");
            cancel.layout().width = Length::px(100.0f);
            cancel.clicked.connect([&root, id, &log] {
                root.overlay().close(id);
                log.text = "Cancelled.";
            });

            auto& discard = buttons.add<Button>("Discard");
            discard.layout().width = Length::px(100.0f);
            discard.style().fill(glm::vec4{0.55f, 0.20f, 0.24f, 1.0f});
            discard.clicked.connect([&root, id, &log] {
                root.overlay().close(id);
                log.text = "Discarded.";
            });
        });
    }

    {
        auto& panel = card(into, "TOOLTIPS", theme);

        panel.add<Label>("Rest the pointer on any of these.").style() = heading(theme);

        auto& row = panel.add<Row>();
        row.setSpacing(10.0f);

        static constexpr std::array<std::pair<const char*, const char*>, 3> kItems = {{
            {"Compile", "Build the current project"},
            {"Run", "Launch the last successful build"},
            {"Profile", "Run with the frame profiler attached"},
        }};

        for (const auto& [label, hint] : kItems)
        {
            auto& button = row.add<Button>(label);
            button.layout().width = Length::fill();
            button.setTooltip(hint);
        }
    }
}

void buildTree(Column& into, const Theme& theme)
{
    auto& panel = card(into, "TREE", theme);

    panel.add<Label>("Click a node to select it; click a branch to fold it.")
        .style() = heading(theme);

    auto& scroll = panel.add<ScrollView>();
    scroll.layout().height = Length::px(220.0f);

    auto& tree = scroll.setContent<Column>();

    static std::vector<TreeNode*> nodes;
    nodes.clear();

    auto& assets = tree.add<TreeNode>("Assets", true);
    auto& textures = assets.addChild("Textures", true);
    auto& models = assets.addChild("Models");
    auto& scenes = tree.add<TreeNode>("Scenes", true);

    nodes = {&assets, &textures, &models, &scenes};

    for (const char* name : {"brick.png", "orb.png", "noise.png"})
        nodes.push_back(&textures.addChild(name));

    for (const char* name : {"player.obj", "level.obj"})
        nodes.push_back(&models.addChild(name));

    for (const char* name : {"main.scene", "test.scene"})
        nodes.push_back(&scenes.addChild(name));

    auto& chosen = panel.add<Label>("Nothing selected.");
    chosen.style().textColor(theme.palette().accent);

    for (TreeNode* node : nodes)
    {
        node->activated.connect([node, &chosen] {
            for (TreeNode* other : nodes)
                other->selected = other == node;

            chosen.text = "Selected: " + node->label().text.get();
        });
    }
}

} // namespace

int main()
{
    Engine engine("settings.json");

    IRenderer* renderer = engine.getRenderer();
    wma::IWindowManager* window = renderer->getWindowManager();

    /*
     * No font ships with the engine, so this is empty and AuraUI falls back to
     * the embedded 5x8 bitmap font -- crude, but it draws on every platform
     * with no asset staged. Point it at any .ttf/.otf for real typography;
     * nothing else about the gallery changes.
     */
    UIViewDesc desc{};
    desc.fontPath = "";

    UIView ui(*renderer, desc);
    ui.attachInput(*window);

    const Theme& theme = ui.theme();

    auto& shell = ui.root().setContent<Column>();
    shell.layout().padding = Thickness::all(16.0f);
    shell.setSpacing(14.0f);

    auto& header = shell.add<Row>();
    header.setSpacing(12.0f);

    auto& title = header.add<Label>("AuraUI Gallery");
    title.setFontSize(26.0f);
    title.layout().vAlign = Alignment::Center;

    header.add<Spacer>();

    auto& palette = header.add<Dropdown>(std::vector<std::string>{"Dark", "Light"});
    palette.layout().vAlign = Alignment::Center;
    palette.selected = 0;
    palette.selected.changed().connect([&ui](int index) {
        ui.theme().applyPalette(index == 0 ? Palette::dark() : Palette::light());
    });

    shell.add<Separator>();

    auto& tabs = shell.add<TabView>();
    tabs.layout().height = Length::fill();

    buildButtons(page(tabs, "Buttons"), theme);
    buildSelection(page(tabs, "Selection"), theme);
    buildText(page(tabs, "Text"), theme);
    buildContainers(page(tabs, "Containers"), theme);
    buildOverlays(page(tabs, "Overlays"), ui.root(), theme);
    buildTree(page(tabs, "Tree"), theme);

    ui.root().addShortcut(Shortcut::withCtrl(wma::KEY_S),
                          [] { INK_INFO << "Ctrl+S reached the shortcut table"; });

    const glm::vec4 background = theme.palette().window * 0.35f;
    renderer->setClearColor(background.r, background.g, background.b, 1.0f);

    renderer->run([&] {
        const f32 deltaSeconds =
            static_cast<f32>(window->getWindowFlags()->deltaTime) / 1000.0f;

        renderer->beginRenderPass();
        ui.render(deltaSeconds);
        renderer->endRenderPass();
    });

    return 0;
}
