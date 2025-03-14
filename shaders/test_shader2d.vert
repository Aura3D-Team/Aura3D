#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

// Change from set = 0 to set = 1 to match your binding code
layout(set = 0, binding = 0) uniform UBO {
    mat4 transform;
} ubo;

void main() {
    gl_Position = ubo.transform * vec4(inPosition, 0.0, 1.0);
    gl_Position.z = 0.0;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
