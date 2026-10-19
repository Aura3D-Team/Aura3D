#include "aura/UI/Backend/WindowClipboard.h"

namespace aura3d::ui
{

WindowClipboard::WindowClipboard(wma::IWindowManager &window) noexcept : _window(&window)
{
}

std::string WindowClipboard::text()
{
    return _window->getClipboardText();
}

bool WindowClipboard::setText(std::string_view text)
{
    return _window->setClipboardText(text);
}

} // namespace aura3d::ui
