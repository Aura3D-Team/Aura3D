#include "aura/UI/Core/Clipboard.h"

namespace aura3d::ui
{

std::string MemoryClipboard::text()
{
    return _text;
}

bool MemoryClipboard::setText(std::string_view text)
{
    _text.assign(text);
    return true;
}

} // namespace aura3d::ui
