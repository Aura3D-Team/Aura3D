#version 330 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inNormal;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;

layout(std140) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main()
{
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);

    gl_Position = ubo.proj * ubo.view * worldPos;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
    fragPos = worldPos.xyz;
}
