# Changelog

All notable changes to Aura3D are documented in this file.

## [0.4.0]

### Added

- `UIView` supplies a dark `DefaultWindowDecoration` for client-decorated toplevel windows: a centred title, native minimize, maximize/restore and close, title dragging, double-click maximize, and a thin same-colour border whose edges and corners resize with matching cursors. Replace it with an `IWindowDecoration` widget or disable it with `nullptr`.
- Decorations behave the same on every WMA backend: `IWindowDecoration::windowHit()` becomes the window's native hit test, so the window manager moves, resizes and maximizes. The bar is hidden where a backend has no native move. An open menu takes the first press on the frame, so pressing the bar closes it; a modal dialog leaves the bar and border working.
- `UIRoot::setDecoration()` frames application content independently of it; content fills the decoration's `contentRect()`. Decoration controls use normal AuraUI styling, input, focus and accessibility.
- `window.decorations` (`"server"` / `"client"`) and `AuraConfig::Window::decorations` request client-side decorations without a custom window factory.
- `OverlayLayer::hasLightDismissible()`: whether a press outside would close something.
- `gfx::Canvas`: lines, rectangles and triangles drawn by one `drawBatch(canvas)` call. World lines take `IRenderer::canvasView()` and keep their pixel width at any depth, depth-tested. A batch built once can be drawn every frame.

### Changed

- Vulkan reuses the allocation-free parallel executor for recording, keeps chunk state local, and binds scene descriptors together. CPU triangle binning skips untouched row bands and screen batches skip the world transform.
- The software rasterizer walks each row's covered span instead of the triangle's bounding box, so lines and thin triangles cost the pixels they cover.
- Vulkan keeps up to 64 cameras per frame in a dynamic-offset uniform ring.
- Vulkan batch buffers live in system memory: the CPU copy no longer crosses PCIe as it is written. 25,000 moving canvas shapes: 900 to 575 µs per frame on an RTX 4060 Ti.
- The software rasterizer resolves flat untextured triangles once per triangle, validates at queue time, drops its binning pass, and converts large screen batches across its workers: 2,880 to 2,050 µs for the same scene.
- `benchmark_runtime <backend> canvas` measures that scene; `AURA_BENCH_WINDOW=sdl3|x11|wayland|glfw` runs any mode through that window backend.
- Every backend blends in linear and stores sRGB, as Vulkan did: OpenGL renders each pass into an sRGB target and encodes it to the window, the software rasterizer encodes what it stores, and Metal draws to `BGRA8Unorm_sRGB`. A colour now shows the same pixels everywhere; on OpenGL and software, mid-tones are lighter than before (a 50% blend reads 188, not 128). Colours are linear; convert hex values.
- OpenGL runs on every WMA backend (SDL3, X11, Wayland, GLFW): it loads through `IWindowManager::getGLProcAddress()` and presents through `swapBuffers()`.
- OpenGL stages batches and draws each run from one upload, one draw per texture or transform change; batches of 64 KiB or more upload directly, through a mapped buffer where the platform has one. 400 small textured batches: 463 to 320 µs per frame; 25,000 canvas shapes: 794 to 650 µs.
- OpenGL keeps only the camera in a uniform block and sends the model and its CPU-computed normal matrix per draw, as Vulkan and Metal do, instead of re-uploading three matrices and inverting the model per vertex. It skips an unchanged camera or model. Lit output matches on every backend (`benchmark_runtime <backend> lighting`).
- The OpenGL backend is split into `GlTargetManager` (sRGB target and resolve), `GlBatchManager` (staging and batch draws) and `GlShaderManager::createProgram()`; its resource tables are vectors indexed by handle instead of hash maps.
- **Breaking:** `drawBatch2D()` and `gfx::Vertex2D` became `drawBatch()` and `gfx::BatchVertex`, with a `gfx::BatchSpace`: `Screen` (default; `z` is depth, 0 on the near plane) or `World`. Each backend draws every batch through one pipeline, in submission order with meshes.
- **Breaking:** `TextOverlay::drawText()` and `drawFPS()` became `addText()` and `addFPS()`, which queue; `draw()` submits the frame's text as one batch.
- **Breaking:** `FontAtlas::takeUpload(revision)` replaces `takeDirtyUpload()` and returns R8 coverage; each texture of a shared sheet keeps its own revision.
- **Breaking:** removed the unused `gfx::Mesh2D` and `RENDERER_MODE_LIST`, and `CpuFrameBufferManager`'s `drawTriangle()`, `drawTriangle2D()`, `submitTriangles()` and `submitTriangles2D()`; `queueTriangle()` with a `RasterMode` replaces them.
- Vulkan's scene and batch pipelines share one bindless texture table, and consecutive batches bind the frame's batch buffers once.
- Vulkan builds its command-recording worker pool on the first `drawMeshes()` large enough to use it (512 draws), instead of one thread per core at startup. A default Sandbox runs 8 threads instead of 32.
- Built-in shaders are embedded at build time from `resources/shaders`, so a shader edit takes effect on the next build. The committed `EmbeddedShaders.h`, `EmbeddedSpirv.h` and `EmbeddedMetalLib.h` and the `scripts/gen_embedded_*.sh` generators are gone. Vulkan builds require `glslc` or `glslangValidator`; `-DAURA_METAL_PRECOMPILE=ON` embeds a `.metallib` where Xcode's Metal toolchain is installed.
- Font atlases upload single-channel coverage on Vulkan, OpenGL/WebGL, Metal and CPU. A 1024×1024 texture uses 1 MiB instead of 4 MiB; cached font sizes share packed sheets and draw batches.
- Requires WMA 0.5 for native window controls and decoration negotiation, and ink 0.6.
- Rounded embedded bitmap font with proportional spacing and exact device-size rasterization. Advances are whole device pixels and glyph quads align to them, keeping text sharp and evenly spaced without font downloads or glyph antialiasing.
- The bitmap face scales row by row instead of by nearest sampling: shrinking drops padding and redundant rows first, and each glyph keeps its own most telling rows within aligned bands, so cap lines, crossbars and i dots survive at 11-13 px; growing repeats redundant rows, so strokes stay one pixel.
- The software renderer's `drawText()` draws the new face proportionally. `fontSize` keeps its old meaning, 8 px of line height per step, so existing layouts keep their size.
- Title-bar controls are full-height, square, flush-right caption buttons with 12 px glyphs and neutral, blue and red hover states.
- The default decoration's edges resize from 5 px in; the painted border stays 1.5 px.

