#ifndef AURA_UI_WIDGETS_NAVIGATION_H
#define AURA_UI_WIDGETS_NAVIGATION_H

#pragma once

#include <string>

#include "aura/UI/Core/Animation.h"
#include "aura/UI/Widgets/Controls.h"
#include "aura/UI/Widgets/Layouts.h"

/**
 * @file Navigation.h
 * @brief The widgets that show one part of their content at a time.
 */

namespace aura3d::ui {

/**
 * @class Disclosure
 * @brief A clickable header with content that folds away under it.
 *
 * The shared body of @ref CollapsingHeader and @ref TreeNode, which differ in
 * what they mean rather than in what they do: a header groups a form, a node
 * is a place in a hierarchy. Both are a row with an arrow and a column that
 * comes and goes.
 *
 * Only the header takes clicks. The content below it is ordinary tree, so
 * whatever is in there keeps its own input.
 */
class Disclosure : public Control {
public:
    Property<bool> expanded;

    /// Where children go. Laid out only while @ref expanded.
    [[nodiscard]] Column& content() noexcept { return *_content; }

    [[nodiscard]] Label& label() noexcept { return *_label; }
    void setTitle(std::string title);

    void accessibility(AccessibilityInfo& out) const override;

protected:
    Disclosure(Part part, std::string title, bool expanded);

    glm::vec2 measureContent(const Constraints& available) override;
    void arrangeContent(const Rect& content) override;
    void paint(DrawList& out) override;
    void activate() override;

    /// The header row only -- the content underneath must not toggle the
    /// header when something in it is missed.
    [[nodiscard]] bool hitTest(glm::vec2 point) const override;

    [[nodiscard]] f32 headerHeight() const;
    [[nodiscard]] Rect headerRect() const;

    /// How far the content sits in from the header. A tree indents; a
    /// collapsing header need not.
    f32 _indent = 0.0f;

    /// False for a node with no children, which gets no arrow and no toggle.
    bool _foldable = true;

private:
    Label* _label = nullptr;
    Column* _content = nullptr;
};

/**
 * @class CollapsingHeader
 * @brief A titled section of a form that folds away.
 *
 * @code
 * auto& graphics = page.add<CollapsingHeader>("Graphics");
 * graphics.content().add<CheckBox>("VSync");
 * graphics.content().add<Slider>(30.0f, 240.0f);
 * @endcode
 */
class CollapsingHeader final : public Disclosure {
public:
    explicit CollapsingHeader(std::string title = {}, bool expanded = true);
};

/**
 * @class TreeNode
 * @brief One place in a hierarchy: a label, its children, and a selection.
 *
 * @code
 * auto& assets = panel.add<TreeNode>("Assets");
 * assets.addChild("Textures");
 * assets.addChild("Models").addChild("player.obj");
 * @endcode
 *
 * A tree is a Column of these; there is no separate view type, because a list
 * of roots is already what a tree is.
 */
class TreeNode final : public Disclosure {
public:
    explicit TreeNode(std::string title = {}, bool expanded = false);

    /// Drawn as the current selection. A tree does not manage selection
    /// itself -- what "selected" means across a hierarchy is the caller's.
    Property<bool> selected;

    /// Fires on click, whether or not the node folds.
    Signal<> activated;

    /// Adds a child node and returns it, for nesting.
    TreeNode& addChild(std::string title, bool expanded = false);

    void accessibility(AccessibilityInfo& out) const override;

protected:
    void paint(DrawList& out) override;
    void activate() override;
};

/**
 * @class TabView
 * @brief A row of tabs over one page at a time.
 *
 * @code
 * auto& tabs = page.add<TabView>();
 * tabs.addTab("General").add<CheckBox>("Autosave");
 * tabs.addTab("Graphics").add<Slider>(0.0f, 1.0f);
 * @endcode
 *
 * Left and Right move between tabs while any of them has focus.
 */
class TabView final : public Widget {
public:
    TabView();

    /// The selected tab. Out-of-range values are clamped.
    Property<int> current;

    /// Adds a tab and returns its page, for filling in.
    Column& addTab(std::string title);

    [[nodiscard]] usize tabCount() const noexcept;

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void arrangeContent(const Rect& content) override;
    void paint(DrawList& out) override;

    bool onKeyDown(const KeyEvent& event) override;
    bool onTick(f32 deltaSeconds) override;

private:
    /// Shows the selected page, marks the selected tab, and sends the
    /// underline after it.
    void _apply();

    Row* _bar = nullptr;

    //! A plain Widget: its default layout overlays its children, which is
    //! exactly a stack of pages with one of them visible.
    Widget* _pages = nullptr;

    //! The underline, animated rather than jumped, because a tab strip is the
    //! one place a UI can afford to show where the selection went.
    Transition<f32> _indicatorX{0.0f};
    Transition<f32> _indicatorWidth{0.0f};
};

} // namespace aura3d::ui

#endif // AURA_UI_WIDGETS_NAVIGATION_H
