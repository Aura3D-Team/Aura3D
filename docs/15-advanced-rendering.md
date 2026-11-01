# 15. Advanced Rendering

Everything past one lit pass to the window. Each section names the
`RendererCaps` flag that says whether a backend has it
(`INK_FLAG_CHECK(r->caps(), RendererCaps::Compute)`); the software rasterizer
has render targets, instancing, glTF, skinning, picking, background loading
and C++ compute. Your own shaders, shadows and post-processing are
[chapter 16](16-custom-pipelines.md) and [chapter 17](17-shadows-and-post-processing.md).

## Textures: formats, mipmaps, samplers

```cpp
const TextureHandle albedo = r->createTextureFromFile("rock.png",
    {.format = TextureFormat::Rgba8Srgb, .mipmaps = true,
     .sampler = {.wrapU = TextureWrap::Repeat, .maxAnisotropy = 8.0f}});
const TextureHandle normals = r->createTextureFromFile("rock_n.png", {.format = TextureFormat::Rgba8Unorm});
```

`Rgba8Srgb` is for colour (decoded to linear when sampled), `Rgba8Unorm` for
data such as normal maps, `Rgba16Float` for HDR (`createTexture()` with half
floats). A full mip chain is generated from level 0.

## Render targets (`RendererCaps::RenderTargets`)

```cpp
const RenderTargetHandle mirror = r->createRenderTarget({.width = 512, .height = 512});
r->beginRenderPass(mirror);   // clears, then draws land in the target
r->drawMeshes(reflected);
r->endRenderPass();
r->beginRenderPass();         // the window
r->drawMesh(mirrorQuad, r->renderTargetTexture(mirror));
```

`resizeRenderTarget(target, w, h)` gives it new storage with the same handles,
so a window-sized target follows the window. `readPixels()` copies one back
(top row first) for tests and captures. On Vulkan, `renderer.vulkan.pipeline_cache`
keeps compiled pipelines on disk between runs.

## Instancing and culling (`RendererCaps::Instancing`)

`drawMeshes()` culls every item against the camera's frustum.
`drawMeshInstanced(mesh, material, models)` culls each model and draws the
survivors in one instanced draw. `meshBounds(mesh)` gives a mesh's box.

## Picking and world lines

```cpp
const Ray ray = camera.rayFromScreen(mousePixel, viewportSize);
if (const auto t = intersect(ray, transformed(r->meshBounds(crate), crateModel)))
    select(ray.at(*t));

gfx::Canvas debug;
const gfx::CanvasView view = r->canvasView();
debug.line(view, ray.origin, ray.at(10.0f), {1, 1, 0, 1});   // any shape: lines, triangles or append()
r->drawBatch(debug);
```

`aura/Core/Collision.h` also has spheres, planes, triangles, `overlaps()` and
`Frustum::fromMatrix()` with `visible()`.

## glTF models and skinned animation

```cpp
#include "aura/Core/Gltf/Animation.h"
#include "aura/Renderer/Model.h"

std::optional<gltf::Model> robot = gltf::load("robot.glb");   // .gltf or .glb
ModelHandles handles = uploadModel(*r, *robot);               // meshes, materials, textures

// per frame:
const gltf::Animation &walk = robot->animations[0];
gltf::animate(robot->nodes, walk, std::fmod(seconds, walk.duration));
const std::vector<glm::mat4> world = gltf::worldMatrices(*robot);
updateSkins(*r, *robot, handles, world, &jobs);               // no pass open
items.clear();
appendDrawItems(*robot, handles, world, placement, items);
r->beginRenderPass();
r->drawMeshes(items);
```

The loader brings in triangle meshes (strips and fans too), metallic-roughness
materials with their textures, the node hierarchy, skins and animations (step,
linear with slerp, cubic spline). Morph targets are not imported. A malformed
or hostile file returns `std::nullopt` with the reason logged.

Skinning runs on the CPU, split across the job system, and `updateMesh()`
streams the vertices, so every backend shows the same pose. Use `updateMesh()`
directly for any geometry that deforms. It is called between `beginFrame()`
and the pass that draws the mesh.

## Compute and indirect draws (`RendererCaps::Compute`)

```cpp
ComputeDesc desc{.buffers = 2};
desc.spirv = cullSpirv;                 // Vulkan
desc.glsl = cullGlsl;                   // OpenGL 4.3 (desktop)
desc.msl = cullMsl; desc.mslEntry = "cull";
desc.cpu = [](const ComputeInvocation &call) { /* the software backend's version */ };
const ComputeHandle cull = r->createCompute(desc);

const BufferHandle instances = r->createBuffer(maxInstances * sizeof(InstanceTransform));
const DrawIndexedIndirect reset{.indexCount = r->meshIndexCount(rock)};
const BufferHandle args = r->createBuffer(sizeof(reset), std::as_bytes(std::span(&reset, 1)));

// per frame, no pass open:
r->updateBuffer(args, 0, std::as_bytes(std::span(&reset, 1)));
r->dispatch(cull, std::array{instances, args}, {groups, 1, 1}, std::as_bytes(std::span(&constants, 1)));
r->beginRenderPass();
r->drawMeshInstancedIndirect(rock, rockMaterial, instances, args);
```

Kernels see their buffers at bindings 0.., and up to 128 bytes of constants
(push constants on Vulkan, a std140 block at binding 3 on OpenGL, the next
buffer index on Metal). `tests/shaders/grid.comp` is a complete example: it
appends instances and counts them with an atomic, so the GPU decides how many
are drawn. `readBuffer()` waits for the GPU and is for tests and tools.

## Loading in the background

```cpp
AssetLoader loader;   // two worker threads by default
loader.loadModel("level.glb", [&](std::optional<gltf::Model> model) {
    if (model) level = uploadModel(*r, *model);
});
// each frame:
loader.poll();        // runs finished callbacks here, where uploads are allowed
```

`submit()` takes any job that returns a callback. Destroying the loader
finishes what is running and drops the rest.

## Shader hot reload

In debug builds (`renderer.shader_hot_reload`), `run()` watches `paths.shaders`.
When a file there changes, it rebuilds the lit and instanced pipelines with
`reloadShaders()`. Edit a shader under
`resources/shaders`, then rebuild the app's target: the build restages only the
changed files, SPIR-V included, without relinking. A shader that fails to
compile is logged, and the running one stays.

Back to the **[index](README.md)**.
