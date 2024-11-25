#version 450

// Inputs from the vertex buffer
layout(location = 0) in vec3 aPos;        // Vertex position
layout(location = 1) in vec2 aUV;         // Texture coordinates
layout(location = 2) in vec3 aNormal;     // Vertex normal

// Outputs to the fragment shader
layout(location = 0) out vec2 vUV;        // Interpolated UV coordinates
layout(location = 1) out vec3 vNormal;    // Interpolated normal
layout(location = 2) out vec3 vPosition;  // World-space position

// Uniforms for transformation matrices
layout(binding = 0) uniform Matrices {
    mat4 uModel;       // Model matrix
    mat4 uView;        // View matrix
    mat4 uProjection;  // Projection matrix
};

void main() {
    // Transform position to clip space
    vec4 worldPosition = uModel * vec4(aPos, 1.0);
    gl_Position = uProjection * uView * worldPosition;

    // Pass data to the fragment shader
    vUV = aUV;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;  // Transform normal to world space
    vPosition = worldPosition.xyz;
}
