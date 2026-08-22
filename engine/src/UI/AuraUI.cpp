#include "aura/UI/AuraUI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace aura3d::ui {
namespace {
constexpr size_t kVerticesPerQuad = 4;
constexpr size_t kIndicesPerQuad = 6;

constexpr float kUnboundedBelow = 1.0e6f;

[[nodiscard]] constexpr u32 hashBytes(std::string_view text, u32 seed) noexcept
{
    constexpr u32 kPrime = 16777619u;

    u32 hash = seed;
    for (const char c : text)
    {
        hash ^= static_cast<u32>(static_cast<u8>(c));
        hash *= kPrime;
    }
    return hash;
}

constexpr u32 kHashOffsetBasis = 2166136261u;

[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

constexpr float kScrollbarWidth = 10.0f;

constexpr float kScrollStep = 48.0f;

[[nodiscard]] constexpr bool isContinuationByte(char c) noexcept
{
    return (static_cast<u8>(c) & 0xC0u) == 0x80u;
}

[[nodiscard]] constexpr size_t previousBoundary(std::string_view text, size_t offset) noexcept
{
    if (offset == 0)
        return 0;

    --offset;
    while (offset > 0 && isContinuationByte(text[offset]))
        --offset;

    return offset;
}

[[nodiscard]] constexpr size_t nextBoundary(std::string_view text, size_t offset) noexcept
{
    const size_t size = text.size();
    if (offset >= size)
        return size;

    ++offset;
    while (offset < size && isContinuationByte(text[offset]))
        ++offset;

    return offset;
}

[[nodiscard]] constexpr bool isWordSeparator(char c) noexcept
{
    return c == ' ' || c == '\t';
}

[[nodiscard]] constexpr size_t previousWord(std::string_view text, size_t offset) noexcept
{
    while (offset > 0 && isWordSeparator(text[previousBoundary(text, offset)]))
        offset = previousBoundary(text, offset);

    while (offset > 0 && !isWordSeparator(text[previousBoundary(text, offset)]))
        offset = previousBoundary(text, offset);

    return offset;
}

[[nodiscard]] constexpr size_t nextWord(std::string_view text, size_t offset) noexcept
{
    const size_t size = text.size();

    while (offset < size && !isWordSeparator(text[offset]))
        offset = nextBoundary(text, offset);

    while (offset < size && isWordSeparator(text[offset]))
        offset = nextBoundary(text, offset);

    return offset;
}

void appendUtf8(std::string& out, char32_t codepoint)
{
    const auto emit = [&out](u32 byte) { out.push_back(static_cast<char>(byte)); };

    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
        return;

    if (codepoint < 0x80u)
    {
        emit(codepoint);
    }
    else if (codepoint < 0x800u)
    {
        emit(0xC0u | (codepoint >> 6));
        emit(0x80u | (codepoint & 0x3Fu));
    }
    else if (codepoint < 0x10000u)
    {
        emit(0xE0u | (codepoint >> 12));
        emit(0x80u | ((codepoint >> 6) & 0x3Fu));
        emit(0x80u | (codepoint & 0x3Fu));
    }
    else
    {
        emit(0xF0u | (codepoint >> 18));
        emit(0x80u | ((codepoint >> 12) & 0x3Fu));
        emit(0x80u | ((codepoint >> 6) & 0x3Fu));
        emit(0x80u | (codepoint & 0x3Fu));
    }
}

} // namespace

Context::Context(IRenderer* renderer, const ContextDesc& desc)
    : _renderer(renderer)
{
    FontAtlasDesc atlasDesc{};
    atlasDesc.width = desc.atlasSize;
    atlasDesc.height = desc.atlasSize;
    atlasDesc.pixelHeight = desc.pixelHeight;

    if (!desc.fontPath.empty())
    {
        auto loaded = FontAtlas::fromFile(desc.fontPath, atlasDesc);
        if (loaded)
        {
            _atlas = std::move(*loaded);
        }
        else
        {
            INK_WARN << loaded.error() << "; falling back to the embedded bitmap font";
        }
    }

    if (!_atlas)
        _atlas = FontAtlas::builtinBitmap(atlasDesc);

    const auto solidUv = _atlas->solidTexelUv();
    if (!solidUv)
    {
        INK_ERROR << "AuraUI: the glyph atlas is too small to reserve a solid texel";
        return;
    }
    _solidUv = *solidUv;

    _atlasTexture = _renderer->createDynamicTexture(_atlas->width(), _atlas->height());
    if (!isValidHandle(_atlasTexture))
    {
        INK_ERROR << "AuraUI: could not allocate the glyph atlas texture";
        return;
    }

    _usable = true;
}

Context::~Context()
{
    if (!_window)
        return;

    wma::KeyboardListener& keyboard = _window->getKeyboardListener();
    wma::MouseListener& mouse = _window->getMouseListener();
    wma::TouchListener& touch = _window->getTouchListener();

    for (const Attachment& attachment : _attachments)
    {
        keyboard.setKeyEventAction(wma::KeyEventCallback{}, attachment.keys);
        keyboard.setTextInputAction(wma::TextInputCallback{}, attachment.keys);

        mouse.removeButtonAction(wma::MouseButton::WMALeft, attachment.pointer);

        touch.setDownAction(wma::TouchInputCallback{}, attachment.touch);
        touch.setMoveAction(wma::TouchInputCallback{}, attachment.touch);
        touch.setUpAction(wma::TouchInputCallback{}, attachment.touch);
    }
}

void Context::attachInput(wma::IWindowManager& windowManager)
{
    _window = &windowManager;
    _mouse = &windowManager.getMouseListener();

    _attachments.push_back(
        Attachment{windowManager.getKeyboardListener().getResolvedContext(),
                   _mouse->getResolvedContext(),
                   windowManager.getTouchListener().getResolvedContext()});

    _mouse->addButtonAction(wma::MouseButton::WMALeft,
                            wma::MouseAction{[this]() { _pendingInput.mouseDown = true; },
                                             [this]() { _pendingInput.mouseDown = false; }});

    wma::KeyboardListener& keyboard = windowManager.getKeyboardListener();

    keyboard.setKeyEventAction(wma::KeyEventCallback::from(
        [this](const wma::WMAKeyEvent& event) {
            if (event.isPressOrRepeat())
                _pendingInput.keys.push_back(KeyPress{event.key, event.mods});
        }));

    keyboard.setTextInputAction(wma::TextInputCallback::from(
        [this](wma::Codepoint codepoint) { appendUtf8(_pendingInput.text, codepoint); }));

    wma::TouchListener& touch = windowManager.getTouchListener();

    const auto trackFinger = [this](const wma::WMATouchPoint& point) {
        _pendingInput.mouse = {static_cast<float>(point.x), static_cast<float>(point.y)};
    };

    touch.setDownAction(wma::TouchInputCallback::from(
        [this, trackFinger](const wma::WMATouchPoint& point) {
            trackFinger(point);
            _pendingInput.mouseDown = true;
            _pointer.touchActive = true;
        }));

    touch.setMoveAction(wma::TouchInputCallback::from(trackFinger));

    touch.setUpAction(wma::TouchInputCallback::from(
        [this](const wma::WMATouchPoint&) {
            _pendingInput.mouseDown = false;
            _pointer.touchActive = false;
        }));
}

void Context::newFrame() noexcept
{
    if (_mouse && !_pointer.touchActive)
    {
        const wma::WMAMousePosition position = _mouse->getCurrentPosition();
        _pendingInput.mouse = {static_cast<float>(position.x), static_cast<float>(position.y)};

        const wma::WMAMouseScroll scroll = _mouse->consumeScrollDelta();
        _pendingInput.scroll += static_cast<float>(scroll.yOffset);
    }

    newFrame(_pendingInput);

    _pendingInput.clearFrameEvents();

    if (_window && _window->isTextInputEnabled() != _capture.textInput)
        _window->setTextInputEnabled(_capture.textInput);
}

void Context::newFrame(const Input& input) noexcept
{
    _input = input;

    _capture.mouse = _capture.mouseThisFrame || _pointer.active != 0;
    _capture.mouseThisFrame = false;
    _capture.textInputThisFrame = false;

    _pointer.beginFrame(_input.mouseDown);
    _focus.beginFrame();

    for (const KeyPress& press : _input.keys)
    {
        if (press.key != wma::KEY_TAB)
            continue;

        if (press.mods.shift)
            _focus.requestPrev = true;
        else
            _focus.requestNext = true;
    }

    _vertices.clear();
    _indices.clear();
    _overlayQuads.clear();
    _deferring = false;

    _panel.open = false;
    _scrollStack.clear();
    _panel.treeDepth = 0;
    _tabBar.open = false;
    _clip = Rect{};
}

bool Context::_focusable(u32 id) noexcept
{
    if (_focus.first == 0)
        _focus.first = id;
    _focus.last = id;

    if (_focus.requestNext && _focus.passed)
    {
        _focus.requestNext = false;
        _setFocus(id);
    }

    else if (_focus.requestPrev && id == _focus.lastFrame)
    {
        _focus.requestPrev = false;
        if (_focus.previous != 0)
            _setFocus(_focus.previous);
        else
            _focus.wrapToLast = true;
    }

    if (id == _focus.lastFrame)
        _focus.passed = true;

    _focus.previous = id;

    if (_focus.current == id)
    {
        _focus.submitted = true;
        return true;
    }

    return false;
}

void Context::_setFocus(u32 id) noexcept
{
    if (_focus.current == id)
        return;

    _focus.current = id;
    _focus.submitted = true;
}

void Context::_clearFocus(u32 id) noexcept
{
    if (_focus.current == id)
        _focus.current = 0;
}

bool Context::_activated(bool focused) const noexcept
{
    return focused && (_keyPressed(wma::KEY_ENTER) ||
                       _keyPressed(wma::KEY_KP_ENTER) ||
                       _keyPressed(wma::KEY_SPACE));
}

bool Context::_keyPressed(wma::Key key) const noexcept
{
    for (const KeyPress& press : _input.keys)
    {
        if (press.key == key && press.mods.onlyShiftOrNone())
            return true;
    }
    return false;
}

bool Context::_keyPressed(wma::Key key, wma::KeyModifiers mods) const noexcept
{
    for (const KeyPress& press : _input.keys)
    {
        if (press.key == key && press.mods == mods)
            return true;
    }
    return false;
}

void Context::render()
{
    if (_focus.requestNext)
    {
        _focus.requestNext = false;
        if (_focus.first != 0)
            _setFocus(_focus.first);
    }

    if (_focus.requestPrev || _focus.wrapToLast)
    {
        _focus.requestPrev = false;
        _focus.wrapToLast = false;
        if (_focus.last != 0)
            _setFocus(_focus.last);
    }

    _capture.keyboard = _focus.current != 0;
    _capture.textInput = _capture.textInputThisFrame;

    _flushOverlays();

    if (!_usable || _indices.empty())
        return;

    _uploadAtlasChanges();

    _renderer->drawBatch2D(_vertices, _indices, _atlasTexture);
}

void Context::_flushOverlays()
{
    if (_overlayQuads.empty())
        return;

    const Rect savedClip = _clip;
    _deferring = false;

    for (const DeferredQuad& quad : _overlayQuads)
    {
        _clip = quad.clip;
        _texturedQuad(quad.bounds, quad.uvMin, quad.uvMax, quad.color);
    }

    _clip = savedClip;
    _overlayQuads.clear();
}

bool Context::beginPanel(std::string_view title, glm::vec2 defaultPosition, float width)
{
    if (!_usable || _panel.open)
        return false;

    const u32 id = hashBytes(title, kHashOffsetBasis);

    PanelState& state = _panels[id];
    if (!state.placed)
    {
        state.position = defaultPosition;
        state.placed = true;
    }

    const float titleHeight = _style.rowHeight;

    const Rect grabBar{state.position, state.position + glm::vec2{width, titleHeight}};

    _clip = grabBar;
    const Interaction bar = _behaviour(hashBytes("##title", id), grabBar);

    if (bar.held)
    {
        if (_pointer.pressed)
            _pointer.dragOffset = _input.mouse - state.position;

        state.position = _input.mouse - _pointer.dragOffset;

        wma::IWindowManager* manager = _renderer->getWindowManager();
        const wma::WindowDetails* window = manager ? manager->getWindowDetails() : nullptr;
        if (window)
        {
            const float margin = _style.rowHeight;

            const float minX = margin - width;
            const float maxX = std::max(minX, static_cast<float>(window->width) - margin);
            const float minY = 0.0f;
            const float maxY = std::max(minY, static_cast<float>(window->height) - margin);

            state.position.x = std::clamp(state.position.x, minX, maxX);
            state.position.y = std::clamp(state.position.y, minY, maxY);
        }
    }

    const Rect titleBar{state.position, state.position + glm::vec2{width, titleHeight}};

    _panel = CurrentPanel{};
    _panel.id = id;
    _panel.bounds = Rect{state.position, {state.position.x + width, state.position.y + titleHeight}};
    _panel.open = true;

    _clip = Rect{state.position, {state.position.x + width, state.position.y + kUnboundedBelow}};

    _panel.backgroundVertex = _vertices.size();
    _quad(_panel.bounds, _style.panelBackground);
    _panel.backgroundEmitted = _vertices.size() == _panel.backgroundVertex + kVerticesPerQuad;

    _quad(titleBar, _style.panelTitle);
    _textCentered(title, titleBar, _style.text);

    _clip = Rect{{state.position.x, state.position.y + titleHeight},
                 {state.position.x + width, state.position.y + kUnboundedBelow}};

    _panel.cursorY = state.position.y + titleHeight + _style.padding;

    return true;
}

void Context::endPanel()
{
    if (!_panel.open)
        return;

    const float bottom = std::max(_panel.cursorY - _style.itemSpacing + _style.padding,
                                  _panel.bounds.min.y + _style.rowHeight);

    _panel.bounds.max.y = bottom;

    if (_panel.backgroundEmitted)
    {
        _vertices[_panel.backgroundVertex + 2].pos.y = bottom;
        _vertices[_panel.backgroundVertex + 3].pos.y = bottom;
    }

    if (_panel.bounds.contains(_input.mouse))
        _capture.mouseThisFrame = true;

    _panel.open = false;
}

void Context::label(std::string_view text)
{
    if (!_panel.open)
        return;

    const Rect row = _nextRow(_style.rowHeight);
    _text(text, {row.min.x, row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);
}

bool Context::button(std::string_view text)
{
    if (!_panel.open)
        return false;

    const u32 id = _idFor(text);
    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(id, row);

    const bool focused = _focusable(id);

    if (it.clicked)
        _setFocus(id);

    const bool activated = _activated(focused);

    const glm::vec4& fill = it.held      ? _style.controlActive
                          : it.hovered   ? _style.controlHovered
                                         : _style.control;

    _quad(row, fill);

    if (focused)
        _focusRing(row);

    _textCentered(text, row, _style.text);

    return it.clicked || activated;
}

bool Context::checkbox(std::string_view text, bool& value)
{
    if (!_panel.open)
        return false;

    const u32 id = _idFor(text);
    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(id, row);

    const bool focused = _focusable(id);

    if (it.clicked)
        _setFocus(id);

    const bool toggled = _activated(focused);

    if (it.clicked || toggled)
        value = !value;

    if (focused)
        _focusRing(row);

    const float boxSize = std::max(_style.rowHeight - 6.0f, 4.0f);
    const float boxTop = row.min.y + (row.height() - boxSize) * 0.5f;
    const Rect box{{row.min.x, boxTop}, {row.min.x + boxSize, boxTop + boxSize}};

    _quad(box, it.hovered ? _style.controlHovered : _style.control);

    if (value)
    {
        const float inset = std::max(boxSize * 0.25f, 2.0f);
        _quad({box.min + inset, box.max - inset}, _style.accent);
    }

    _text(text,
          {box.max.x + _style.padding, row.min.y + (row.height() - _lineHeight()) * 0.5f},
          _style.text);

    return it.clicked || toggled;
}

bool Context::sliderFloat(std::string_view text, float& value, float min, float max)
{
    if (!_panel.open || !(max > min))
        return false;

    const u32 id = _idFor(text);
    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(id, row);

    const bool focused = _focusable(id);

    if (it.held && _pointer.pressed)
        _setFocus(id);

    const float clamped = std::clamp(value, min, max);
    bool changed = clamped != value;
    value = clamped;

    if (focused)
    {
        const float fine = (max - min) * 0.01f;
        const float coarse = (max - min) * 0.10f;

        for (const KeyPress& press : _input.keys)
        {
            if (!press.mods.onlyShiftOrNone())
                continue;

            const float step = press.mods.shift ? coarse : fine;

            if (press.key == wma::KEY_LEFT || press.key == wma::KEY_DOWN)
            {
                value = std::clamp(value - step, min, max);
                changed = true;
            }
            else if (press.key == wma::KEY_RIGHT || press.key == wma::KEY_UP)
            {
                value = std::clamp(value + step, min, max);
                changed = true;
            }
        }
    }

    if (it.held)
    {
        const float t = std::clamp((_input.mouse.x - row.min.x) / row.width(), 0.0f, 1.0f);
        const float next = lerp(min, max, t);
        if (next != value)
        {
            value = next;
            changed = true;
        }
    }

    _quad(row, it.hovered ? _style.controlHovered : _style.control);

    const float fill = (value - min) / (max - min);
    if (fill > 0.0f)
        _quad({row.min, {row.min.x + fill * row.width(), row.max.y}}, _style.accent);

    char formatted[32];
    std::snprintf(formatted, sizeof(formatted), "%.3f", static_cast<double>(value));

    _caption.clear();
    _caption.append(text);
    _caption.append(": ");
    _caption.append(formatted);

    if (focused)
        _focusRing(row);

    _textCentered(_caption, row, _style.text);

    return changed;
}

void Context::_focusRing(const Rect& bounds)
{
    constexpr float kThickness = 1.0f;
    const glm::vec4& color = _style.accent;

    _quad({{bounds.min.x, bounds.min.y}, {bounds.max.x, bounds.min.y + kThickness}}, color);
    _quad({{bounds.min.x, bounds.max.y - kThickness}, {bounds.max.x, bounds.max.y}}, color);
    _quad({{bounds.min.x, bounds.min.y}, {bounds.min.x + kThickness, bounds.max.y}}, color);
    _quad({{bounds.max.x - kThickness, bounds.min.y}, {bounds.max.x, bounds.max.y}}, color);
}

float Context::_xFromCaret(std::string_view text, size_t offset)
{
    return measureText(text.substr(0, std::min(offset, text.size()))).x;
}

size_t Context::_caretFromX(std::string_view text, float originX, float x)
{
    size_t best = 0;
    float bestDistance = std::abs(originX - x);

    float penX = originX;
    char32_t previous = 0;

    size_t offset = 0;
    while (offset < text.size())
    {
        size_t cursor = offset;
        const char32_t codepoint = decodeUtf8(text, cursor);
        if (codepoint == 0)
            break;

        offset = cursor;

        if (const GlyphInfo* glyph = _atlas ? _atlas->glyph(codepoint) : nullptr)
        {
            if (previous != 0)
                penX += _atlas->kerning(previous, codepoint) * _style.textScale;

            penX += glyph->advance * _style.textScale;
            previous = codepoint;
        }

        const float distance = std::abs(penX - x);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = offset;
        }
    }

    return best;
}

bool Context::_editText(TextState& state, std::string& value, size_t maxBytes)
{
    bool changed = false;

    state.caret = std::min(state.caret, value.size());
    state.anchor = std::min(state.anchor, value.size());

    const auto selectionRange = [&state]() {
        return std::pair{std::min(state.caret, state.anchor),
                         std::max(state.caret, state.anchor)};
    };

    const auto deleteSelection = [&]() {
        const auto [from, to] = selectionRange();
        if (from == to)
            return false;

        value.erase(from, to - from);
        state.caret = from;
        state.anchor = from;
        return true;
    };

    const auto moveCaret = [&state](size_t to, bool selecting) {
        state.caret = to;
        if (!selecting)
            state.anchor = to;
    };

    for (const KeyPress& press : _input.keys)
    {
        const bool selecting = press.mods.shift;
        const bool word = press.mods.ctrl;

        switch (press.key)
        {
            case wma::KEY_LEFT:
                moveCaret(word ? previousWord(value, state.caret)
                               : previousBoundary(value, state.caret), selecting);
                break;

            case wma::KEY_RIGHT:
                moveCaret(word ? nextWord(value, state.caret)
                               : nextBoundary(value, state.caret), selecting);
                break;

            case wma::KEY_HOME:
                moveCaret(0, selecting);
                break;

            case wma::KEY_END:
                moveCaret(value.size(), selecting);
                break;

            case wma::KEY_BACKSPACE:
                if (deleteSelection()) {
                    changed = true;
                } else if (state.caret > 0) {
                    const size_t from = word ? previousWord(value, state.caret)
                                             : previousBoundary(value, state.caret);
                    value.erase(from, state.caret - from);
                    state.caret = from;
                    state.anchor = from;
                    changed = true;
                }
                break;

            case wma::KEY_DELETE:
                if (deleteSelection()) {
                    changed = true;
                } else if (state.caret < value.size()) {
                    const size_t to = word ? nextWord(value, state.caret)
                                           : nextBoundary(value, state.caret);
                    value.erase(state.caret, to - state.caret);
                    changed = true;
                }
                break;

            case wma::KEY_A:

                if (press.mods.ctrl) {
                    state.anchor = 0;
                    state.caret = value.size();
                }
                break;

            default:
                break;
        }
    }

    if (!_input.text.empty())
    {
        if (deleteSelection())
            changed = true;

        if (value.size() + _input.text.size() <= maxBytes)
        {
            value.insert(state.caret, _input.text);
            state.caret += _input.text.size();
            state.anchor = state.caret;
            changed = true;
        }
    }

    return changed;
}

bool Context::inputText(std::string_view label, std::string& value, size_t maxBytes)
{
    if (!_panel.open)
        return false;

    const u32 id = _idFor(label);

    const Rect fullRow = _nextRow(_style.rowHeight);
    const float labelWidth = label.empty() ? 0.0f : measureText(label).x + _style.padding;
    const Rect row{fullRow.min,
                   {std::max(fullRow.min.x, fullRow.max.x - labelWidth), fullRow.max.y}};

    const Interaction it = _behaviour(id, row);

    const bool focused = _focusable(id);

    if (it.clicked || (it.held && _pointer.pressed))
        _setFocus(id);
    else if (_pointer.pressed && !it.hovered && focused)
        _clearFocus(id);

    TextState& state = _textStates[id];

    if (focused && _focus.lastFrame != id)
    {
        state.original = value;

        state.caret = value.size();
        state.anchor = state.caret;
    }

    bool changed = false;

    if (focused)
    {
        if (_keyPressed(wma::KEY_ESCAPE))
        {
            value = state.original;
            changed = true;
            _clearFocus(id);
        }
        else if (_keyPressed(wma::KEY_ENTER) || _keyPressed(wma::KEY_KP_ENTER))
        {
            _clearFocus(id);
        }
        else
        {
            changed = _editText(state, value, maxBytes);
        }

        if (_focus.current == id)
            _capture.textInputThisFrame = true;
    }

    const glm::vec4& fill = focused    ? _style.controlActive
                          : it.hovered ? _style.controlHovered
                                       : _style.control;
    _quad(row, fill);

    const float inset = _style.padding * 0.5f;
    const float visibleWidth = std::max(row.width() - inset * 2.0f, 1.0f);
    const float caretX = _xFromCaret(value, state.caret);

    if (caretX - state.scrollX > visibleWidth)
        state.scrollX = caretX - visibleWidth;
    if (caretX < state.scrollX)
        state.scrollX = caretX;

    const float textWidth = measureText(value).x;
    state.scrollX = std::clamp(state.scrollX, 0.0f, std::max(0.0f, textWidth - visibleWidth));

    const Rect savedClip = _clip;
    _clip = Rect{{std::max(row.min.x, savedClip.min.x), std::max(row.min.y, savedClip.min.y)},
                 {std::min(row.max.x, savedClip.max.x), std::min(row.max.y, savedClip.max.y)}};

    const glm::vec2 textOrigin{row.min.x + inset - state.scrollX,
                               row.min.y + (row.height() - _lineHeight()) * 0.5f};

    if (focused)
    {
        const auto [from, to] = std::pair{std::min(state.caret, state.anchor),
                                          std::max(state.caret, state.anchor)};
        if (from != to)
        {
            _quad({{textOrigin.x + _xFromCaret(value, from), row.min.y + 2.0f},
                   {textOrigin.x + _xFromCaret(value, to), row.max.y - 2.0f}},
                  _style.accent);
        }
    }

    _text(value, textOrigin, _style.text);

    if (focused)
    {
        const float x = textOrigin.x + caretX;
        _quad({{x, row.min.y + 2.0f}, {x + 1.0f, row.max.y - 2.0f}}, _style.text);
    }

    _clip = savedClip;

    if (it.held && _pointer.pressed)
    {
        state.caret = _caretFromX(value, textOrigin.x, _input.mouse.x);
        state.anchor = state.caret;
    }
    else if (it.held)
    {
        state.caret = _caretFromX(value, textOrigin.x, _input.mouse.x);
    }

    _text(label, {row.max.x + _style.padding,
                  row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);

    return changed;
}

bool Context::inputFloat(std::string_view label, float& value)
{
    if (!_panel.open)
        return false;

    const u32 id = hashBytes(label, _panel.id) ^ (_panel.widgetIndex * 0x9e3779b9u);

    auto [entry, inserted] = _numericBuffers.try_emplace(id);
    std::string& buffer = entry->second;

    if (inserted || _focus.current != id)
    {
        char formatted[32];
        std::snprintf(formatted, sizeof(formatted), "%.3f", static_cast<double>(value));
        buffer.assign(formatted);
    }

    if (!inputText(label, buffer, 31))
        return false;

    const char* begin = buffer.c_str();
    char* end = nullptr;
    const float parsed = std::strtof(begin, &end);

    if (end == begin || !std::isfinite(parsed))
        return false;

    if (parsed == value)
        return false;

    value = parsed;
    return true;
}

bool Context::beginScroll(std::string_view id, float height)
{
    if (!_panel.open)
        return false;

    const u32 widgetId = _idFor(id);
    const Rect viewport = _nextRow(std::max(height, _style.rowHeight));

    ScrollState& state = _scrollStates[widgetId];

    const float maxOffset = std::max(0.0f, state.contentHeight - viewport.height());

    if (viewport.contains(_input.mouse) && _clip.contains(_input.mouse) && _input.scroll != 0.0f)
    {
        state.offset -= _input.scroll * kScrollStep;
        _input.scroll = 0.0f;
        _capture.mouseThisFrame = true;
    }

    state.offset = std::clamp(state.offset, 0.0f, maxOffset);

    _quad(viewport, {0.0f, 0.0f, 0.0f, 0.25f});

    if (maxOffset > 0.0f)
    {
        const Rect track{{viewport.max.x - kScrollbarWidth, viewport.min.y},
                         {viewport.max.x, viewport.max.y}};

        const float visibleFraction = viewport.height() / state.contentHeight;
        const float thumbHeight = std::max(track.height() * visibleFraction, 16.0f);
        const float travel = track.height() - thumbHeight;
        const float thumbTop = track.min.y + travel * (state.offset / maxOffset);

        const Rect thumb{{track.min.x, thumbTop}, {track.max.x, thumbTop + thumbHeight}};

        const Interaction drag = _behaviour(hashBytes("##scrollbar", widgetId), track);
        if (drag.held)
        {
            const float t = std::clamp((_input.mouse.y - track.min.y - thumbHeight * 0.5f)
                                       / std::max(travel, 1.0f), 0.0f, 1.0f);
            state.offset = t * maxOffset;
        }

        _quad(track, {1.0f, 1.0f, 1.0f, 0.06f});
        _quad(thumb, drag.held ? _style.controlActive
                   : drag.hovered ? _style.controlHovered
                                  : _style.control);
    }

    ScrollFrame frame;
    frame.id = widgetId;
    frame.viewport = viewport;
    frame.savedClip = _clip;
    frame.savedIndent = _panel.indent;

    _clip = Rect{{std::max(viewport.min.x, _clip.min.x), std::max(viewport.min.y, _clip.min.y)},
                 {std::min(viewport.max.x, _clip.max.x), std::min(viewport.max.y, _clip.max.y)}};

    _panel.cursorY = viewport.min.y - state.offset;
    frame.contentTop = _panel.cursorY;

    _scrollStack.push_back(frame);

    return true;
}

void Context::endScroll()
{
    if (_scrollStack.empty())
        return;

    const ScrollFrame frame = _scrollStack.back();
    _scrollStack.pop_back();

    _scrollStates[frame.id].contentHeight = _panel.cursorY - frame.contentTop;

    _clip = frame.savedClip;
    _panel.indent = frame.savedIndent;

    _panel.cursorY = frame.viewport.max.y + _style.itemSpacing;
    _panel.rowHeight = frame.viewport.height();
    _panel.rowTop = frame.viewport.min.y;

    if (frame.viewport.contains(_input.mouse))
        _capture.mouseThisFrame = true;
}

bool Context::selectable(std::string_view label, bool selected)
{
    if (!_panel.open)
        return false;

    const u32 id = _idFor(label);
    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(id, row);

    const bool focused = _focusable(id);

    if (it.clicked)
        _setFocus(id);

    const bool activated = _activated(focused);

    if (selected)
        _quad(row, _style.controlActive);
    else if (it.hovered)
        _quad(row, _style.controlHovered);

    if (focused)
        _focusRing(row);

    _text(label, {row.min.x + _style.padding * 0.5f,
                  row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);

    return it.clicked || activated;
}

bool Context::radioButton(std::string_view label, int& value, int buttonValue)
{
    if (!_panel.open)
        return false;

    const u32 id = _idFor(label);
    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(id, row);

    const bool focused = _focusable(id);

    if (it.clicked)
        _setFocus(id);

    const bool chosen = it.clicked || _activated(focused);

    const bool wasSelected = value == buttonValue;
    const bool takes = chosen && !wasSelected;

    if (chosen)
        value = buttonValue;

    if (focused)
        _focusRing(row);

    const float boxSize = std::max(_style.rowHeight - 8.0f, 4.0f);
    const float boxTop = row.min.y + (row.height() - boxSize) * 0.5f;
    const Rect box{{row.min.x, boxTop}, {row.min.x + boxSize, boxTop + boxSize}};

    _quad(box, it.hovered ? _style.controlHovered : _style.control);

    if (value == buttonValue)
    {
        const float inset = std::max(boxSize * 0.3f, 2.0f);
        _quad({box.min + inset, box.max - inset}, _style.accent);
    }

    _text(label, {box.max.x + _style.padding,
                  row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);

    return takes;
}

bool Context::collapsingHeader(std::string_view label, bool defaultOpen)
{
    if (!_panel.open)
        return false;

    const u32 id = _idFor(label);

    auto [entry, inserted] = _widgetValues.try_emplace(id, defaultOpen ? 1u : 0u);
    u32& open = entry->second;

    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(id, row);

    const bool focused = _focusable(id);

    if (it.clicked)
        _setFocus(id);

    if (it.clicked || _activated(focused))
        open = open ? 0u : 1u;

    _quad(row, it.hovered ? _style.controlHovered : _style.panelTitle);

    if (focused)
        _focusRing(row);

    _text(open ? "-" : "+",
          {row.min.x + _style.padding * 0.5f,
           row.min.y + (row.height() - _lineHeight()) * 0.5f},
          _style.text);

    _text(label, {row.min.x + _style.padding * 2.0f,
                  row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);

    return open;
}

bool Context::treeNode(std::string_view label, bool defaultOpen)
{
    if (!_panel.open)
        return false;

    const u32 id = _idFor(label);

    auto [entry, inserted] = _widgetValues.try_emplace(id, defaultOpen ? 1u : 0u);
    u32& open = entry->second;

    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(id, row);

    const bool focused = _focusable(id);

    if (it.clicked)
        _setFocus(id);

    if (it.clicked || _activated(focused))
        open = open ? 0u : 1u;

    if (it.hovered)
        _quad(row, _style.controlHovered);

    if (focused)
        _focusRing(row);

    _text(open ? "v" : ">",
          {row.min.x, row.min.y + (row.height() - _lineHeight()) * 0.5f},
          _style.text);

    _text(label, {row.min.x + _style.padding * 1.5f,
                  row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);

    if (open)
    {
        _panel.indent += _style.padding * 1.5f;
        ++_panel.treeDepth;
    }

    return open;
}

void Context::treePop() noexcept
{
    if (_panel.treeDepth == 0)
        return;

    --_panel.treeDepth;
    _panel.indent = std::max(0.0f, _panel.indent - _style.padding * 1.5f);
}

bool Context::dropdown(std::string_view label, int& index, std::span<const std::string_view> items)
{
    if (!_panel.open || items.empty())
        return false;

    const u32 id = _idFor(label);

    index = std::clamp(index, 0, static_cast<int>(items.size()) - 1);

    const Rect fullRow = _nextRow(_style.rowHeight);
    const float labelWidth = label.empty() ? 0.0f : measureText(label).x + _style.padding;
    const Rect row{fullRow.min,
                   {std::max(fullRow.min.x, fullRow.max.x - labelWidth), fullRow.max.y}};

    const Interaction it = _behaviour(id, row);

    auto [entry, inserted] = _widgetValues.try_emplace(id, 0u);
    u32& open = entry->second;

    const bool focused = _focusable(id);

    if (it.clicked)
        _setFocus(id);

    if (it.clicked || _activated(focused))
        open = open ? 0u : 1u;

    bool movedByKey = false;
    if (focused)
    {
        const auto count = static_cast<int>(items.size());

        if (_keyPressed(wma::KEY_DOWN) && index + 1 < count)
        {
            ++index;
            movedByKey = true;
        }
        else if (_keyPressed(wma::KEY_UP) && index > 0)
        {
            --index;
            movedByKey = true;
        }
    }

    _quad(row, open ? _style.controlActive
             : it.hovered ? _style.controlHovered
                          : _style.control);

    _text(items[static_cast<size_t>(index)],
          {row.min.x + _style.padding * 0.5f,
           row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);

    _text("v", {row.max.x - _style.padding * 1.5f,
                row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);

    _text(label, {row.max.x + _style.padding,
                  row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);

    if (focused)
        _focusRing(row);

    bool changed = movedByKey;

    if (open)
    {
        const float itemHeight = _style.rowHeight;
        const Rect list{{row.min.x, row.max.y},
                        {row.max.x, row.max.y + itemHeight * static_cast<float>(items.size())}};

        if (list.contains(_input.mouse))
            _capture.mouseThisFrame = true;

        const Rect savedClip = _clip;

        _clip = Rect{{list.min.x, list.min.y}, {list.max.x, list.max.y}};

        _deferring = true;
        _quad(list, _style.panelBackground);

        for (size_t i = 0; i < items.size(); ++i)
        {
            const Rect itemRow{{list.min.x, list.min.y + itemHeight * static_cast<float>(i)},
                               {list.max.x, list.min.y + itemHeight * static_cast<float>(i + 1)}};

            const Interaction itemIt =
                _behaviour(hashBytes(items[i], id) ^ 0x51ed270bu, itemRow);

            if (itemIt.hovered)
                _quad(itemRow, _style.controlHovered);
            else if (static_cast<int>(i) == index)
                _quad(itemRow, _style.controlActive);

            _text(items[i], {itemRow.min.x + _style.padding * 0.5f,
                             itemRow.min.y + (itemRow.height() - _lineHeight()) * 0.5f},
                  _style.text);

            if (itemIt.clicked)
            {
                changed = changed || static_cast<int>(i) != index;
                index = static_cast<int>(i);
                open = 0u;
            }
        }

        _deferring = false;
        _clip = savedClip;

        if (_pointer.pressed && !list.contains(_input.mouse) && !row.contains(_input.mouse))
            open = 0u;
    }

    return changed;
}

bool Context::beginTabBar(std::string_view id)
{
    if (!_panel.open || _tabBar.open)
        return false;

    _tabBar.id = _idFor(id);
    _tabBar.index = 0;
    _tabBar.open = true;

    _panel.rowTop = _panel.cursorY;
    _panel.rowHeight = _style.rowHeight;
    _panel.cursorY += _style.rowHeight + _style.itemSpacing;

    _tabBar.cursorX = _panel.bounds.min.x + _style.padding + _panel.indent;

    return true;
}

bool Context::tabItem(std::string_view label)
{
    if (!_tabBar.open)
        return false;

    const u32 index = _tabBar.index++;

    auto [entry, inserted] = _widgetValues.try_emplace(_tabBar.id, 0u);
    u32& selected = entry->second;

    const float width = measureText(label).x + _style.padding * 2.0f;

    const Rect tab{{_tabBar.cursorX, _panel.rowTop},
                   {_tabBar.cursorX + width, _panel.rowTop + _style.rowHeight}};

    _tabBar.cursorX += width + 2.0f;

    const u32 id = hashBytes(label, _tabBar.id);
    const Interaction it = _behaviour(id, tab);

    const bool focused = _focusable(id);

    if (it.clicked)
        _setFocus(id);

    if (it.clicked || _activated(focused))
        selected = index;

    const bool active = selected == index;

    _quad(tab, active      ? _style.controlActive
             : it.hovered  ? _style.controlHovered
                           : _style.control);

    if (focused)
        _focusRing(tab);

    _textCentered(label, tab, _style.text);

    return active;
}

void Context::endTabBar()
{
    _tabBar.open = false;
}

void Context::tooltip(std::string_view text)
{
    if (!_panel.open || !_panel.lastWidget.contains(_input.mouse) || !_clip.contains(_input.mouse))
        return;

    const glm::vec2 size = measureText(text);
    const float pad = _style.padding * 0.5f;

    const Rect box{{_input.mouse.x + 12.0f, _input.mouse.y + 12.0f},
                   {_input.mouse.x + 12.0f + size.x + pad * 2.0f,
                    _input.mouse.y + 12.0f + size.y + pad * 2.0f}};

    const Rect savedClip = _clip;
    _clip = box;

    _deferring = true;
    _quad(box, {0.05f, 0.05f, 0.07f, 0.96f});
    _text(text, {box.min.x + pad, box.min.y + pad}, _style.text);
    _deferring = false;

    _clip = savedClip;
}

void Context::separator()
{
    if (!_panel.open)
        return;

    constexpr float kThickness = 1.0f;

    const Rect row = _nextRow(_style.itemSpacing * 2.0f + kThickness);

    const float y = std::round((row.min.y + row.max.y) * 0.5f);

    _quad({{row.min.x, y}, {row.max.x, y + kThickness}}, _style.separator);
}

void Context::spacing(float pixels) noexcept
{
    if (_panel.open)
        _panel.cursorY += pixels;
}

glm::vec2 Context::measureText(std::string_view text)
{
    if (!_atlas || text.empty())
        return {0.0f, 0.0f};

    float width = 0.0f;
    char32_t previous = 0;

    size_t offset = 0;
    while (offset < text.size())
    {
        const char32_t codepoint = decodeUtf8(text, offset);
        if (codepoint == 0)
            break;

        const GlyphInfo* glyph = _atlas->glyph(codepoint);
        if (!glyph)
            continue;

        if (previous != 0)
            width += _atlas->kerning(previous, codepoint) * _style.textScale;

        width += glyph->advance * _style.textScale;
        previous = codepoint;
    }

    return {width, _lineHeight()};
}

Context::Interaction Context::_behaviour(u32 id, const Rect& bounds) noexcept
{
    Interaction result;
    result.hovered = _hovering(bounds);

    if (result.hovered)
        _pointer.nextHot = id;

    if (_pointer.active == id)
    {
        _pointer.activeSubmitted = true;
        result.held = true;

        if (!_input.mouseDown)
        {
            result.clicked = result.hovered;
            result.held = false;
            _pointer.active = 0;
        }
    }

    else if (_pointer.pressed && (_pointer.hot == id || (_pointer.hot == 0 && result.hovered)))
    {
        _pointer.active = id;
        _pointer.activeSubmitted = true;
        result.held = true;
    }

    return result;
}

bool Context::_hovering(const Rect& bounds) const noexcept
{
    return bounds.contains(_input.mouse) && _clip.contains(_input.mouse);
}

Rect Context::_nextRow(float height) noexcept
{
    const float left = _panel.bounds.min.x + _style.padding + _panel.indent;
    float right = _panel.bounds.max.x - _style.padding;

    if (!_scrollStack.empty())
        right = _scrollStack.back().viewport.max.x - kScrollbarWidth - _style.itemSpacing;

    if (_panel.packNext)
    {
        _panel.packNext = false;

        const float x = _panel.lastWidget.max.x + _style.itemSpacing;
        const float wanted = _panel.nextWidth > 0.0f ? _panel.nextWidth : (right - x);
        _panel.nextWidth = 0.0f;

        const Rect row{{x, _panel.rowTop},
                       {std::min(x + wanted, right), _panel.rowTop + _panel.rowHeight}};

        _panel.lastWidget = row;
        return row;
    }

    _panel.rowTop = _panel.cursorY;
    _panel.rowHeight = height;

    const float wanted = _panel.nextWidth > 0.0f ? _panel.nextWidth : (right - left);
    _panel.nextWidth = 0.0f;

    const Rect row{{left, _panel.cursorY},
                   {std::min(left + wanted, right), _panel.cursorY + height}};

    _panel.cursorY += height + _style.itemSpacing;
    _panel.lastWidget = row;

    return row;
}

void Context::sameLine() noexcept
{
    if (_panel.open && _panel.rowHeight > 0.0f)
        _panel.packNext = true;
}

void Context::setNextItemWidth(float width) noexcept
{
    if (_panel.open)
        _panel.nextWidth = std::max(width, 0.0f);
}

u32 Context::_idFor(std::string_view text) noexcept
{
    return hashBytes(text, _panel.id) ^ (_panel.widgetIndex++ * 0x9e3779b9u);
}

float Context::_lineHeight() const noexcept
{
    return _atlas ? _atlas->lineHeight() * _style.textScale : 0.0f;
}

void Context::_quad(const Rect& bounds, const glm::vec4& color)
{
    _texturedQuad(bounds, _solidUv, _solidUv, color);
}

void Context::_texturedQuad(Rect bounds, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4& color)
{
    const float width = bounds.width();
    const float height = bounds.height();
    if (width <= 0.0f || height <= 0.0f)
        return;

    if (_deferring)
    {
        _overlayQuads.push_back(DeferredQuad{bounds, uvMin, uvMax, color, _clip});
        return;
    }

    const Rect visible{{std::max(bounds.min.x, _clip.min.x), std::max(bounds.min.y, _clip.min.y)},
                       {std::min(bounds.max.x, _clip.max.x), std::min(bounds.max.y, _clip.max.y)}};

    if (visible.width() <= 0.0f || visible.height() <= 0.0f)
        return;

    const glm::vec2 uv0{lerp(uvMin.x, uvMax.x, (visible.min.x - bounds.min.x) / width),
                        lerp(uvMin.y, uvMax.y, (visible.min.y - bounds.min.y) / height)};
    const glm::vec2 uv1{lerp(uvMin.x, uvMax.x, (visible.max.x - bounds.min.x) / width),
                        lerp(uvMin.y, uvMax.y, (visible.max.y - bounds.min.y) / height)};

    const auto base = static_cast<u32>(_vertices.size());

    _vertices.push_back({{visible.min.x, visible.min.y}, {uv0.x, uv0.y}, color});
    _vertices.push_back({{visible.max.x, visible.min.y}, {uv1.x, uv0.y}, color});
    _vertices.push_back({{visible.max.x, visible.max.y}, {uv1.x, uv1.y}, color});
    _vertices.push_back({{visible.min.x, visible.max.y}, {uv0.x, uv1.y}, color});

    _indices.push_back(base + 0);
    _indices.push_back(base + 1);
    _indices.push_back(base + 2);
    _indices.push_back(base + 2);
    _indices.push_back(base + 3);
    _indices.push_back(base + 0);
}

void Context::_text(std::string_view text, glm::vec2 origin, const glm::vec4& color)
{
    if (!_atlas || text.empty())
        return;

    _vertices.reserve(_vertices.size() + text.size() * kVerticesPerQuad);
    _indices.reserve(_indices.size() + text.size() * kIndicesPerQuad);

    const float scale = _style.textScale;

    float penX = origin.x;
    char32_t previous = 0;

    size_t offset = 0;
    while (offset < text.size())
    {
        const char32_t codepoint = decodeUtf8(text, offset);
        if (codepoint == 0)
            break;

        const GlyphInfo* glyph = _atlas->glyph(codepoint);
        if (!glyph)
            continue;

        if (previous != 0)
            penX += _atlas->kerning(previous, codepoint) * scale;

        if (glyph->size.x > 0.0f && glyph->size.y > 0.0f)
        {
            const glm::vec2 topLeft{penX + glyph->bearing.x * scale,
                                    origin.y + glyph->bearing.y * scale};

            _texturedQuad({topLeft, topLeft + glyph->size * scale},
                          glyph->uvMin, glyph->uvMax, color);
        }

        penX += glyph->advance * scale;
        previous = codepoint;
    }
}

void Context::_textCentered(std::string_view text, const Rect& bounds, const glm::vec4& color)
{
    const glm::vec2 size = measureText(text);

    _text(text,
          {bounds.min.x + (bounds.width() - size.x) * 0.5f,
           bounds.min.y + (bounds.height() - size.y) * 0.5f},
          color);
}

void Context::_uploadAtlasChanges()
{
    const auto pending = _atlas->takeDirtyUpload();
    if (!pending)
        return;

    _renderer->updateTextureRegion(_atlasTexture,
                                   pending->region.x, pending->region.y,
                                   pending->region.width, pending->region.height,
                                   pending->rgba.data());
}

} // namespace aura3d::ui
