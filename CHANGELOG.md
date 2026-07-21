# Changelog

All notable changes to Aura3D are documented in this file.

## [1.0.0]

First release: a working build across all three backends (Vulkan, OpenGL, CPU
software rasterizer) with file-based assets, camera, lighting, and materials
for multi-object 3D scenes.

### Added

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
