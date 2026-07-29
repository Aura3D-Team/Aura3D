# Changelog

All notable changes to Aura3D are documented in this file.

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
