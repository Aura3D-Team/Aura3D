# 16. Your Own Shaders

`createPipeline()` takes your vertex and fragment shaders; `drawPipeline()` draws a mesh, or nothing at all, with
them. The engine decides nothing about the look: lighting, shadows and effects are yours.

```cpp
struct Constants { glm::mat4 viewProj; glm::vec4 tint; };                 // vec4 and mat4 members only

const PipelineHandle lit = r->createPipeline({
    .vertexSpirv = vertSpirv, .fragmentSpirv = fragSpirv,                 // Vulkan: glslc output
    .vertexGlsl = vertGlsl,   .fragmentGlsl = fragGlsl,                   // OpenGL: "#version 330 core"
    .textures = 1, .buffers = 1, .constantBytes = sizeof(Constants)});

// per frame
const Constants constants{camera.projectionMatrix() * camera.viewMatrix(), {1, 1, 1, 1}};
r->beginRenderPass();
r->drawPipeline({.pipeline = lit, .mesh = rock,
                 .constants = std::as_bytes(std::span(&constants, 1)),
                 .textures = std::array{albedo}, .buffers = std::array{lights}});   // lights: r->createBuffer(...)
r->endRenderPass();
```

## What a shader reads

| | Vulkan (SPIR-V) | OpenGL (GLSL 330 core, 300 es on GLES) | Metal (MSL) |
|---|---|---|---|
| constants | `layout(push_constant) uniform Constants { ... };` | `layout(std140) uniform Constants { ... };` | `constant Constants &c [[buffer(0)]]` |
| texture N | `layout(set = 0, binding = N) uniform sampler2D texN;` | `uniform sampler2D texN;` | `texture2d<float> texN [[texture(N)]]`, `sampler s [[sampler(N)]]` |
| uniform buffer N | `layout(std140, set = 1, binding = N) uniform BufferN { ... };` | `layout(std140) uniform BufferN { ... };` | `constant BufferN &b [[buffer(1 + N)]]` |
| vertex input, `VertexInput::Mesh` | location 0 position, 1 uv, 2 colour (vec4), 3 normal | the same | `[[stage_in]]` with `[[attribute(0..3)]]` |
| vertex input, `VertexInput::None` | `gl_VertexIndex` | `gl_VertexID` | `[[vertex_id]]` |

Constants and buffer blocks use only `vec4` and `mat4` members, so both layouts agree. Declare as many textures and
buffers as the shader reads: the only ceiling is the device's, which `r->pipelineLimits()` reports and `createPipeline()`
enforces (a `Constants` block of at most `constantBytes`, past which the data belongs in a buffer; a buffer shows its
first `bufferBytes`). An unset texture slot samples white.

Metal takes one MSL source per stage (`vertexMsl`, `fragmentMsl`, entry point `main0` unless `mslVertexEntry` and
`mslFragmentEntry` say otherwise), and both stages see the same slots. Those slots do not depend on how many buffers a
pipeline has, so one source serves every pipeline. `spirv-cross --msl` numbers a stage's buffers by what the stage
uses, so give it these numbers (its resource-binding API) or edit the output; `tests/shaders/*.metal` are written by
hand to them.

## One source for both

glslc defines `VULKAN`, so one file serves both backends with this prelude; for OpenGL, swap the `#version` line:

```glsl
#version 450
#ifdef VULKAN
#define VARYING(n) layout(location = n)
#define TEXTURE(n) layout(set = 0, binding = n)
#define BUFFER(n) layout(std140, set = 1, binding = n)
#define CONSTANTS layout(push_constant)
#else
#define VARYING(n)
#define TEXTURE(n)
#define BUFFER(n) layout(std140)
#define CONSTANTS layout(std140)
#endif
```

```sh
glslc lit.frag -o lit.frag.spv        # glslangValidator -V lit.frag -o lit.frag.spv
```

`tests/shaders/` holds complete pairs, built and checked by `benchmark_runtime <backend> pipeline`.

## Render targets and depth maps

`createRenderTarget()` with `TextureFormat::Depth` makes a depth map with no colour; `renderTargetTexture()` is that
depth, ready to sample. Clip space is y-up everywhere and a target's texture holds its top row first, so look a clip
position up at `vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5)`. OpenGL stores a target upside down, so multiply the
projection of any pass into a target by `r->clipCorrection(target)`; the window needs none.

```cpp
const RenderTargetHandle map = r->createRenderTarget({.width = 2048, .height = 2048, .format = TextureFormat::Depth});
const glm::mat4 lightClip = r->clipCorrection(map) * lightProj * lightView;
r->beginRenderPass(map);                  // draw depth with a pipeline of your own
// ...
r->endRenderPass();
// later: .textures = {r->renderTargetTexture(map)}, sampled by a pipeline in any pass
```

Draw a target before you sample it. A pipeline made for the window works in every target; the built-in shaders draw
into a depth map too, writing depth alone.

## Support

| | Vulkan | OpenGL | OpenGL ES, WebGL | Software | Metal |
|---|---|---|---|---|---|
| `createPipeline()`, `drawPipeline()` | yes | yes | yes, no `BufferN` | no | yes, never run |
| `TextureFormat::Depth` targets | yes | yes | yes | no | not yet |
| `createBuffer()` as a uniform buffer | yes | yes | no | no | yes, never run |

Tested on Vulkan and OpenGL; the OpenGL ES, WebGL and Metal paths have not been run.
Where `RendererCaps::CustomPipelines` is absent, `createPipeline()` logs why and returns an invalid handle. Shadows
and post-processing are not part of the engine: [chapter 17](17-shadows-and-post-processing.md).

Next: **[17-shadows-and-post-processing.md](17-shadows-and-post-processing.md)**.
