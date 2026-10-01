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
    void paint(DrawList &out) override;
};

/// Title bar with a centred title and caption buttons, framed by a thin resize
/// border of the same colour while the window is resizable and not maximized.
class DefaultWindowDecoration : public IWindowDecoration
{
  public:
    DefaultWindowDecoration();

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
