#version 450

layout(location = 0) in vec2 inPosition;  // Position (usually in screen space)
layout(location = 1) in vec2 inTexCoord;  // Texture coordinate
layout(location = 2) in vec4 inColor;     // Vertex color (RGBA)

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 transform;  // Transformation matrix (e.g., for scaling & translation)
} ubo;

void main() {
    gl_Position = ubo.transform * vec4(inPosition, 0.0, 1.0);
    gl_Position.z = 0.0; // Ensure UI elements are at the same depth
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