### Fixed

- CPU meshes use the shared normal-matrix calculation, preserving lighting under singular model transforms.
- `VK_RESULT_CHECK` evaluated a failing Vulkan call twice.
- Vulkan threw and caught an exception several times a frame reading an absent `renderer.max_frames_in_flight`; the per-frame path no longer reads settings. The software renderer no longer allocates per present.
- Vulkan meshes used the previous frame's camera when `setTransform()` followed `beginRenderPass()`, so they lagged world-space batches while the camera moved.
- Vulkan meshes now use the camera current when they are drawn. Every mesh of a frame took the last `setTransform()`'s view and projection, while world-space batches took the one current at their call.
- World-space batches drawn after `drawMeshes()` were transformed by the last item's model matrix on every backend; `drawMeshes()` now leaves the caller's transform in place.
- Sandbox drew its FPS counter under the client-side title bar, and below where it belongs with a server-side one.
- OpenGL scene draws after a batch get their program, buffers and texture back, and an invalid texture samples white as on the other backends. Batches switch GL and Metal state once per run, not per call.
- Vulkan batch-buffer growth keeps live allocations on failure. Software rasterization shares coverage rules across meshes and batches and preserves destination alpha.
- Shared font caches synchronize correctly with multiple UI renderers and retain pending glyphs when texture creation fails. Commands that disappear after pixel snapping no longer split batches.
- Rounded widgets showed dark lines where a corner met the body: corner masks were sampled half a texel off and blended with the atlas padding. Fills, borders and clips now snap to device pixels and corner masks are rasterized at device size.
- Icons are rasterized at device size and snapped, so they stay sharp at fractional and 2x UI scales.
- Vulkan's per-frame camera ring holds 1024 cameras instead of 64, and a full ring ignores further camera changes instead of overwriting a slot that recorded draws still read.
- OpenGL presented every frame twice under `IRenderer::run()`, halving a vsynced frame rate.
- OpenGL sized its viewport from the logical window size, so a HiDPI window drew into a quarter of itself.
- OpenGL pixel reads after `endRenderPass()` could return black: they read the window's framebuffer, undefined while the window is being mapped. They read the pass target now.

