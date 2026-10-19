# 3. Engine & Renderer

## `Engine`: the entry point

```cpp
#include "aura/Core/Engine.h"

Engine engine("settings.json");
```

The constructor, in order:
1. Sets up logging (level and optional file output — see
   [02-project-configuration.md](02-project-configuration.md#the-file)).
2. Loads the settings layers into `AuraSettings::get()`.
3. Builds a `wma::WindowDetails` from the `window.*` settings.
4. Resolves `renderer.api` through `RendererFactory` and constructs that
   renderer, which creates the actual OS window.

After construction you have a live window and a ready-to-draw renderer:

```cpp
IRenderer* r = engine.getRenderer();
ResourceManager* resources = engine.resources();
const AuraSettings* settings = engine.getSettings();
aura3d::RendererChoice backend = engine.getBackend();
```

`Engine` owns the renderer and the `ResourceManager` for its whole lifetime —
you don't call `new`/`delete` on either.

## Window decorations

```cpp
aura3d::AuraConfig config;
config.set(aura3d::setting::windowDecorations, wma::DecorationMode::ClientSide); // or "decorations": "client"
Engine engine(config, "settings.json");

aura3d::ui::UIView ui(*engine.getRenderer()); // owns a DefaultWindowDecoration
ui.setWindowTitle("My application");
```

The decoration frames `root().setContent()`: a title bar with the title
centred, and — while the window is resizable and not maximized — a thin border
in the same colour. `root().contentBounds()` is what remains; 3D rendering still
covers the full framebuffer.

Moving, resizing, resize cursors and double-click maximize are the window
manager's: `attachInput()` installs the decoration's `windowHit()` as the
window's `setHitTest()`, and every WMA backend acts on it natively. The bar is
hidden on server-decorated, fullscreen and non-toplevel windows, wherever the
backend has no native move (GLFW off X11, the browser, Android), and Wayland
may override the request either way.

```cpp
auto bar = std::make_unique<aura3d::ui::DefaultWindowDecoration>();
bar->titleLabel().style().textColor({0.8f, 0.9f, 1.0f, 1.0f});
ui.setWindowDecoration(std::move(bar)); // nullptr removes it
```

For a different layout, derive from `IWindowDecoration`: reserve the frame with
`contentInsets()` (or `layout().padding`), place children in `arrangeContent()`
and implement `update(window, title)`.

| Region | Reported by `windowHit()` | Handled by |
|---|---|---|
| Content, buttons, any hit-testable child | `wma::WindowHit::Client` | AuraUI |
| Frame background | `Caption` | WMA: move, double-click maximize |
| Border (override) | `Top`, `BottomRight`, … | WMA: resize and its cursor |

Buttons call `window.minimize()`, `window.close()` and
`window.isMaximized() ? window.restore() : window.maximize()`. Make labels in
the bar non-hit-testable (`setHitTestVisible(false)`) so they drag.

## `IRenderer`: the interface every backend implements

All four backends (Vulkan, OpenGL, Metal, CPU) implement the same
`aura3d::IRenderer` abstract class. Your game code is written once against
this interface and never needs to know which backend is actually running.

The methods you'll use directly, grouped by purpose:

**Per-frame draw cycle**
```cpp
r->beginRenderPass();   // clears the framebuffer, starts recording
// ... setTransform / bindMaterial / drawMesh / drawBatch calls ...
r->endRenderPass();     // ends recording; endFrame() presents (handled by run())
```

**Resources** (see [05-meshes-materials-textures.md](05-meshes-materials-textures.md) for the full picture)
```cpp
r->createMesh(mesh);                          // -> MeshHandle
r->createMaterial(material);                  // -> MaterialHandle
r->createTextureFromFile(path);               // -> TextureHandle
r->createTextureFromPixels(rgba, w, h);        // -> TextureHandle
r->createCheckerboardTexture();                // -> TextureHandle (missing-asset fallback pattern)
r->createSolidColorTexture(r, g, b, a);        // -> TextureHandle
```

**Drawing**
```cpp
r->setTransform(camera.buildUBO(modelMatrix)); // upload model/view/proj for the next draw
r->bindMaterial(materialHandle);
r->drawMesh(meshHandle);                       // the primary 3D draw path
r->drawBatch(canvas, texture);               // a gfx::Canvas, one draw call — see doc 7
```

**State**
```cpp
r->setClearColor(r, g, b, a);                  // linear, like every colour (doc 7)
r->setLight(lightUBO);                         // see doc 6
```

**GPU time**
```cpp
r->setGpuTimingEnabled(true);                  // profile builds; or "renderer": { "gpu_timing": true }
if (const GpuTimingStats gpu = r->gpuTiming(); gpu.available)
    INK_INFO << "GPU " << gpu.frameMillis << " ms";
```
The last frame the GPU finished, a few frames behind the CPU: Vulkan, OpenGL
and Metal, in `AURA_PROFILE_FRAME` builds only, where it is on by default and
logged as `[gpu]` beside the CPU phases. Any other build, and the software
backend, report it unavailable, so the call needs no `#ifdef`.

Handles (`MeshHandle`, `TextureHandle`, `MaterialHandle`, ...) are opaque,
type-safe wrappers around a `u32` (`aura3d::Handle<Tag>`, one instantiation per
resource kind, so a `TextureHandle` cannot be passed where a `MeshHandle` is
expected — the compiler rejects it). A default-constructed handle (`{}`) is
the sentinel for "nothing" / "failed to create"; `isValidHandle(handle)` tests
for it. A default texture in `drawMesh()` preserves the current binding;
`drawBatch(..., {})` draws with vertex color alone. Those defaults do not
make stale handles safe after a renderer switch.

## The run loop

```cpp
r->run([&]() {
    // Update application state here, including frames without a render target.
    if (!r->frameBegun())
        return;
    r->beginRenderPass();
    // ...
    r->endRenderPass();
});
```

`IRenderer::run()`:
- hands your callback to `wma::IWindowManager::process()`, which wraps it as
  `beginFrame(); yourCallback(); endFrame();` every frame;
- on desktop this blocks until the window closes; on WASM it registers the
  browser's `requestAnimationFrame` loop and returns immediately (your
  callback and everything it captures by reference must stay alive after
  `run()` returns, on that platform).

Install input actions, including any Escape shortcut, through the window
manager. `run()` does not install a quit key. Its callback is invoked even
when a frame could not be acquired, so retain pending redraw work until
`frameBegun()` is true.

You call `beginFrame()`/`endFrame()` yourself only in a custom window loop.
`beginRenderPass()`/`endRenderPass()` remain application calls around the
frame's drawing. A loop that draws only on change must also draw while
`needsFrame()` is true, for resize and presentation recovery.

## Backend resolution & fallback

`renderer.api` in `settings.json` is a request, not a guarantee.
`RendererFactory` resolves it:

```cpp
RendererFactory::defaultChoice();       // best backend compiled into this binary
RendererFactory::isAvailable(choice);   // was `choice` compiled in? (AURA_HAS_* defines)
RendererFactory::resolve(choice);       // choice, or the nearest available one
RendererFactory::create(choice, details); // constructs it, falling back with a warning
```

If the requested backend was compiled in, it is used. Otherwise resolution
tries the platform default first, then the available entries in
**Vulkan → Metal → OpenGL → Software**. Availability here means compiled
support, not successful device initialization. Platform defaults are:

| Platform | Default |
|---|---|
| WASM | OpenGL (WebGL2) |
| macOS / iOS | Metal |
| Android | Vulkan |
| Other desktop platforms | Vulkan → OpenGL → Software |

`Engine::getBackend()` always reflects what's *actually* running, which may
differ from what `settings.json` asked for — check it rather than assuming
the requested string won.

## Present modes (Vulkan)

`present.mode` maps to a Vulkan present mode via `aura3d::VSyncMode`:

| Value | Behavior |
|---|---|
| `fifo` | Guaranteed supported by every Vulkan implementation; waits for the display, never tears. The default. |
| `fifo_relaxed` | Fifo, but presents immediately if the app is already late (reduces stutter under load; may tear). |
| `immediate` | No sync — lowest latency, tearing possible. |
| `mailbox` | Triple-buffered, low-latency, no tearing. |
| `uncapped` | The best non-waiting mode the surface has: `immediate`, else `mailbox`, else `fifo`. |

`fifo` is the only mode every Vulkan implementation is required to support.
Requesting `fifo_relaxed`, `immediate`, or `mailbox` explicitly on a surface
that doesn't support it logs a warning and falls back to `fifo`; `uncapped`
silently tries its alternatives in order, since falling back is the whole point
of asking for it.

Vulkan uses the mode itself. OpenGL and Metal take only on/off from it
(`fifo` and `fifo_relaxed` = on). The CPU backend does not wait for the
display (Wayland aside, where the compositor's buffer hand-back paces it); a
loop that renders continuously there wants `immediate` with a `present.fps_limit`.

## Switching backends at runtime

`engine.switchBackend(aura3d::RendererChoice::OPENGL)` performs cleanup
itself; do not clean up the active renderer before calling it.

`Engine::switchBackend()`:
- resolves the request the same way startup does (falls back rather than
  failing);
- is a no-op if the resolved choice is already active;
- tears down the old renderer and window, then creates their replacements
  from the current configuration;
- **invalidates every handle** the old renderer issued and clears/rebinds the
  `ResourceManager` cache. Reload assets through `engine.resources()` and
  recreate renderer-dependent helpers such as text overlays and UI views.

```cpp
engine.switchBackend(aura3d::RendererChoice::OPENGL);
IRenderer* r = engine.getRenderer();               // re-fetch: it's a new object
const TextureHandle tex = engine.resources()->loadTexture("crate.png"); // reload
```

## Software (CPU) backend notes

The CPU backend is a real rasterizer, not a stub: perspective-correct
barycentric interpolation, a depth buffer, Gouraud shading identical in
spirit to the GPU backends' lighting model. Rasterization is parallel — a
frame's triangles are binned into row bands, about four per worker, which
workers take as they finish, shading 8 pixels at a time with AVX2 (4 with SSE2
or NEON) — and it presents through the window manager's software-framebuffer path
(`wma::IWindowManager::lockFramebuffer`/`presentFramebuffer`), so it works
with any `window.backend` wma supports (see
[02-project-configuration.md](02-project-configuration.md#window--live)), not
just one.

Because it has no GPU descriptor limits or pipeline objects, `graphics.gpu_preference`,
`graphics.msaa_samples`, and `renderer.validation_layers` don't apply to it —
those are Vulkan-specific knobs. `graphics.cpu_threads` is the one `graphics.*`
key it *does* read: it pins the worker count (`0` auto-detects).

Next: **[04-camera.md](04-camera.md)**.
