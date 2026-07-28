#version 330 core

/*
 * Overlay 2D vertex stage. Mirrors EmbeddedShaders.h's GL_VERTEX_2D; keep the
 * two in sync.
 *
 * Positions arrive as window pixels with (0,0) at the top-left corner. uProj
 * maps them into OpenGL's Y-up, [-1,1]-depth clip space, so this pipeline needs
 * no camera and is independent of the 3D scene.
 */

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

out vec2 fragTexCoord;
out vec4 fragColor;

//! Screen-space projection, set once per batch. A plain uniform rather than a
//! block: one matrix does not justify a UBO binding point.
uniform mat4 uProj;

void main()
{
    gl_Position = uProj * vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
