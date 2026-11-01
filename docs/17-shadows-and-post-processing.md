# 17. Shadows and Post-Processing

The engine has neither. Both are built from what chapter 16 gives you, so they look the way your game wants.

| Effect | Built from | Reading |
|---|---|---|
| Shadow map | a `TextureFormat::Depth` target, a pipeline that writes depth, a pipeline that samples it | [LearnOpenGL: Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping) |
| Cascaded shadows | one depth target per cascade, or one atlas cut by viewport | [LearnOpenGL: CSM](https://learnopengl.com/Guest-Articles/2021/CSM), [Microsoft: Cascaded Shadow Maps](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps) |
| HDR and exposure | an `Rgba16Float` target, then a vertex-less pass that reads it | [LearnOpenGL: HDR](https://learnopengl.com/Advanced-Lighting/HDR) |
| Tonemapping | the same pass, with a curve in the fragment shader | [Narkowicz: ACES filmic curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/) |
| Bloom | a chain of smaller `Rgba16Float` targets, blurred and added back | [LearnOpenGL: Bloom](https://learnopengl.com/Advanced-Lighting/Bloom) |
| FXAA | a vertex-less pass over the tonemapped image | [NVIDIA: FXAA white paper](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf) |

Those pages write GLSL for OpenGL; port the lookups to the conventions of chapter 16.

```cpp
// a full-screen pass: no mesh, a pipeline with VertexInput::None, a target as its texture
r->beginRenderPass();                                       // the window
r->drawPipeline({.pipeline = tonemap, .vertexCount = 3,
                 .constants = std::as_bytes(std::span(&params, 1)),
                 .textures = std::array{r->renderTargetTexture(hdr)}});
r->endRenderPass();
```

`benchmark_runtime <backend> pipeline` renders a depth target and samples it, and runs a vertex-less pass over a
colour target: the two moves everything above is made of.

## Shader language and graphics API references

| | |
|---|---|
| [GLSL 4.60 specification](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.pdf) | the language |
| [GL_KHR_vulkan_glsl](https://github.com/KhronosGroup/GLSL/blob/main/extensions/khr/GL_KHR_vulkan_glsl.txt) | what changes for Vulkan: `set`, `binding`, `push_constant` |
| [glslc](https://github.com/google/shaderc/tree/main/glslc) | compiling GLSL to SPIR-V |
| [Vulkan specification](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html) | depth, clip space, render passes |
| [Vulkan tutorial](https://docs.vulkan.org/tutorial/latest/index.html) | the same, as a walkthrough |
| [Sascha Willems' Vulkan examples](https://github.com/SaschaWillems/Vulkan) | shadow mapping and post-processing, in Vulkan |

Back to the **[index](README.md)**.
