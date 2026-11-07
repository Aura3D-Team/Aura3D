#version 330 core

// gl_shader3d.vert for drawMeshInstanced(): the model and normal matrices arrive per instance.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in mat4 inModel;      // locations 4-7
layout(location = 8) in vec4 inNormalCol0; // the normal matrix's three columns
layout(location = 9) in vec4 inNormalCol1;
layout(location = 10) in vec4 inNormalCol2;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;
flat out vec3 fragCameraPos;

layout(std140) uniform Camera {
    mat4 view;
    mat4 proj;
} camera;

void main()
{
    vec4 worldPos = inModel * vec4(inPosition, 1.0);

    gl_Position = camera.proj * camera.view * worldPos;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragNormal = mat3(inNormalCol0.xyz, inNormalCol1.xyz, inNormalCol2.xyz) * inNormal;
    fragPos = worldPos.xyz;
    // The view matrix is rigid, so its inverse rotation undoes the translation.
    fragCameraPos = -transpose(mat3(camera.view)) * camera.view[3].xyz;
}
