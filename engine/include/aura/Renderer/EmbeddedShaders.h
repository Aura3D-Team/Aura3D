#ifndef EMBEDDED_SHADERS_H
#define EMBEDDED_SHADERS_H

#pragma once

namespace aura3d {

inline const char* GL_VERTEX_2D = R"(
#version 330 core
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
out vec2 fragTexCoord;
out vec4 fragColor;
layout(std140) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;
void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
)";

inline const char* GL_FRAGMENT_2D = R"(
#version 330 core
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 outColor;
uniform sampler2D textureSampler;
void main() {
    vec4 texColor = texture(textureSampler, fragTexCoord);
    outColor = texColor * fragColor;
}
)";

inline const char* GL_VERTEX_3D = R"(
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
void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragNormal = inNormal;
    fragPos = vec3(ubo.model * vec4(inPosition, 1.0));
}
)";

inline const char* GL_FRAGMENT_3D = R"(
#version 330 core
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPos;
out vec4 outColor;
uniform sampler2D textureSampler;
void main() {
    vec4 texColor = texture(textureSampler, fragTexCoord);
    outColor = texColor * fragColor;
}
)";

} // namespace aura3d

#endif // EMBEDDED_SHADERS_H