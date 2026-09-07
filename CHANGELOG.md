# Changelog

All notable changes to Aura3D are documented in this file.

## [0.2.2]

### Added

- `FontAtlas::cornerRingMask()` and a rounded-ring path in `DrawListRenderer`: an outline is now built as a ring rather than laid down as a filled rounded rect for the fill to cover. A `strokeRect()`, a focus ring and any bordered widget whose fill is transparent or translucent used to arrive as a solid block of the border colour — every focused control, and every text field over a panel
- `Widget::effectivelyVisible()`, `Widget::onPointerCancel()` and `UIRoot::cancelInput()`: the three pieces a host needs to drop pointer capture, hover and focus when a window loses focus or a subtree is hidden
- Test suites `test_aui_lifecycle` and `test_aui_signal`; `test_aui_paint` gained backend-coverage checks that assert what the renderer actually filled, not what the draw list recorded

### Changed

- AuraUI source reorganized: `InputRouter` folded into `UIRoot` and `OverlayLayer`, widgets split into `Widgets/{Basic,Controls,Layouts,Menus,Navigation}`, `Style.cpp` became `Core/Theme.cpp`. `test_ui`/`test_ui_input` replaced by the per-area `test_aui_*` suites
- `IRenderer::run()` and the Vulkan and Metal backends no longer bind Escape to `cleanup()`. The binding destroyed the window from inside a live input callback, and Escape belongs to the application — a shell needs it to dismiss a popup
- `Selectable` insets its text by the style's padding, as `Button` already did; a tab strip drew as touching words without it

### Fixed

- `Signal` iterated a vector its own callbacks could reallocate, so connecting or disconnecting from inside an emission was a use-after-free
- Hiding a widget left the root holding it as focused, hovered or pointer-captured, so a hidden subtree kept taking input
- Pointer dispatch, the hover chain and the animation list walked raw pointers across handlers free to destroy them
- `AtlasTextShaper::setScale()` dropped every glyph page while retained runs still held page indices and UVs, so a DPI change blanked or corrupted text already on screen
- Corner masks allocated while a frame's quads were being emitted were uploaded a frame late
- Tab could leave a modal overlay, and focus could be set outside one

## [0.2.1]

### Fixed

- `cmake/Dependencies.cmake` never actually looked for glm — it resolved by accident wherever ink/wma's interface include path happened to carry it, so a from-scratch Windows configure failed on `glm/glm.hpp`. `find_package(glm CONFIG)` first, falling back to `find_path` plus an imported target where glm ships no CMake package; `cmake/Aura3DConfig.cmake.in` reproduces whichever path was taken
- Android install skipped `install()` entirely — no CMake package, no `find_package(Aura3D)`
- iOS: `std::format_to` needs 16.3+ (deployment target raised 16.0 → 16.3)
- Windows CI: vcpkg static triplet needs `CMAKE_MSVC_RUNTIME_LIBRARY` set explicitly

### Docs

- `IRenderer` frame-lifecycle comments rewritten to explain `run()` vs. hand-rolled loops instead of restating function names

## [0.2.0]

### Added

