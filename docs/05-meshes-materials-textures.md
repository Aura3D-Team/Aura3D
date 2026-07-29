# 5. Meshes, Materials & Textures

## Geometry: `gfx::Vertex3D` and `gfx::Mesh3D`

```cpp
namespace aura3d::gfx {
struct Vertex3D {
    glm::vec3 pos;
    glm::vec2 texCoord;
    glm::vec4 color;
    glm::vec3 normal;
};

template <typename VertexType>
struct Mesh {
    std::vector<VertexType> vertices;
    std::vector<u32> indices;
};
using Mesh3D = Mesh<Vertex3D>;
}
```

This is plain CPU-side data — building one yourself (as
`apps/OrgLogo/main.cpp` does for its single quad) is completely normal and
requires no engine machinery. `MeshLoader` below exists for the common cases
so you don't have to.

## `MeshLoader`: files and built-in primitives

```cpp
#include "aura/Core/MeshLoader/MeshLoader.h"

gfx::Mesh3D fromFile = MeshLoader::loadOBJ("resources/models/ship.obj");
gfx::Mesh3D cube     = MeshLoader::createCube();          // unit cube, centred on origin
gfx::Mesh3D sphere   = MeshLoader::createSphere(3);       // UV sphere, radius 0.5, tessellation=3
gfx::Mesh3D plane    = MeshLoader::createPlane();         // unit quad on XZ, facing +Y
```

`loadOBJ` parses Wavefront OBJ (via `tinyobjloader`), de-duplicates shared
vertices into indexed geometry, and generates flat per-face normals for
meshes that don't have any — lighting still works even on a minimal export.

**It never throws and never fails visibly**: a missing or corrupt file logs a
warning and returns `createCube()` instead. This matters for iteration —
your scene keeps rendering (with an obviously-wrong placeholder shape)
instead of crashing when an asset path is wrong.

## Uploading geometry: `createMesh` / `drawMesh`

```cpp
const MeshHandle handle = r->createMesh(MeshLoader::createCube());
// ... later, once per frame per object:
r->setTransform(camera.buildUBO(modelMatrix));
r->bindMaterial(materialHandle);
r->drawMesh(handle);
```

`createMesh` uploads a CPU mesh to a GPU-resident vertex+index buffer pair
and returns a `MeshHandle`; an empty mesh yields `INVALID_HANDLE`.
`drawMesh` is the primary 3D draw path — it replaces the lower-level
bind-vertex-buffer / bind-index-buffer / `drawIndexed` triple, which remains
available on `IRenderer` for advanced use (instancing, manual state control).

## Textures

Every texture entry point on `IRenderer` funnels through one primitive:

```cpp
virtual TextureHandle createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height) = 0;
```

Built on top of it:

```cpp
r->createTextureFromFile(path);          // decode PNG/JPEG/TGA/BMP/PSD/GIF via stb_image
r->createCheckerboardTexture(64);        // magenta/black "missing texture" pattern, no file needed
r->createSolidColorTexture(r, g, b, a);  // 1x1 flat color
r->createDynamicTexture(w, h);           // empty, writable — see below
r->updateTextureRegion(handle, x, y, w, h, rgbaPixels); // patch a sub-rectangle in place
```

`createTextureFromFile`, like `loadOBJ`, never fails visibly: a missing or
corrupt image logs a warning and substitutes the checkerboard pattern.

`createDynamicTexture` + `updateTextureRegion` exist for content that changes
after creation — the built-in `FontAtlas` (see
[07-2d-rendering-and-text.md](07-2d-rendering-and-text.md)) is the reference
user. Allocate once, patch sub-rectangles as needed: on Vulkan, every texture
permanently consumes a descriptor-pool slot, so recreating one per update
would exhaust the pool within minutes.

### `ImageLoader` directly

If you need decoded pixels without going through the renderer (e.g. to
inspect or post-process before upload):

```cpp
#include "aura/Core/ImageLoader/ImageLoader.h"

ImageData img = ImageLoader::loadRGBA("texture.png"); // .valid() == false on failure
ImageData fallback = ImageLoader::makeCheckerboard(64, 8); // size, cells-per-edge
```

`ImageData::pixels` is tightly packed RGBA8, `width * height * 4` bytes.

## Materials

```cpp
struct Material {
    TextureHandle albedo = INVALID_HANDLE;
    glm::vec4 tint = {1, 1, 1, 1};
    float roughness = 0.5f;
    float metallic = 0.0f;
};
```

```cpp
Material mat;
mat.albedo = crateTexture;
mat.roughness = 0.8f;
const MaterialHandle handle = r->createMaterial(mat);
// ...
r->bindMaterial(handle);
r->drawMesh(mesh);
```

The built-in shaders currently consume `albedo` together with the
directional light (see [06-lighting.md](06-lighting.md)); `tint`,
`roughness`, and `metallic` are carried through and readable via
`r->getCurrentMaterial()`, but not yet sampled by the default GLSL/SPIR-V —
they're part of the API so the shading model can grow later without another
break. If you write your own shaders, they're already there to read.

`INVALID_HANDLE` for `albedo` means "leave whatever texture is already
bound" rather than "no texture" — bind a real texture (even the
checkerboard) before the first draw that uses a material.

## `ResourceManager`: caching by path

Loading the same file twice from disk (and re-uploading it to the GPU) is
wasted work. `Engine` owns one `ResourceManager` for its current renderer,
reachable via `engine.resources()`:

```cpp
ResourceManager* resources = engine.resources();

const TextureHandle crate = resources->loadTexture(settings->getTexturesPath() + "crate.png");
const MeshHandle ship     = resources->loadMesh(settings->getModelsPath() + "ship.obj");

resources->cachedTextureCount();
resources->cachedMeshCount();
```

Repeated calls with the same path return the same handle instantly.
`unloadAll()` drops the cache (not the GPU resources themselves, which
belong to and are released by the renderer) — call it after
`Engine::switchBackend()`, since handles minted by the old renderer mean
nothing to the new one. `Engine::switchBackend` calls `resources->setRenderer(...)`
for you, which does this automatically.

Not thread-safe — use it from the thread that owns the renderer, like
everything else on `IRenderer`.

## Handles

```cpp
using TextureHandle = u32; // same for MeshHandle, MaterialHandle, VertexBufferHandle, IndexBufferHandle
constexpr u32 INVALID_HANDLE = UINT32_MAX;
bool isValidHandle(u32 handle) noexcept;
```

Handles are 1-based indices into a per-renderer pool (`index + 1`), so `0`
is never a valid handle and never collides with the `INVALID_HANDLE`
sentinel. They are meaningful only to the renderer that issued them — see
the backend-switching caveat in
[03-engine-and-renderer.md](03-engine-and-renderer.md#switching-backends-at-runtime).

Next: **[06-lighting.md](06-lighting.md)**.
