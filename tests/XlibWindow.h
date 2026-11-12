#ifndef AURA_TEST_XLIB_WINDOW_H
#define AURA_TEST_XLIB_WINDOW_H

#pragma once

#include <vector>

#include "aura/Core/AuraCore.h"

//! What a test does to an X11 window from outside, as the user or the window manager would. Xlib stays in
//! XlibWindow.cpp: its macros (None, Bool, Status, ...) collide with the engine's headers.
namespace aura3d::test::xlib
{

//! A window's pixels as the X server holds them: 0xRRGGBB, top row first.
struct Pixels
{
    i32 width = 0;
    i32 height = 0;
    std::vector<u32> rgb;

    [[nodiscard]] bool valid() const noexcept
    {
        return !rgb.empty();
    }
    [[nodiscard]] u32 at(i32 x, i32 y) const noexcept
    {
        return rgb[static_cast<usize>(y) * static_cast<usize>(width) + static_cast<usize>(x)];
    }
};

/// @p display and @p window are wma's getNativeDisplayHandle() and getWindowInstance() for an X11 window.
class Window
{
  public:
    Window(void *display, void *window) noexcept;

    //! Empty while the window is not viewable (unmapped).
    [[nodiscard]] Pixels read() const;
    void expose() const;
    void resize(u32 width, u32 height) const;
    void unmap() const;
    void map() const;
    void move(i32 x, i32 y) const;
    //! Sends what was asked and waits until the server has handled it.
    void sync() const;

  private:
    void *_display;
    unsigned long _window;
};

} // namespace aura3d::test::xlib

#endif // AURA_TEST_XLIB_WINDOW_H