- `AudioEngine` (`Engine::audio()`, never null): WAV + Ogg Vorbis loading, fixed voice pool, per-voice/master gain, looping, pause/resume, 3D positional audio (distance attenuation + equal-power panning). Device I/O is `wma::IAudioDevice` (ALSA/SDL3/null). `ResourceManager::loadSound()`, new `audio` settings section. See docs/14
- Metal backend (`aura3d::mtl::MetalRenderer`), fourth `IRenderer` impl, Apple-only. Per-responsibility managers mirroring the Vulkan backend; MSL shaders embedded as source + optional precompiled `.metallib`; `vendor/metal-cpp` vendored private
- **AuraUI**, a retained widget toolkit (`aura3d::ui`), one include (`<aura/UI/UI.hpp>`), gated by `AURA_ENABLE_UI`. Layered: Core (values/signals/theme) → Text (shaping/wrap/carets, IME-correct, caret/selection, word wrap) → Layout (flex/grid constraints) → Widgets → UIRoot (dispatch/focus/Tab navigation/touch/shortcuts) → Backend (DrawList → draw calls, one draw call regardless of widget count via `FontAtlas::solidTexelUv()`, nine-slice rounded corners against an analytic coverage mask, no steady-state allocation) → UIView (wired to renderer+window). `Label`/`Image`/`Separator`/`Button`/`CheckBox`/`RadioButton`+`RadioGroup`/`Slider`/`ProgressBar`/`TextField`, built by composition. Accessibility tree, DPI-independent layout. See docs/14, `apps/AuraUIDemo`, and `apps/Sandbox`
- **AuraUI overlay layer** (`UIRoot::overlay()`): one mechanism for everything that escapes its parent's rectangle. `OverlayDesc` gives anchor + `Placement` (Below/Above/Right/Left/Over/Cursor/Center, each flipping then clamping to stay on screen), modality, light dismissal on outside-click and Escape, and an `onClosed` hook. Closing is deferred to the frame boundary, so a menu item can close the menu it lives in. Tab is trapped in the topmost overlay; Escape closes it before the focused widget sees the key
- AuraUI widgets on that layer: `Dropdown` (Space opens, arrows move, Enter takes, Escape closes; arrows step the selection while shut), `Menu` with items, shortcut labels, separators and nested submenus, and tooltips (`Widget::setTooltip()`, dwell-opened by the root, click-through so they never shield what is under them)
- AuraUI navigation widgets: `TabView` (animated selection underline, Left/Right to move), `CollapsingHeader` and `TreeNode` over a shared `Disclosure` base, and `Selectable` — one row widget that serves lists, drop-downs, menus and tabs by retargeting its `Part` (`Widget::setPart()`)
- `FontAtlas::convexMask()`: exact per-texel coverage of any convex polygon, by clipping it against each texel's square and taking the shoelace area — the general form of `cornerMask()`. Icons (`ui::icon::triangle`, `ui::icon::convex`) are one atlas cell and one textured quad each, antialiased, in the same batch as the text beside them, so a chevron or a disclosure arrow costs no draw call, no new command type and no backend case
- `Property::bind()` / `bindFrom()`: one-way reactive links between properties, undone by dropping the returned `ScopedConnection`
- `DrawList::drawMask()` (`DrawCommandType::Mask`): a tinted quad sampling a glyph-atlas cell — the one primitive every icon needs
- Type-safe resource handles: `Handle<Tag>` per resource kind (`VertexBufferHandle`, `TextureHandle`, `MeshHandle`, ...) instead of raw `u32` — cross-type misuse is now a compile error. Same runtime shape/cost
- Engine config in code (`AuraConfig`) — `Engine(config)` needs no settings file; `Engine(config, path)` layers a file over it. See docs/02
- WebAssembly is now installable (`find_package(Aura3D)` on the `wasm` preset)
- Tests: `test_audio_clip_loader`, `test_audio_engine`, `test_settings`, `test_texture_sampling`, plus five AuraUI suites — `test_aui_layout`, `test_aui_input`, `test_aui_text`, `test_aui_paint`, `test_aui_overlay` (228 checks: layout arithmetic, hit testing/capture, caret/byte round trips, single-batch and bounded-geometry properties, overlay placement/modality/deferred-close/focus-trapping)

### Changed

- `TextOverlay::drawFPS` now smooths (EMA, ~0.5s constant) instead of printing the raw per-frame reciprocal; `TextOverlay::fps()` reads it back
- Bilinear filtering on the software rasteriser's `Texture::sample()` (was point-sampled, undoing `FontAtlas`'s antialiasing)
- `Part` gained `Container` (transparent, for layout nodes) and `ProgressBar`; `Metrics` gained `fontSize`
- `Signal::connect` is not `[[nodiscard]]`
- Every platform installs into one prefix; library is ABI-tagged (`libaura3d_linux_x86_64.a`, etc.) instead of a per-platform directory
- `INVALID_HANDLE` removed — a default-constructed handle (`{}`) is now the invalid value

