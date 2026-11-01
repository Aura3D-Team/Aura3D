#version 450

// benchmark_runtime's pipeline checks: a mesh drawn through constants alone. Compiled for Vulkan (glslc defines
// VULKAN) and read as GLSL 330 for OpenGL, its first line swapped.
#ifdef VULKAN
#define VARYING(n) layout(location = n)
#define CONSTANTS layout(push_constant)
#else
#define VARYING(n)
#define CONSTANTS layout(std140)
#endif

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec3 aNormal;

CONSTANTS uniform Constants { mat4 viewProj; vec4 tint; };

VARYING(0) out vec2 vUv;

void main()
{
    vUv = aUv;
    gl_Position = viewProj * vec4(aPosition, 1.0);
}
