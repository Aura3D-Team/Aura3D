#version 450

// One texture, one uniform buffer and the constants, each read into a channel of its own: red and green are a
// gradient of the uv (scaled by the constants), blue the texture times and plus the buffer's entries.
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

VARYING(0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

CONSTANTS uniform Constants { mat4 viewProj; vec4 tint; };
TEXTURE(0) uniform sampler2D tex0;
BUFFER(0) uniform Buffer0 { vec4 palette[4]; };

void main()
{
    float blue = texture(tex0, vUv).b * palette[1].z + palette[2].z;
    outColor = vec4(vUv * tint.xy, blue, 1.0);
}
