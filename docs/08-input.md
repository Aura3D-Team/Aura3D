# 8. Input

Input isn't part of `IRenderer` — it belongs to the window, which comes from
`wma` (the windowing library Aura3D is built on):

```cpp
auto *windowManager = r.getWindowManager();
wma::KeyboardListener& keyboard = windowManager->getKeyboardListener();
wma::MouseListener& mouse = windowManager->getMouseListener();
```

Both listeners share the same shape: a context stack for organizing
bindings, and press/release (keyboard) or press/release/move/scroll (mouse)
callbacks per key or button.

## Keyboard

For continuous movement, ask whether a key is held, once a frame:

```cpp
r.run([&](const Frame &frame) {
    glm::vec3 move{0.0f};
    if (keyboard.isKeyDown(wma::KEY_W)) move += camera.forward();
    if (keyboard.isKeyDown(wma::KEY_S)) move -= camera.forward();
    if (glm::length(move) > 0.0f)
        camera.setPosition(camera.position() + glm::normalize(move) * speed * frame.dt);
    // ...
});
```

`isKeyDown()` follows the press and release events on every backend, needs no
binding, and forgets held keys when the window loses focus.

For an action that happens once per press (jump, fire, pause), bind it:

```cpp
keyboard.addKeyAction(wma::KEY_SPACE, wma::KeyAction{[&] { player.jump(); }});
// A release callback too: wma::KeyAction{onPress, onRelease}
```

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

Not input, but almost always read alongside it: `run()` hands its callback a
`Frame` with `dt` (seconds since the previous iteration) and `time` (seconds
since the loop started). `windowManager->getWindowFlags()->fps` is the frame
rate, averaged over half a second.

```cpp
r.run([&](const Frame &frame) {
    camera.setPosition(camera.position() + velocity * frame.dt);
});
```

Next: **[09-building-a-game.md](09-building-a-game.md)** — putting all eight
chapters together into one playable scene.
