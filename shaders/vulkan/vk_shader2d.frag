#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D textureSampler;

void main()
{
    vec4 texColor = texture(textureSampler, fragTexCoord);
    outColor = texColor * fragColor; // Multiply texture color by vertex color (with alpha)
}
