# 3. Engine & Renderer

## `Engine`: the entry point

```cpp
#include "aura/Core/Engine.h"

Engine engine("settings.json");
```

The constructor, in order:
1. Sets up logging (level and optional file output — see
   [02-project-configuration.md](02-project-configuration.md#logging--live)).
2. Loads `settings.json` into `AuraSettings::get()`.
3. Builds a `wma::WindowDetails` from the `window.*` settings.
4. Resolves `renderer.backend` through `RendererFactory` and constructs that
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

## `IRenderer`: the interface every backend implements

All three backends (Vulkan, OpenGL, CPU) implement the same
`aura3d::IRenderer` abstract class. Your game code is written once against
this interface and never needs to know which backend is actually running.

The methods you'll use directly, grouped by purpose:

**Per-frame draw cycle**
```cpp
r->beginRenderPass();   // clears the framebuffer, starts recording
// ... setTransform / bindMaterial / drawMesh / drawBatch2D calls ...
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
r->drawBatch2D(vertices, indices, texture);     // one batch, one draw call — see doc 7
```

**State**
```cpp
r->setClearColor(r, g, b, a);
r->setLight(lightUBO);                         // see doc 6
```

Handles (`MeshHandle`, `TextureHandle`, `MaterialHandle`, ...) are opaque
`u32` values; `INVALID_HANDLE` (`aura3d::INVALID_HANDLE`) is the sentinel for
"nothing" / "failed to create". Passing an invalid handle where a texture is
expected is defined to mean "keep whatever is already bound", not a crash.

## The run loop

```cpp
r->run([&]() {
    // your per-frame update + draw code
    r->beginRenderPass();
    // ...
    r->endRenderPass();
});
```

`IRenderer::run()`:
- registers **Escape → quit** on the keyboard automatically, so every Aura3D
  app gets a working close key for free;
- hands your callback to `wma::IWindowManager::process()`, which wraps it as
  `beginFrame(); yourCallback(); endFrame();` every frame;
- on desktop this blocks until the window closes; on WASM it registers the
  browser's `requestAnimationFrame` loop and returns immediately (your
  callback and everything it captures by reference must stay alive after
  `run()` returns, on that platform).

You almost never call `beginFrame`/`endFrame` yourself — `run()` does it.
`beginRenderPass`/`endRenderPass` are yours to call, once per frame, around
your draw calls.

## Backend resolution & fallback

`renderer.backend` in `settings.json` is a request, not a guarantee.
`RendererFactory` resolves it:

```cpp
RendererFactory::defaultChoice();       // best backend compiled into this binary
RendererFactory::isAvailable(choice);   // was `choice` compiled in? (AURA_HAS_* defines)
RendererFactory::resolve(choice);       // choice, or the nearest available one
RendererFactory::create(choice, details); // constructs it, falling back with a warning
```

The fallback order is always **Vulkan → OpenGL → Software**. Per-platform
compile-time defaults (`defaultChoice()`, used when `settings.json` names a
backend that isn't even a known string):

| Platform | Default |
|---|---|
| WASM | OpenGL (WebGL2) |
| Android | Vulkan |
| Desktop | Vulkan → OpenGL → Software |

`Engine::getBackend()` always reflects what's *actually* running, which may
differ from what `settings.json` asked for — check it rather than assuming
the requested string won.

## Present modes (Vulkan)

`window.vsync_mode` maps to a Vulkan present mode via `aura3d::VSyncMode`:

| Value | Behavior |
|---|---|
| `AutoVsync` | Portable "vsync on": `FifoRelaxed` if the surface supports it, else `Fifo`. |
| `AutoNoVsync` | Portable "vsync off": `Immediate` if supported, else `Mailbox`, else `Fifo`. |
| `Fifo` | Guaranteed supported by every Vulkan implementation; classic double/triple-buffered vsync. |
| `FifoRelaxed` | Fifo, but presents immediately if the app is already late (reduces stutter under load). |
| `Immediate` | No sync — lowest latency, tearing possible. |
| `Mailbox` | Triple-buffered, low-latency, no tearing. |

`Fifo` is the only mode every Vulkan implementation is required to support.
Requesting `FifoRelaxed`, `Immediate`, or `Mailbox` explicitly on a surface
that doesn't support it logs a warning and falls back to `Fifo`; the `Auto*`
modes silently try their listed alternatives in order with no warning, since
falling back is the whole point of asking for one of those.

This only applies to Vulkan; OpenGL and the CPU backend use `window.vsync` as
a plain on/off switch through their own present paths.

## Switching backends at runtime

```cpp
engine_renderer_before->cleanup(); 
engine.switchBackend(aura3d::RendererChoice::OPENGL);
```

`Engine::switchBackend()`:
- resolves the request the same way startup does (falls back rather than
  failing);
- is a no-op if the resolved choice is already active;
- tears down the old renderer and brings up the new one against the same
  window;
- **invalidates every handle** the old renderer issued. `ResourceManager`'s
  cache is stale after this — reload what you need through `engine.resources()`.

```cpp
engine.switchBackend(aura3d::RendererChoice::OPENGL);
IRenderer* r = engine.getRenderer();               // re-fetch: it's a new object
const TextureHandle tex = engine.resources()->loadTexture("crate.png"); // reload
```

## Software (CPU) backend notes

The CPU backend is a real rasterizer, not a stub: perspective-correct
barycentric interpolation, a depth buffer, Gouraud shading identical in
spirit to the GPU backends' lighting model. It presents through the window
manager's software-framebuffer path (`wma::IWindowManager::lockFramebuffer`/
`presentFramebuffer`), so it works on any platform wma supports, not just
one windowing backend.

Because it has no GPU descriptor limits or pipeline objects, `graphics.*`
(MSAA, GPU preference) and `renderer.validation_layers` don't apply to it —
those are Vulkan-specific knobs.

Next: **[04-camera.md](04-camera.md)**.
