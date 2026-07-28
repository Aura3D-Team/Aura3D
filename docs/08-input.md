# 8. Input

Input isn't part of `IRenderer` — it belongs to the window, which comes from
`wma` (the windowing library Aura3D is built on):

```cpp
auto* windowManager = r->getWindowManager();
wma::KeyboardListener& keyboard = windowManager->getKeyboardListener();
wma::MouseListener& mouse = windowManager->getMouseListener();
```

Both listeners share the same shape: a context stack for organizing
bindings, and press/release (keyboard) or press/release/move/scroll (mouse)
callbacks per key or button.

## Keyboard

```cpp
wma::InputContextId gameplay = keyboard.createContext();
keyboard.setActiveContext(gameplay);

bool moveForward = false;
keyboard.addKeyAction(wma::KEY_W, wma::KeyAction{
    [&moveForward]() { moveForward = true; },   // on press
    [&moveForward]() { moveForward = false; },  // on release
});
```

The "held key drives a bool, movement code reads the bool every frame"
pattern above (from `apps/Sandbox/main.cpp`) is the standard way to handle
continuous movement — a helper makes it terser for several keys at once:

```cpp
auto bindHeld = [&keyboard](wma::Key key, bool& flag) {
    keyboard.addKeyAction(key, wma::KeyAction{
        [&flag]() { flag = true; },
        [&flag]() { flag = false; }
    });
};
bindHeld(wma::KEY_W, moveForward);
bindHeld(wma::KEY_S, moveBack);
bindHeld(wma::KEY_A, moveLeft);
bindHeld(wma::KEY_D, moveRight);
bindHeld(wma::KEY_SPACE, moveUp);
bindHeld(wma::KEY_LEFT_SHIFT, moveDown);
```

For a one-shot action (jump, fire, pause), just don't bind a release
callback — `KeyAction{ onPress }` alone is enough.

`removeKeyAction(key)` / `hasKeyAction(key)` manage bindings later;
`clearKeyActions()` drops everything in a context.

**`KEY_ESCAPE` is reserved.** `IRenderer::run()` binds it to close the app
before your callback ever runs, on whichever context is active at that
point. Rebind it if you need Escape to do something else (open a pause menu,
for instance) — your binding simply replaces the reserved one.

## Contexts

Contexts let you swap an entire binding set at once — gameplay vs. a pause
menu vs. a text-input field — without manually unbinding and rebinding every
key:

```cpp
wma::InputContextId gameplay = keyboard.createContext();
wma::InputContextId menu     = keyboard.createContext();

keyboard.addKeyAction(wma::KEY_SPACE, jumpAction, gameplay);
keyboard.addKeyAction(wma::KEY_RETURN, confirmAction, menu);

keyboard.setActiveContext(menu);   // only `menu`'s bindings fire now
keyboard.pushContext(gameplay);    // stack it instead, to layer rather than replace
keyboard.popContext();
```

Mouse contexts work identically via the same methods on `MouseListener`.

## Mouse

```cpp
mouse.setCursorEnabled(false); // hide + lock, for a mouse-look camera

mouse.setMoveAction(wma::MouseAction{[&](const wma::WMAMousePosition& pos) {
    camYaw   += static_cast<float>(pos.deltaX) * sensitivity;
    camPitch -= static_cast<float>(pos.deltaY) * sensitivity;
}});

mouse.addButtonAction(wma::MouseButton::WMALeft, wma::MouseAction{
    [&]() { firePressed = true; },   // press
    [&]() { firePressed = false; }   // release
});

mouse.setScrollAction(wma::MouseAction{[&](const wma::WMAMouseScroll& s) {
    zoom -= static_cast<float>(s.yOffset);
}});
```

```cpp
struct WMAMousePosition { f64 x, y, deltaX, deltaY; };
struct WMAMouseScroll   { f64 xOffset, yOffset; };

namespace wma::MouseButton {
    constexpr i32 WMALeft = 0, WMARight = 1, WMAMiddle = 2;
    constexpr i32 WMAButton4 = 3, WMAButton5 = 4, WMAButton6 = 5, WMAButton7 = 6, WMAButton8 = 7;
}
```

`mouse.getCurrentPosition()` / `mouse.isCursorEnabled()` read current state
outside a callback, if you need a poll-style check instead of an event.

## Per-frame timing

Not input, but almost always read alongside it — `windowManager->getWindowFlags()`
exposes `deltaTime` (milliseconds) and `fps`, both live-updated once per
frame:

```cpp
const float dt = static_cast<float>(windowManager->getWindowFlags()->deltaTime) / 1000.0f;
camera.setPosition(camera.position() + velocity * dt);
```

Next: **[09-building-a-game.md](09-building-a-game.md)** — putting all eight
chapters together into one playable scene.
