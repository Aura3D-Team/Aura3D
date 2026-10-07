#version 330 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inNormal;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;

// Per frame; the per-draw model and its normal matrix are plain uniforms, as on Vulkan and Metal.
layout(std140) uniform Camera {
    mat4 view;
    mat4 proj;
} camera;

uniform mat4 uModel;
uniform mat3 uNormal;

void main()
{
    vec4 worldPos = uModel * vec4(inPosition, 1.0);

    gl_Position = camera.proj * camera.view * worldPos;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragNormal = uNormal * inNormal;
    fragPos = worldPos.xyz;
}
