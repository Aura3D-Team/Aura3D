#version 450

layout(location = 0) in vec2 inPosition;  // Position attribute
layout(location = 1) in vec2 inTexCoord;  // Texture coordinate attribute
layout(location = 2) in vec4 inColor;     // Color with alpha

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

/*layout(set = 0, binding = 0) uniform UBO {
    mat4 transform;  // Transformation matrix
} ubo;*/  // <-- This must match your descriptor set

void main() {
    gl_Position = /*ubo.transform **/ vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
