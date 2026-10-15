#ifndef AURA_UI_WINDOW_DECORATION_H
#define AURA_UI_WINDOW_DECORATION_H

#pragma once

#include <string_view>

#include "aura/UI/Widgets/Controls.h"

namespace aura3d::ui
{

/// Covers the surface as its root's decoration. Application content fills
/// contentRect(), so contentInsets() reserves the title bar and border, and only
/// the frame around it is painted. Children are arranged by the subclass. The
/// borrowed window outlives the view; call it from event-thread handlers.
class IWindowDecoration : public Widget
{
  public:
    virtual void update(wma::IWindowManager &window, std::string_view title) = 0;

    /// What the window manager should do with a press at @p point; UIView makes
    /// this the window's hit test. Content and interactive children are Client,
    /// the frame's own background is Caption. Must be cheap: asked on every move.
    [[nodiscard]] virtual wma::WindowHit windowHit(glm::vec2 point) const;

  protected:
    /// True when AuraUI must see a press at @p point: a popup sits over it, or one
    /// is open elsewhere and this press should close it. A modal layer alone does
    /// not count, so a dialog never pins the window in place.
    [[nodiscard]] bool overlayTakesPress(glm::vec2 point) const;

    void paint(DrawList &out) override;
};

/// How DefaultWindowDecoration looks and where it grabs. Pixels; colours are linear, which keeps the intended dark
/// surface on an sRGB swapchain.
struct WindowChrome
{
    f32 titleHeight = 34.0f;
    f32 buttonWidth = 46.0f; //! Each caption button; narrower surfaces shrink them.
    f32 titleInset = 16.0f;  //! Title to the left edge.
    f32 titleGap = 12.0f;    //! Title to the caption buttons.
    f32 titleFontSize = 16.0f;
    f32 iconSize = 12.0f; //! The caption icons' square. Their strokes scale with it.
    f32 border = 1.5f;    //! The painted resize border, while the window is resizable.
    /// Edges grab this far in, past the painted border: a surface without an invisible outer margin otherwise leaves
    /// a target too thin to hit with a mouse.
    f32 resizeReach = 5.0f;
    f32 cornerReach = 16.0f; //! How far along an edge from a corner the hit test reports a diagonal resize.

    glm::vec4 frame{0.006f, 0.006f, 0.006f, 1.0f};
    glm::vec4 titleText{0.82f, 0.82f, 0.82f, 1.0f};
    glm::vec4 icon{1.0f, 1.0f, 1.0f, 1.0f};
    ColorSet minimize{{0, 0, 0, 0}, {0.055f, 0.055f, 0.055f, 1}, {0.032f, 0.032f, 0.032f, 1}};
    ColorSet maximize{{0, 0, 0, 0}, {0.015f, 0.19f, 0.62f, 1}, {0.01f, 0.12f, 0.38f, 1}};
    ColorSet close{{0, 0, 0, 0}, {0.65f, 0.018f, 0.035f, 1}, {0.43f, 0.009f, 0.018f, 1}};
};

/// Title bar with a centred title and caption buttons, framed by a thin resize
/// border of the same colour while the window is resizable and not maximized.
class DefaultWindowDecoration : public IWindowDecoration
{
  public:
    DefaultWindowDecoration();

    /// Replaces the look and the reach; it applies at once.
    void setChrome(const WindowChrome &chrome);
    [[nodiscard]] const WindowChrome &chrome() const noexcept;

    void update(wma::IWindowManager &window, std::string_view title) override;
    [[nodiscard]] wma::WindowHit windowHit(glm::vec2 point) const override;
    [[nodiscard]] Thickness contentInsets() const noexcept override;

    [[nodiscard]] Label &titleLabel() noexcept
    {
        return *_title;
    }
    [[nodiscard]] Button &minimizeButton() noexcept
    {
        return *_minimize;
    }
    [[nodiscard]] Button &maximizeButton() noexcept
    {
        return *_maximize;
    }
    [[nodiscard]] Button &closeButton() noexcept
    {
        return *_close;
    }

  protected:
    void arrangeContent(const Rect &content) override;
    void paintChildren(DrawList &out) override;

  private:
    [[nodiscard]] Rect _titleBar() const noexcept;
    void _toggleMaximized();
    void _applyChrome();

    WindowChrome _chrome{};

    wma::IWindowManager *_window = nullptr;
    Label *_title = nullptr;
    Button *_minimize = nullptr;
    Button *_maximize = nullptr;
    Button *_close = nullptr;
    Widget *_maximizeIcon = nullptr;
    Widget *_restoreIcon = nullptr;
    bool _maximized = false;
    bool _resizable = false;
};

} // namespace aura3d::ui

#endif // AURA_UI_WINDOW_DECORATION_H
