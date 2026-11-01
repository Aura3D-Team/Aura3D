#version 450

// What a shadow lookup does at its simplest: the depth a map holds, and whether it is nearer than a threshold.
#ifdef VULKAN
#define VARYING(n) layout(location = n)
#define TEXTURE(n) layout(set = 0, binding = n)
#define CONSTANTS layout(push_constant)
#else
#define VARYING(n)
#define TEXTURE(n)
#define CONSTANTS layout(std140)
#endif

VARYING(0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

CONSTANTS uniform Constants { mat4 viewProj; vec4 tint; };
TEXTURE(0) uniform sampler2D tex0;

void main()
{
    float depth = texture(tex0, vUv).r;
    outColor = vec4(depth, depth < 0.9 ? 1.0 : 0.0, 0.0, 1.0);
}
