#include "XlibWindow.h"

#include <cstdint>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace aura3d::test::xlib
{
namespace
{

[[nodiscard]] Display *native(void *display) noexcept
{
    return static_cast<Display *>(display);
}

} // namespace

Window::Window(void *display, void *window) noexcept
    // NOLINTNEXTLINE(performance-no-int-to-ptr): X11 hands its XID through wma's opaque window handle.
    : _display(display), _window(static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)))
{
}

Pixels Window::read() const
{
    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(native(_display), _window, &attributes) || attributes.map_state != IsViewable)
        return {};
    XImage *image = XGetImage(native(_display), _window, 0, 0, static_cast<unsigned>(attributes.width),
                              static_cast<unsigned>(attributes.height), AllPlanes, ZPixmap);
    if (!image)
        return {};
    Pixels out{attributes.width, attributes.height, {}};
    out.rgb.reserve(static_cast<usize>(out.width) * static_cast<usize>(out.height));
    for (i32 y = 0; y < out.height; ++y)
        for (i32 x = 0; x < out.width; ++x)
            out.rgb.push_back(static_cast<u32>(XGetPixel(image, x, y)) & 0xFFFFFFu);
    XDestroyImage(image);
    return out;
}

void Window::expose() const
{
    XClearArea(native(_display), _window, 0, 0, 0, 0, True);
}

void Window::resize(u32 width, u32 height) const
{
    XResizeWindow(native(_display), _window, width, height);
}

void Window::unmap() const
{
    XUnmapWindow(native(_display), _window);
}

void Window::map() const
{
    XMapWindow(native(_display), _window);
}

void Window::move(i32 x, i32 y) const
{
    XMoveWindow(native(_display), _window, x, y);
}

void Window::sync() const
{
    XSync(native(_display), False);
}

} // namespace aura3d::test::xlib
