#version 450

/*
 * Overlay 2D vertex stage.
 *
 * Positions arrive as window pixels with (0,0) at the top-left corner. The
 * orthographic matrix pushed by the backend is what maps them into Vulkan's
 * Y-down, [0,1]-depth clip space, so this pipeline is entirely independent of
 * the 3D scene's camera.
 */

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) flat out uint fragTextureIndex;

//! Screen-space projection plus a bindless texture-array index, pushed once
//! per batch (68 of the guaranteed 128 push-constant bytes).
layout(push_constant) uniform Overlay2D {
    mat4 proj;
    uint textureIndex;
} overlay;

void main()
{
    gl_Position = overlay.proj * vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragTextureIndex = overlay.textureIndex;
}
