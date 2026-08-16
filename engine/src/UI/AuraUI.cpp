#include "aura/UI/AuraUI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace aura3d::ui {

namespace {

//! Four vertices and six indices per quad, glyphs included.
constexpr size_t kVerticesPerQuad = 4;
constexpr size_t kIndicesPerQuad = 6;

/*
 * A panel's height is not known until endPanel(), so its content clip needs a
 * bottom edge that cannot reject anything. This is that edge: far below any
 * real framebuffer, yet nowhere near float's range, so the subtractions in the
 * clipping arithmetic stay exact.
 */
constexpr float kUnboundedBelow = 1.0e6f;

/// FNV-1a, 32-bit. Small, fast, and good enough to key widget identity.
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
            /*
             * A missing font degrades the look, it does not break the tool: the
             * embedded bitmap font needs no asset on disk, so the UI still
             * draws on platforms where none was staged (WASM, Android).
             */
            INK_WARN << loaded.error() << "; falling back to the embedded bitmap font";
        }
    }

    if (!_atlas)
        _atlas = FontAtlas::builtinBitmap(atlasDesc);

    /*
     * Claimed here, while the atlas is still empty and the reservation cannot
     * fail for want of room. Every solid quad the UI ever draws samples this
     * texel, which is what keeps rectangles and glyphs in one batch -- and
     * therefore the whole UI in one draw call.
     */
    const auto solidUv = _atlas->solidTexelUv();
    if (!solidUv)
    {
        INK_ERROR << "AuraUI: the glyph atlas is too small to reserve a solid texel";
        return;
    }
    _solidUv = *solidUv;

    /*
     * One texture for the context's whole life. Glyphs rasterized later are
     * patched in with updateTextureRegion(), never by allocating another
     * texture -- which on Vulkan would consume descriptor-pool slots that are
     * never given back.
     */
    _atlasTexture = _renderer->createDynamicTexture(_atlas->width(), _atlas->height());
    if (!isValidHandle(_atlasTexture))
    {
        INK_ERROR << "AuraUI: could not allocate the glyph atlas texture";
        return;
    }

    _usable = true;
}

Context::~Context() = default;

void Context::attachInput(wma::IWindowManager& windowManager)
{
    _mouse = &windowManager.getMouseListener();

    /*
     * Only the button is bound. The cursor position is polled in newFrame()
     * instead, so attaching the UI never displaces an application's own move
     * callback -- which, in a game, is usually driving the camera.
     */
    _mouse->addButtonAction(wma::MouseButton::WMALeft,
                            wma::MouseAction{[this]() { _pendingInput.mouseDown = true; },
                                             [this]() { _pendingInput.mouseDown = false; }});
}

void Context::newFrame() noexcept
{
    if (_mouse)
    {
        const wma::WMAMousePosition position = _mouse->getCurrentPosition();
        _pendingInput.mouse = {static_cast<float>(position.x), static_cast<float>(position.y)};
    }

    newFrame(_pendingInput);
}

void Context::newFrame(const Input& input) noexcept
{
    _input = input;
    _mousePressed = _input.mouseDown && !_previousMouseDown;
    _previousMouseDown = _input.mouseDown;

    //! Claims made while building the previous frame decide what is hot now;
    //! see the _hot declaration for why this is resolved a frame late.
    _hot = _nextHot;
    _nextHot = 0;

    //! Published before the reset so the answer describes the frame that was
    //! just completed, and therefore stays stable wherever it is queried.
    _wantsMouse = _wantsMouseThisFrame || _active != 0;
    _wantsMouseThisFrame = false;

    if (!_activeSubmitted)
        _active = 0;
    _activeSubmitted = false;

    _vertices.clear();
    _indices.clear();

    _inPanel = false;
    _clip = Rect{};
}

void Context::render()
{
    if (!_usable || _indices.empty())
        return;

    //! Strictly after the widgets ran -- they are what rasterized the glyphs
    //! this upload carries -- and strictly before the draw that samples them.
    _uploadAtlasChanges();

    _renderer->drawBatch2D(_vertices, _indices, _atlasTexture);
}