### Fixed

- Wayland: keyboard events (including AuraUI text input) could arrive with no xkb keymap loaded — subscribed a roundtrip too late (fixed in libwma)
- `cmake/Aura3DConfig.cmake.in` gated `find_dependency(OpenGL)` wrong for Emscripten

### Docs

- `docs/14-auraui-toolkit.md`: AuraUI, renumbered in ahead of Audio (12) and Debug & Benchmark Mode (13)

## [0.1.0]

First release: a working build across all three backends (Vulkan, OpenGL, CPU
software rasterizer) with file-based assets, camera, lighting, and materials
for multi-object 3D scenes.

### Added

- **GPU font rendering pipeline** (issue #14). Text costs a single draw call
  per batch instead of one per character, and no longer inherits the 3D
  scene's lighting.
  - `FontAtlas` (`aura/Core/AuraFont/FontAtlas.h`): loads `.ttf`/`.otf` at
    runtime via the vendored `stb_truetype` (`vendor/stb`), rasterizing each
    glyph lazily into a single large GPU texture (2048x2048 by default) packed
    with a shelf allocator. New characters are unioned into one dirty
    rectangle and uploaded as a sub-image, so a character costs a few hundred
    bytes rather than a full texture. Exposes UTF-8 decoding (`decodeUtf8`),
    kerning and vertical metrics, and falls back to the engine's embedded
    bitmap font when no font file can be loaded — which keeps text working on
    WASM and Android with no staged asset.
  - **Dedicated unlit 2D pipeline** on all three backends, independent of the
    3D scene: its own shaders (`resources/shaders/{vulkan,opengl}/*2d*`,
    `GL_VERTEX_2D`/`GL_FRAGMENT_2D`), an orthographic projection the backend
    derives from the framebuffer size, depth testing off and straight alpha
    blending on. Vulkan builds a second `VkGraphicsPipelineManager` with its
    own layout (one sampler in set 0, a 64-byte push-constant projection) and
    per-frame-in-flight dynamic vertex/index buffers.
  - `IRenderer::createDynamicTexture` / `updateTextureRegion`: allocate a
    texture once and patch sub-rectangles in place, so a growing atlas never
    re-consumes Vulkan descriptor-pool slots.
  - `IRenderer::drawBatch2D`: submits a whole batch of window-pixel-space 2D
    geometry as a **single draw call**.
  - `VkGraphicsPipelineManager::resetInterface` and the `PipelineOptions`
    struct (depth test / alpha blend / back-face culling), so a second pipeline
    can declare its own descriptor and push-constant interface.
  - `CpuFrameBufferManager::drawTriangle2D`: the software rasterizer's
    counterpart — affine interpolation, no depth interaction, source-over
    alpha blending.
  - `tests/test_font_atlas.cpp`: covers UTF-8 decoding, shelf packing,
    on-demand rasterization, dirty-rectangle tracking and load failures.
- **`TextOverlay`**: draws UTF-8 text (`std::string_view`) through the 2D
  pipeline above, with its own orthographic projection — no scene camera
  needed. Provides `measureText`, `lineHeight`, and per-call colour;
  configured through `TextOverlayDesc` (font path, pixel height, atlas size,
  colour).
- **Build system**: working CMake configuration for the engine, Sandbox app,
  and all three backends — `cmake/Assets.cmake` (`aura_compile_shaders`,
  `aura_copy_assets` helpers), `apps/Sandbox/CMakeLists.txt`,
  `scripts/gen_embedded_spirv.sh` to build `EmbeddedSpirv.h` from GLSL
  sources, `settings.wasm.json` for WASM builds.
- **Handle system**: `INVALID_HANDLE` sentinel and consistent 1-based handles
  for vertex buffers, index buffers, textures, meshes, and materials across
  all backends.
- **32-bit index buffers**: full `VK_INDEX_TYPE_UINT32` support on Vulkan
  alongside 16-bit, tracked per-buffer and used correctly at bind time.
- **Asset loading**
  - `ImageLoader`: decodes PNG/JPEG/TGA/BMP/PSD/GIF via `stb_image`, with an
    in-memory magenta/black checkerboard fallback when a file is missing or
    corrupt (`vendor/stb`).
  - `MeshLoader`: loads Wavefront OBJ via `tinyobjloader`, with vertex
    de-duplication, generated per-vertex normals when a file has none, and a
    cube fallback on failure (`vendor/tinyobj`). Includes embedded primitives:
    `createCube()`, `createSphere(subdivisions)`, `createPlane()`.
  - `IRenderer::createTextureFromFile`, `createCheckerboardTexture`,
    `createMesh`, `drawMesh` — backend-independent, built once on top of each
    backend's `createTextureFromPixels` primitive.
  - `ResourceManager`: path-keyed texture/mesh cache bound to a renderer,
    exposed via `Engine::resources()`.
- **Camera** (`aura::Camera`, header-only): perspective/orthographic
  projections, `lookAt` and yaw/pitch targeting, `buildUBO()` for direct use
  with `IRenderer::setTransform`.
- **Lighting**: `gfx::LightUBO` (directional light: direction, intensity,
  color, ambient) and `IRenderer::setLight`, implemented as Gouraud shading on
  the CPU backend, a uniform block on OpenGL, and a descriptor set on Vulkan.
- **Materials**: `Material` (albedo texture, tint, roughness, metallic) with
  `IRenderer::createMaterial` / `bindMaterial`.
- **Per-object transforms**: Vulkan pushes model + normal matrices via push
  constants on every draw, so multiple objects with distinct transforms
  render correctly within a single render pass.
- **Config**: `AuraSettings` typed accessors (window size, vsync, FPS limit,
  renderer backend, validation layers, asset paths) with defaults, plus
  `reload(path)`.
- **Renderer selection**: `RendererFactory::isAvailable()` and `resolve()` to
  query and degrade backend choice; `create()` falls back along
  Vulkan → OpenGL → Software when the requested backend wasn't compiled in.
- **Engine**: `switchBackend()` for runtime backend swaps, `getSettings()`,
  `resources()`.
- Full CSS named-color set in `ColorsDefinitions.h` (94 colors), plus the
  `Vertex2D` / `Mesh2D` / `FragmentInput2D` types.
- `apps/Sandbox/main.cpp`: a multi-object scene (floor, two spinning cubes, a
  sphere) demonstrating `Camera`, `ResourceManager`, `Material`, and
  per-object `drawMesh`.
- **CPU backend: multi-threaded rasterization.** `CpuFrameBufferManager`
  splits a frame's triangles into row-bands, one per hardware thread, and
  rasterizes every band concurrently (`drawTriangles` / `drawTriangles2D`);
  previously only the final present blit was parallel, so a several-thousand-
  triangle mesh rasterized entirely on the calling thread. `graphics.cpu_threads`
  (`AuraSettings::getCpuThreads()`) pins the worker count; `0` (default)
  auto-detects via `std::thread::hardware_concurrency()`.
- Backend-internal headers (`Vk*Manager`, `Gl*Manager`, `VulkanRenderer.h`,
  `OpenGLRenderer.h`, `CPURenderer.h`, ...) are now excluded from
  `cmake --install`, matching the "internals stay out of your include path"
  design principle — a consumer only ever sees `aura3d::IRenderer` and the
  backend-agnostic `Core`/`Renderer`/`Utils` API.
