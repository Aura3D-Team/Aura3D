#include "aura/UI/UIView.h"

#include <string>

#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/UI/Text/Utf8.h"

namespace aura3d::ui
{

namespace
{

[[nodiscard]] PointerButton toPointerButton(i32 button) noexcept
{
    switch (button)
    {
    case wma::MouseButton::WMARight:
        return PointerButton::Right;
    case wma::MouseButton::WMAMiddle:
        return PointerButton::Middle;
    default:
        break;
    }

    return PointerButton::Left;
}

} // namespace

UIView::UIView(IRenderer &renderer, const UIViewDesc &desc, std::unique_ptr<IWindowDecoration> decoration)
    : _renderer(&renderer),
      _shaper(TextShaperDesc{.fontPath = desc.fontPath, .pageSize = desc.glyphPageSize, .maxPages = desc.maxFontSizes}),
      _root(_shaper, desc.theme), _backend(renderer, _shaper),
      _windowTitle(desc.windowTitle.empty() ? AuraSettings::get()->getWindowTitle() : desc.windowTitle)
{
    _root.setDecoration(std::move(decoration));
    if (!desc.windowTitle.empty())
        if (auto *window = _renderer->getWindowManager())
            window->setTitle(_windowTitle.c_str());
    _syncSurface();
}

UIView::~UIView()
{
    detachInput();
}

void UIView::setWindowDecoration(std::unique_ptr<IWindowDecoration> decoration)
{
    _root.setDecoration(std::move(decoration));
    _syncSurface();
}

IWindowDecoration *UIView::windowDecoration() const noexcept
{
    return dynamic_cast<IWindowDecoration *>(_root.decoration());
}

void UIView::setWindowTitle(std::string title)
{
    _windowTitle = std::move(title);
    if (auto *window = _renderer->getWindowManager())
        window->setTitle(_windowTitle.c_str());
    _syncSurface();
}

void UIView::detachInput()
{
    _inputLifetime.reset();
    _root.cancelInput();
    if (_window)
    {
        _window->setTextInputEnabled(false);
        _window->setHitTest({});
    }
    _frameHitTest = false;
    _window = nullptr;
    _mouse = nullptr;
    _touchActive = false;
    _pointerKnown = false;
}

// -----------------------------------------------------------------------------
// Input
// -----------------------------------------------------------------------------

void UIView::attachInput(wma::IWindowManager &window)
{
    detachInput();
    _inputLifetime = std::make_shared<u8>(0);
    const std::weak_ptr<void> alive = _inputLifetime;
    _window = &window;
    _mouse = &window.getMouseListener();

    //! The decoration's regions drive native move, resize and caption double-click.
    _frameHitTest = window.setHitTest(
        [this](f64 x, f64 y)
        {
            const IWindowDecoration *decoration = windowDecoration();
            return decoration && decoration->visibility() == Visibility::Visible
                       ? decoration->windowHit({static_cast<f32>(x), static_cast<f32>(y)})
                       : wma::WindowHit::Client;
        });
    _syncSurface();

    for (const i32 button : {wma::MouseButton::WMALeft, wma::MouseButton::WMARight, wma::MouseButton::WMAMiddle})
    {
        _mouse->addButtonAction(button,
                                wma::MouseAction{[this, button, alive]()
                                                 {
                                                     if (alive.expired() || _touchActive)
                                                         return;
                                                     //! The cursor is read here rather than
                                                     //! trusted from the last frame: a
                                                     //! click is where the pointer is now.
                                                     _syncPointer();
                                                     _root.pointerDown(_pointer, toPointerButton(button),
                                                                       _window->getKeyboardListener().modifiers());
                                                 },
                                                 [this, button, alive]()
                                                 {
                                                     if (alive.expired() || _touchActive)
                                                         return;
                                                     _syncPointer();
                                                     _root.pointerUp(_pointer, toPointerButton(button),
                                                                     _window->getKeyboardListener().modifiers());
                                                 }});
    }

    wma::KeyboardListener &keyboard = window.getKeyboardListener();

    keyboard.setKeyEventAction(wma::KeyEventCallback::from(
        [this, alive](const wma::WMAKeyEvent &event)
        {
            if (alive.expired())
                return;
            if (event.isPressOrRepeat())
                _root.keyDown(event.key, event.mods, event.state == wma::KeyState::Repeat);
            else
                _root.keyUp(event.key, event.mods);
        }));

    keyboard.setTextInputAction(wma::TextInputCallback::from(
        [this, alive](wma::Codepoint codepoint)
        {
            if (alive.expired())
                return;
            //! One codepoint at a time, so no buffer has to survive between the
            //! platform's callback and the next frame.
            std::string encoded;
            utf8::append(encoded, codepoint);
            _root.textInput(encoded);
        }));

    wma::TouchListener &touch = window.getTouchListener();

    const auto trackFinger = [this](const wma::WMATouchPoint &point)
    {
        _pointer = {static_cast<f32>(point.x), static_cast<f32>(point.y)};
        _pointerKnown = true;
    };

    touch.setDownAction(wma::TouchInputCallback::from(
        [this, alive, trackFinger](const wma::WMATouchPoint &point)
        {
            if (alive.expired() || _touchActive)
                return;
            _finger = point.fingerId;
            trackFinger(point);
            _touchActive = true;

            //! A finger arrives already on the widget, so hover has to be
            //! established before the press or the press has nothing to land on.
            _root.pointerMoved(_pointer);
            _root.pointerDown(_pointer);
        }));

    touch.setMoveAction(wma::TouchInputCallback::from(
        [this, alive, trackFinger](const wma::WMATouchPoint &point)
        {
            if (alive.expired() || !_touchActive || point.fingerId != _finger)
                return;
            trackFinger(point);
            _root.pointerMoved(_pointer);
        }));

    touch.setUpAction(wma::TouchInputCallback::from(
        [this, alive, trackFinger](const wma::WMATouchPoint &point)
        {
            if (alive.expired() || !_touchActive || point.fingerId != _finger)
                return;
            trackFinger(point);
            _root.pointerUp(_pointer);
            _root.pointerLeft();
            _touchActive = false;
        }));
}

void UIView::_syncPointer()
{
    if (!_mouse || _touchActive)
        return;

    const wma::WMAMousePosition position = _mouse->getCurrentPosition();
    const glm::vec2 current{static_cast<f32>(position.x), static_cast<f32>(position.y)};

    if (_pointerKnown && current == _pointer)
        return;

    _pointer = current;
    _pointerKnown = true;
    _root.pointerMoved(_pointer);
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------

void UIView::_syncSurface()
{
    const wma::WindowDetails &details = _renderer->getWindowDetails();

    glm::vec2 logical{static_cast<f32>(details.width), static_cast<f32>(details.height)};
    f32 scale = 1.0f;

    if (wma::IWindowManager *window = _renderer->getWindowManager())
    {
        const wma::FramebufferSize framebuffer = window->getFramebufferSize();

        if (const wma::WindowDetails *live = window->getWindowDetails())
            logical = {static_cast<f32>(live->width), static_cast<f32>(live->height)};

        if (framebuffer.valid() && logical.x > 0.0f)
        {
            /*
             * The framebuffer is what drawBatch2D() draws into; the window's
             * logical size is what the pointer reports in. Their ratio is the
             * device-pixel ratio, and taking it from the two the platform
             * actually gives us is more dependable than any scale setting.
             */
            scale = static_cast<f32>(framebuffer.width) / logical.x;
        }
    }

    _root.setScale(scale);
    _root.resize(logical);
    if (auto *decoration = windowDecoration())
    {
        auto *window = _renderer->getWindowManager();
        const auto *details = window ? window->getWindowDetails() : nullptr;
        //! Without a native hit test nothing could move the window from the bar.
        const bool visible = details && _frameHitTest && window->isToplevel() && !details->fullscreen &&
                             window->getDecorationMode() == wma::DecorationMode::ClientSide;
        decoration->setVisibility(visible ? Visibility::Visible : Visibility::Collapsed);
        if (window)
            decoration->update(*window, _windowTitle);
    }
}

void UIView::render(f32 deltaSeconds)
{
    update(deltaSeconds);
    draw();
}

bool UIView::needsDraw() const noexcept
{
    return !_drawn || _root.needsPaint() || _renderer->needsFrame();
}

void UIView::draw()
{
    //! Rebuilt only when the tree re-recorded. An idle UI goes straight to
    //! submit() with the vertex buffers it already has.
    if (_root.paint(_list))
        _backend.build(_list, _root.scale());

    _backend.submit();
    _drawn = true;
}

void UIView::update(f32 deltaSeconds)
{
    _syncSurface();
    if (_window)
    {
        const bool focused = _window->getWindowFlags()->focused;
        if (_windowFocused && !focused)
        {
            _root.cancelInput();
            _touchActive = false;
            _pointerKnown = false;
        }
        _windowFocused = focused;
    }
    _syncPointer();

    if (_mouse && !_touchActive)
    {
        const wma::WMAMouseScroll scroll = _mouse->consumeScrollDelta();

        if (scroll.xOffset != 0.0 || scroll.yOffset != 0.0)
            _root.wheel({static_cast<f32>(scroll.xOffset), static_cast<f32>(scroll.yOffset)}, _pointer,
                        _window->getKeyboardListener().modifiers());
    }

    _root.update(deltaSeconds);

    if (_window && _window->isTextInputEnabled() != _root.capturesTextInput())
        _window->setTextInputEnabled(_root.capturesTextInput());
}

} // namespace aura3d::ui
