#version 450

// Far more bindings than a fixed cap would have allowed: ten textures and six uniform blocks, each read, so
// every one of them has to arrive. Red is the textures' mean, green the blocks' sum.
#ifdef VULKAN
#define VARYING(n) layout(location = n)
#define TEXTURE(n) layout(set = 0, binding = n)
#define BUFFER(n) layout(std140, set = 1, binding = n)
#else
#define VARYING(n)
#define TEXTURE(n)
#define BUFFER(n) layout(std140)
#endif

VARYING(0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

TEXTURE(0) uniform sampler2D tex0;
TEXTURE(1) uniform sampler2D tex1;
TEXTURE(2) uniform sampler2D tex2;
TEXTURE(3) uniform sampler2D tex3;
TEXTURE(4) uniform sampler2D tex4;
TEXTURE(5) uniform sampler2D tex5;
TEXTURE(6) uniform sampler2D tex6;
TEXTURE(7) uniform sampler2D tex7;
TEXTURE(8) uniform sampler2D tex8;
TEXTURE(9) uniform sampler2D tex9;
BUFFER(0) uniform Buffer0 { vec4 b0; };
BUFFER(1) uniform Buffer1 { vec4 b1; };
BUFFER(2) uniform Buffer2 { vec4 b2; };
BUFFER(3) uniform Buffer3 { vec4 b3; };
BUFFER(4) uniform Buffer4 { vec4 b4; };
BUFFER(5) uniform Buffer5 { vec4 b5; };

void main()
{
    float t = texture(tex0, vUv).r + texture(tex1, vUv).r + texture(tex2, vUv).r + texture(tex3, vUv).r +
              texture(tex4, vUv).r + texture(tex5, vUv).r + texture(tex6, vUv).r + texture(tex7, vUv).r +
              texture(tex8, vUv).r + texture(tex9, vUv).r;
    outColor = vec4(t / 10.0, b0.x + b1.x + b2.x + b3.x + b4.x + b5.x, 0.0, 1.0);
}
