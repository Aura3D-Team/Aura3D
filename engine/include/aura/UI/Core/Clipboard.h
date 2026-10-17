#ifndef AURA_UI_CLIPBOARD_H
#define AURA_UI_CLIPBOARD_H

#pragma once

#include <string>
#include <string_view>

/**
 * @file Clipboard.h
 * @brief The text clipboard AuraUI copies to and pastes from.
 *
 * AuraUI owns none of it: UIRoot holds whichever IClipboard it is given, and a platform adapter (UIView installs one
 * over the window) is what reaches the system. With none given, a clipboard inside the process stands in, so copy and
 * paste work between the fields of one application with nothing behind them.
 *
 * @code
 * root.setClipboard(&myClipboard);   // null puts the in-process one back
 * @endcode
 */

namespace aura3d::ui
{

class IClipboard
{
  public:
    virtual ~IClipboard() = default;

    /// UTF-8; empty when there is no text.
    [[nodiscard]] virtual std::string text() = 0;

    /// False when the text was not taken. What copy and cut rely on: a cut that was not stored must not delete.
    virtual bool setText(std::string_view text) = 0;
};

/// A clipboard that lives in the process.
class MemoryClipboard final : public IClipboard
{
  public:
    [[nodiscard]] std::string text() override;
    bool setText(std::string_view text) override;

  private:
    std::string _text;
};

} // namespace aura3d::ui

#endif // AURA_UI_CLIPBOARD_H
