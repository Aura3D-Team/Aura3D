#version 450

// vk_shader3d.vert for drawMeshInstanced(): the model and normal matrices arrive per instance
// through vertex binding 1 instead of the push constants, which still carry the texture index.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in mat4 inModel;      // locations 4-7
layout(location = 8) in vec4 inNormalCol0; // the normal matrix's three columns
layout(location = 9) in vec4 inNormalCol1;
layout(location = 10) in vec4 inNormalCol2;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) flat out uint fragTextureIndex;
layout(location = 5) flat out vec3 fragCameraPos;
layout(location = 6) flat out uint fragMaterialIndex;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
} pc;

void main()
{
    vec4 worldPos = inModel * vec4(inPosition, 1.0);

    gl_Position = ubo.proj * ubo.view * worldPos;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragNormal = mat3(inNormalCol0.xyz, inNormalCol1.xyz, inNormalCol2.xyz) * inNormal;
    fragPos = worldPos.xyz;
    fragTextureIndex = floatBitsToUint(pc.normalMatrix[3].x);
    // normalMatrix[3].y carries the material table index the same way (see VulkanRenderer::bindDrawState()).
    fragMaterialIndex = floatBitsToUint(pc.normalMatrix[3].y);
    // The view matrix is rigid, so its inverse rotation undoes the translation.
    fragCameraPos = -transpose(mat3(ubo.view)) * ubo.view[3].xyz;
}
