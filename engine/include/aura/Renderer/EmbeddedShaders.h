#ifndef EMBEDDED_SHADERS_H
#define EMBEDDED_SHADERS_H

#pragma once

namespace aura3d {

/*
 * AURA_GLES is defined by CMake when targeting WebGL2 (WASM) or OpenGL ES 3.0.
 * WebGL2 == OpenGL ES 3.0: same API, same GLSL dialect (#version 300 es).
 * Desktop OpenGL uses #version 330 core (GLSL 3.30).
 */

#ifdef AURA_GLES

inline const char* GL_VERTEX_3D = R"(
#version 300 es
precision highp float;
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
    fragColor    = inColor;
    fragNormal   = inNormal;
    fragPos      = vec3(ubo.model * vec4(inPosition, 1.0));
}
)";

inline const char* GL_FRAGMENT_3D = R"(
#version 300 es
precision highp float;
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

#else /* Desktop OpenGL 3.3 */

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
    fragColor    = inColor;
    fragNormal   = inNormal;
    fragPos      = vec3(ubo.model * vec4(inPosition, 1.0));
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

#endif /* AURA_GLES */

} // namespace aura3d

#endif // EMBEDDED_SHADERS_H