bool Context::beginPanel(std::string_view title, glm::vec2 defaultPosition, float width)
{
    if (!_usable || _inPanel)
        return false;

    const u32 id = hashBytes(title, kHashOffsetBasis);

    PanelState& state = _panels[id];
    if (!state.placed)
    {
        state.position = defaultPosition;
        state.placed = true;
    }

    const float titleHeight = _style.rowHeight;

    /*
     * Dragging is resolved before any geometry is emitted, so the panel is
     * drawn where the cursor has already put it. Emitting first and moving
     * afterwards would leave the panel trailing the cursor by a frame.
     */
    const Rect grabBar{state.position, state.position + glm::vec2{width, titleHeight}};

    _clip = grabBar;
    const Interaction bar = _behaviour(hashBytes("##title", id), grabBar);

    if (bar.held)
    {
        if (_mousePressed)
            _dragOffset = _input.mouse - state.position;

        state.position = _input.mouse - _dragOffset;

        /*
         * Clamped against the live window size -- the window manager's, not the
         * renderer's cached copy, so the constraint follows a resize -- keeping
         * a sliver of title bar on screen. Without it a panel can be flung past
         * the edge and left with nothing to grab it by.
         */
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

    /*
     * Recomputed after the drag, and separate from grabBar above for that
     * reason: hit-testing has to use where the bar was when the cursor was
     * over it, but drawing has to use where the drag has since put it.
     * Reusing one rectangle for both leaves the title bar and its text
     * trailing the panel body by a frame's worth of mouse movement.
     */
    const Rect titleBar{state.position, state.position + glm::vec2{width, titleHeight}};

    _panel = CurrentPanel{};
    _panel.id = id;
    _panel.bounds = Rect{state.position, {state.position.x + width, state.position.y + titleHeight}};
    _inPanel = true;

    //! Covers the whole column so neither the background nor the title bar is
    //! trimmed by the clip that the content will use.
    _clip = Rect{state.position, {state.position.x + width, state.position.y + kUnboundedBelow}};

    /*
     * The background is emitted now, before the content, so it sits behind it
     * -- but its height is only known at endPanel(). It goes down at the title
     * bar's height and endPanel() rewrites its two bottom vertices in place,
     * which costs nothing and keeps the draw order correct.
     */
    _panel.backgroundVertex = _vertices.size();
    _quad(_panel.bounds, _style.panelBackground);
    _panel.backgroundEmitted = _vertices.size() == _panel.backgroundVertex + kVerticesPerQuad;

    _quad(titleBar, _style.panelTitle);
    _textCentered(title, titleBar, _style.text);

    //! Content is clipped to the panel's column; see the _clip declaration for
    //! why the bottom edge is left unbounded.
    _clip = Rect{{state.position.x, state.position.y + titleHeight},
                 {state.position.x + width, state.position.y + kUnboundedBelow}};

    _panel.cursorY = state.position.y + titleHeight + _style.padding;

    return true;
}

void Context::endPanel()
{
    if (!_inPanel)
        return;

    //! cursorY sits one item-spacing past the last row; trade that gap for the
    //! panel's bottom padding.
    const float bottom = std::max(_panel.cursorY - _style.itemSpacing + _style.padding,
                                  _panel.bounds.min.y + _style.rowHeight);

    _panel.bounds.max.y = bottom;

    if (_panel.backgroundEmitted)
    {
        //! Corners were wound top-left, top-right, bottom-right, bottom-left,
        //! so the last two are the pair that follows the content's height.
        _vertices[_panel.backgroundVertex + 2].pos.y = bottom;
        _vertices[_panel.backgroundVertex + 3].pos.y = bottom;
    }

    if (_panel.bounds.contains(_input.mouse))
        _wantsMouseThisFrame = true;

    _inPanel = false;
}

void Context::label(std::string_view text)
{
    if (!_inPanel)
        return;

    const Rect row = _nextRow(_style.rowHeight);
    _text(text, {row.min.x, row.min.y + (row.height() - _lineHeight()) * 0.5f}, _style.text);
}

bool Context::button(std::string_view text)
{
    if (!_inPanel)
        return false;

    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(_idFor(text), row);

    const glm::vec4& fill = it.held      ? _style.controlActive
                          : it.hovered   ? _style.controlHovered
                                         : _style.control;

    _quad(row, fill);
    _textCentered(text, row, _style.text);

    return it.clicked;
}

bool Context::checkbox(std::string_view text, bool& value)
{
    if (!_inPanel)
        return false;

    //! The whole row is the hit target, not just the box: a 16-pixel square is
    //! a needlessly small thing to ask anyone to hit.
    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(_idFor(text), row);

    if (it.clicked)
        value = !value;

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

    return it.clicked;
}

bool Context::sliderFloat(std::string_view text, float& value, float min, float max)
{
    if (!_inPanel || !(max > min))
        return false;

    const Rect row = _nextRow(_style.rowHeight);
    const Interaction it = _behaviour(_idFor(text), row);

    const float clamped = std::clamp(value, min, max);
    bool changed = clamped != value;
    value = clamped;

    /*
     * Driven by held rather than hovered, so a drag that runs off the end of
     * the track keeps controlling the slider until the button comes up. Pinning
     * it to the cursor's x is what makes that feel continuous instead of
     * snapping back the moment the pointer leaves.
     */
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

    _textCentered(_caption, row, _style.text);

    return changed;
}

void Context::separator()
{
    if (!_inPanel)
        return;

    constexpr float kThickness = 1.0f;

    const Rect row = _nextRow(_style.itemSpacing * 2.0f + kThickness);

    //! Rounded so the rule lands on a pixel row and stays crisp rather than
    //! being smeared across two by the 2D pipeline's filtering.
    const float y = std::round((row.min.y + row.max.y) * 0.5f);

    _quad({{row.min.x, y}, {row.max.x, y + kThickness}}, _style.separator);
}

void Context::spacing(float pixels) noexcept
{
    if (_inPanel)
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
        _nextHot = id;

    if (_active == id)
    {
        _activeSubmitted = true;
        result.held = true;

        if (!_input.mouseDown)
        {
            //! A press only counts as a click when it is also released over the
            //! widget, so dragging off one before letting go cancels it.
            result.clicked = result.hovered;
            result.held = false;
            _active = 0;
        }
    }
    /*
     * The second disjunct covers the frame a widget is first hovered, where
     * _hot is still whatever the previous frame claimed. Requiring _hot to be
     * clear keeps it from letting a covered widget steal a press: when panels
     * overlap, the topmost one has already claimed _hot, so it is non-zero.
     */
    else if (_mousePressed && (_hot == id || (_hot == 0 && result.hovered)))
    {
        _active = id;
        _activeSubmitted = true;
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
    const Rect row{{_panel.bounds.min.x + _style.padding, _panel.cursorY},
                   {_panel.bounds.max.x - _style.padding, _panel.cursorY + height}};

    _panel.cursorY += height + _style.itemSpacing;

    return row;
}

u32 Context::_idFor(std::string_view text) noexcept
{
    //! The counter is folded in so two identically labelled widgets in one
    //! panel stay distinct -- a "Reset" beside another "Reset" is ordinary, and
    //! a shared id would make pressing either one light up both.
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

    const Rect visible{{std::max(bounds.min.x, _clip.min.x), std::max(bounds.min.y, _clip.min.y)},
                       {std::min(bounds.max.x, _clip.max.x), std::min(bounds.max.y, _clip.max.y)}};

    if (visible.width() <= 0.0f || visible.height() <= 0.0f)
        return;

    //! Trimming the corners moves the UV rectangle by the same fractions, which
    //! for an axis-aligned quad is exact: the surviving texels are untouched.
    const glm::vec2 uv0{lerp(uvMin.x, uvMax.x, (visible.min.x - bounds.min.x) / width),
                        lerp(uvMin.y, uvMax.y, (visible.min.y - bounds.min.y) / height)};
    const glm::vec2 uv1{lerp(uvMin.x, uvMax.x, (visible.max.x - bounds.min.x) / width),
                        lerp(uvMin.y, uvMax.y, (visible.max.y - bounds.min.y) / height)};

    const auto base = static_cast<u32>(_vertices.size());

    //! Wound top-left, top-right, bottom-right, bottom-left; endPanel() relies
    //! on that order to patch a panel background's height.
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

        //! Kerning tightens specific pairs (e.g. "AV"); zero for most.
        if (previous != 0)
            penX += _atlas->kerning(previous, codepoint) * scale;

        //! Whitespace carries an advance but no cell, so it emits no quad.
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
    /*
     * One sub-image copy covers every glyph rasterized since the last upload,
     * because FontAtlas unions their cells into a single dirty rectangle. Once
     * the UI's vocabulary has been seen once, nothing is pending and this costs
     * a branch.
     */
    const auto pending = _atlas->takeDirtyUpload();
    if (!pending)
        return;

    _renderer->updateTextureRegion(_atlasTexture,
                                   pending->region.x, pending->region.y,
                                   pending->region.width, pending->region.height,
                                   pending->rgba.data());
}

} // namespace aura3d::ui
