#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) flat out uint fragTextureIndex;

layout(push_constant) uniform Batch {
    mat4 transform;
    uint textureIndex;
} batch;

void main()
{
    gl_Position = batch.transform * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragTextureIndex = batch.textureIndex;
}
