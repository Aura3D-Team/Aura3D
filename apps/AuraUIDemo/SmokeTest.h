#pragma once

#include <cstdio>
#include <stdexcept>

#include "aura/Renderer/IRenderer.h"
#include "aura/UI/UI.hpp"

namespace aura3d::ui {

template <class W, class Predicate>
W* galleryWidget(Widget& node, Predicate match)
{
    if (!node.effectivelyVisible())
        return nullptr;
    if (auto* widget = dynamic_cast<W*>(&node); widget && match(*widget))
        return widget;
    for (const auto& child : node.children())
        if (W* widget = galleryWidget<W>(*child, match))
            return widget;
    return nullptr;
}

inline int smokeTest(UIView& ui, IRenderer& renderer, TabView& tabs)
{
    auto& root = ui.root();
    auto& window = *renderer.getWindowManager();
    int checks = 0;
    const auto require = [&](bool valid, const char* message) {
        std::printf("[%s] %s\n", valid ? "PASS" : "FAIL", message);
        if (!valid)
            throw std::runtime_error(message);
        ++checks;
    };
    const auto frame = [&] {
        window.pollEvents();
        renderer.handleWindowChanges();
        renderer.beginFrame();
        renderer.beginRenderPass();
        ui.render(0.2f);
        renderer.endRenderPass();
        renderer.endFrame();
        window.swapBuffers();
    };
    const auto click = [&](Widget& widget) {
        root.pointerDown(widget.bounds().center());
        root.pointerUp(widget.bounds().center());
        frame();
    };
    const auto button = [&](const char* text) -> Button& {
        auto* result = galleryWidget<Button>(*root.content(),
            [text](const Button& candidate) { return candidate.text() == text; });
        require(result != nullptr, text);
        return *result;
    };

    try {
        frame();
        int clicks = 0;
        const ScopedConnection normal = button("Normal").clicked.connect([&] { ++clicks; });
        click(button("Normal"));
        click(button("Disabled"));
        require(clicks == 1, "button activation and disabled input");
        auto* check = galleryWidget<CheckBox>(*root.content(), [](const auto&) { return true; });
        require(check != nullptr, "checkbox present");
        const bool initial = check->checked.get();
        click(*check);
        require(check->checked.get() != initial, "checkbox toggles");
        auto* radio = galleryWidget<RadioButton>(*root.content(), [](const RadioButton& candidate) {
            return !candidate.checked.get();
        });
        require(radio != nullptr, "radio present");
        click(*radio);
        require(radio->checked.get(), "radio group selection");
        auto* slider = galleryWidget<Slider>(*root.content(), [](const auto&) { return true; });
        require(slider != nullptr, "slider present");
        root.pointerDown(slider->bounds().center());
        root.pointerMoved({slider->bounds().max.x + 100, slider->bounds().center().y});
        root.pointerUp({slider->bounds().max.x + 100, slider->bounds().center().y});
        frame();
        require(slider->value.get() == 4.0f && !root.pointerCapture(), "slider drag and capture release");

        tabs.current = 1;
        frame();
        auto* dropdown = galleryWidget<Dropdown>(*root.content(), [](const Dropdown& candidate) {
            return candidate.items().size() == 4;
        });
        require(dropdown != nullptr, "dropdown present");
        click(*dropdown);
        require(dropdown->isOpen(), "dropdown opens");
        Widget* list = root.overlay().topmost();
        require(list && list->childCount() == 4, "dropdown rows render");
        click(list->childAt(1));
        require(dropdown->selected.get() == 1 && !dropdown->isOpen(), "dropdown selection and dismissal");
        dropdown->requestFocus();
        root.keyDown(wma::KEY_SPACE);
        frame();
        root.keyDown(wma::KEY_DOWN);
        root.keyDown(wma::KEY_ENTER);
        frame();
        require(dropdown->selected.get() == 2, "dropdown keyboard selection");

        auto* entry = galleryWidget<Selectable>(*root.content(), [](const Selectable& candidate) {
            return candidate.text() == "Entry 0";
        });
        require(entry != nullptr, "list entry present");
        click(*entry);
        require(entry->selected.get(), "list selection");
        auto* scroll = dynamic_cast<ScrollView*>(entry->parent()->parent());
        require(scroll != nullptr, "nested list scroll view present");
        root.wheel({0, -100}, scroll->bounds().center());
        frame(); frame();
        require(scroll->offset().y > 0, "list wheel scrolling");
        auto* last = galleryWidget<Selectable>(*root.content(), [](const Selectable& candidate) {
            return candidate.text() == "Entry 23";
        });
        require(last != nullptr, "last list entry present");
        click(*last);
        require(last->selected.get() && !entry->selected.get(), "scrolled list hit testing");

        tabs.current = 2;
        frame();
        auto* field = galleryWidget<TextField>(*root.content(), [](const TextField& candidate) {
            return candidate.text.get() == "untitled";
        });
        require(field != nullptr, "text field present");
        click(*field);
        root.keyDown(wma::KEY_A, {.ctrl = true});
        root.textInput("héllo");
        frame();
        require(field->text.get() == "héllo", "UTF-8 text input and selection replacement");
        root.keyDown(wma::KEY_BACKSPACE);
        frame();
        require(field->text.get() == "héll", "text deletion");

        auto* readonly = galleryWidget<TextField>(*root.content(), [](const TextField& candidate) {
            return candidate.text.get() == "cannot be edited";
        });
        require(readonly != nullptr, "read-only field present");
        click(*readonly);
        root.keyDown(wma::KEY_A, {.ctrl = true});
        root.textInput("replacement");
        root.keyDown(wma::KEY_BACKSPACE);
        require(readonly->text.get() == "cannot be edited", "read-only input remains unchanged");

        tabs.current = 3;
        frame();
        require(!root.focused(), "switching pages clears hidden text focus");
        auto* section = galleryWidget<CollapsingHeader>(*root.content(), [](const auto&) { return true; });
        require(section != nullptr, "collapsing header present");
        const auto header = section->bounds().min + glm::vec2{6, 6};
        root.pointerDown(header); root.pointerUp(header); frame();
        require(!section->expanded.get(), "header collapses");

        tabs.current = 4;
        frame();
        click(button("Context menu"));
        Widget* menu = root.overlay().topmost();
        require(menu && menu->childCount() > 0, "context menu opens");
        click(menu->childAt(0));
        require(root.overlay().empty(), "menu action closes the menu");
        click(button("Modal dialog"));
        require(root.overlay().hasModal() && root.focused(), "modal dialog acquires focus");
        root.keyDown(wma::KEY_ESCAPE); frame();
        require(!root.overlay().hasModal(), "Escape dismisses modal without destroying renderer");
        root.setTooltipDelay(0.01f);
        root.pointerMoved(button("Compile").bounds().center());
        root.update(0.1f); frame();
        require(!root.overlay().empty(), "tooltip opens after dwell");
        root.pointerLeft(); root.update(0.1f); frame(); frame();
        require(root.overlay().empty(), "tooltip leaves with pointer");

        tabs.current = 5;
        frame();
        auto* node = galleryWidget<TreeNode>(*root.content(), [](TreeNode& candidate) {
            return candidate.content().childCount() == 0;
        });
        require(node != nullptr, "tree leaf present");
        click(*node);
        require(node->selected.get(), "tree selection");
        root.cancelInput();
        frame();
        require(!root.pointerCapture() && !root.focused(), "focus-loss cancellation");
        std::printf("AuraUI demo smoke: %d checks passed\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "AuraUI demo smoke failed: %s\n", error.what());
        return 1;
    }
}
}
