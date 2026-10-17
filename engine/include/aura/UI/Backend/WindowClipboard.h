#ifndef AURA_UI_WINDOWCLIPBOARD_H
#define AURA_UI_WINDOWCLIPBOARD_H

#pragma once

#include <wma/wma.hpp>

#include "aura/UI/Core/Clipboard.h"

namespace aura3d::ui
{

/**
 * @class WindowClipboard
 * @brief The system clipboard, through the window AuraUI draws into.
 *
 * What UIView installs when the window has one (wma::IWindowManager::isClipboardSupported()). Call it on the event
 * thread, from the handler of the input that asked: a Wayland selection needs the serial of that event, which the
 * window takes from the events it has delivered.
 *
 * The window is borrowed and must outlive this.
 */
class WindowClipboard final : public IClipboard
{
  public:
    explicit WindowClipboard(wma::IWindowManager &window) noexcept;

    [[nodiscard]] std::string text() override;
    bool setText(std::string_view text) override;

  private:
    wma::IWindowManager *_window;
};

} // namespace aura3d::ui

#endif // AURA_UI_WINDOWCLIPBOARD_H