## [0.3.0]

### Added

- `UIView::update()` / `needsDraw()` / `draw()`: the two halves of `render()`,
  for a loop that skips presenting an unchanged frame. `render()` is unchanged
  and is exactly `update()` then `draw()`.
- `IRenderer::frameBegun()`: whether the last `beginFrame()` acquired a target.
  Vulkan and Metal skip the frame when they rebuild the swapchain or have no
  surface; a loop that draws only on change must know, or what it drew into
  that frame is lost until the next change.
- `IRenderer::needsFrame()`: the backend needs a frame although nothing changed
  -- a pending or just-finished swapchain rebuild on Vulkan, a dropped present
  on the CPU backend. `UIView::needsDraw()` includes it; before, a failed
  present or a lost surface under an idle tree left the window stale.

### Fixed

- Flush Vulkan mapped writes at allocation-relative offsets, reject overflowing
  texture regions across CPU/OpenGL/Vulkan, and bound mapped buffer updates.
- Join all Vulkan recording tasks on failure and preserve serial/threaded scene
  call order while compositing overlays last.
- Clip CPU triangles against the homogeneous frustum before perspective division.
- Resolve soft-wrap caret ambiguity with `CaretAffinity`; keep selection endpoints
  on their own lines.
- `Selectable::detail()`, the missing counterpart to `setDetail()`, so a list
  row's trailing text can be read back as its main text already could.
- `EmbeddedSpirv.h` and `EmbeddedMetalLib.h` match their generators again, which
  now emit `// clang-format off`. Regenerating no longer fails the format check.
- Look up ink before wma, so Aura3D's own lookup selects ink rather than wma's.
- Windows: find Vulkan Memory Allocator explicitly (vcpkg and the Vulkan SDK
  install it under `include/vma`) and install the vcpkg port in CI.
- Android script and Gradle build search the per-ABI prefix before the shared
  one, so the host SDL3 is no longer linked into an arm64 binary.
- `vendor/KHR/khrplatform.h` was a partial copy missing the real header's
  `_WIN64` branch: `GLintptr`/`GLsizeiptr` silently truncated to 32 bits on
  Win64 (LLP64), where `long` and pointers differ in width.
- Windows release: `MSYS_NO_PATHCONV=1` on the `lib.exe /NOLOGO` merge step.
  Git Bash was rewriting the bare `/NOLOGO` into a path under its own install
  dir, and `lib.exe` then read that path as an input file and failed.
- Order the audio retire handshake with seq_cst fences. Under acquire/release
  alone the C++ model let a clip be freed while a block that had not yet seen
  its stop still read it.
- Check changed C++ lines with clang-format and clang-tidy 21 in Linux Debug CI; export its compilation database and document local checks.

### Changed

- Audio callbacks consume bounded, preallocated voice/listener mailboxes without
  locks or heap operations. Control updates coalesce; `update()` reclaims unloaded
  clips after callback acknowledgement.
- Batch Vulkan texture uploads into three fenced staging slots; grow overlay
  buffers geometrically and retire old buffers after frame completion.
- Remove deliberate 100 ms Vulkan/OpenGL resize sleeps.
- Cache transformed/lit indexed CPU vertices and append directly to raster queues.
- Use Ink 0.5.0 `ArenaResource` for OBJ deduplication and `ParallelProcessor` for
  synchronous jobs; use inline storage for small, reentrant UI snapshots.

### Portability

- Keep Vulkan surface backend SDK includes in the implementation and guard them
  by the enabled window backend. Android Vulkan builds no longer require unused
  host GLAD or GLFW headers.

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
