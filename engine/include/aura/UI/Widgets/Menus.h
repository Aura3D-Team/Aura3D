#ifndef AURA_UI_WIDGETS_MENUS_H
#define AURA_UI_WIDGETS_MENUS_H

#pragma once

#include <string>
#include <vector>

#include "aura/UI/Overlay.h"
#include "aura/UI/Widgets/Controls.h"
#include "aura/UI/Widgets/Layouts.h"

/**
 * @file Menus.h
 * @brief The widgets whose content lives in the floating layer.
 *
 * Both of these are the same shape: a column of @ref Selectable rows opened as
 * an @ref OverlayLayer entry, anchored to something. What differs is what
 * anchors them and what a click means. Nothing here re-implements positioning,
 * z-order or dismissal -- that is all @ref OverlayLayer's, once.
 */

namespace aura3d::ui {

/**
 * @class Menu
 * @brief A column of commands, for a menu bar or a right-click.
 *
 * Opened into the overlay rather than constructed in place:
 *
 * @code
 * auto& menu = root.openContextMenu(cursor);
 * menu.addItem("Rename", "F2").activated.connect([&] { rename(); });
 * menu.addSeparator();
 * menu.addItem("Delete").activated.connect([&] { remove(); });
 * @endcode
 *
 * A submenu is simply another overlay anchored to its parent item, so nesting
 * costs the menu nothing it does not already have.
 */
class Menu final : public Box {
public:
    Menu();

    /// @param shortcut Right-aligned reminder text. Purely a label -- binding
    ///        the key is UIRoot::addShortcut()'s job, not the menu's.
    Selectable& addItem(std::string text, std::string shortcut = {});

    void addSeparator();

    /**
     * @brief Adds a row that opens a nested menu beside itself.
     *
     * @param build Runs when the submenu opens, with the fresh Menu to fill
     *        in. Deferred rather than built now, so a submenu costs nothing
     *        until it is actually opened.
     */
    Selectable& addSubmenu(std::string text, std::function<void(Menu&)> build);

    /// Closes the overlay this menu was opened into, and every menu above it.
    void dismiss();

protected:
    /// Takes the popup surface from the theme once it has one, so opening a
    /// menu needs no styling call beside it.
    void onAttach() override;
};

/**
 * @class Dropdown
 * @brief A closed list that opens over whatever is below it.
 *
 * @code
 * auto& backend = row.add<Dropdown>(std::vector<std::string>{"Vulkan", "OpenGL", "CPU"});
 * backend.selected = 0;
 * backend.selected.changed().connect([&](int i) { switchBackend(i); });
 * @endcode
 *
 * Keyboard: Space or Enter opens, Up/Down move the highlight, Enter takes it,
 * Escape closes. With the list shut, Up/Down step the selection directly,
 * which is what makes a form fillable without ever opening one.
 */
class Dropdown final : public Control {
public:
    explicit Dropdown(std::vector<std::string> items = {});
    ~Dropdown() override;

    /// Index into items(); -1 when nothing is chosen.
    Property<int> selected;

    void setItems(std::vector<std::string> items);
    [[nodiscard]] const std::vector<std::string>& items() const noexcept { return _items; }

    /// Drawn while nothing is selected.
    void setPlaceholder(std::string text);

    /// The selected item's text, or the placeholder.
    [[nodiscard]] const std::string& text() const noexcept;

    [[nodiscard]] bool isOpen() const noexcept;

    void open();
    void close();

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void paint(DrawList& out) override;

    void activate() override;
    bool onKeyDown(const KeyEvent& event) override;

    void onDetach() override;

private:
    /// Moves the highlight while open, or the selection while shut.
    void _step(int delta);

    /// Applies @p index to the open list's rows.
    void _highlight(int index);

    void _commit(int index);

    /// Re-shapes the trigger's caption when the text or the theme changes.
    void _reshape();

    std::vector<std::string> _items;
    std::string _placeholder;

    ShapedText _shaped;
    std::string _shapedSource;
    f32 _shapedFontSize = 0.0f;

    OverlayLayer::Id _popup = OverlayLayer::kNone;

    //! The open list, for moving the highlight. Null whenever _popup is kNone;
    //! the overlay's onClosed callback is what keeps the two in step.
    Column* _list = nullptr;

    int _highlighted = -1;
};

} // namespace aura3d::ui

#endif // AURA_UI_WIDGETS_MENUS_H
