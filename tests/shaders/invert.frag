#version 450

// A render target read back through a texture, inverted.
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

CONSTANTS uniform Constants { vec4 flip; };
TEXTURE(0) uniform sampler2D tex0;

void main()
{
    outColor = vec4(1.0 - texture(tex0, vUv).rgb, 1.0);
}
