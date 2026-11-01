#version 450

// No vertex input: one triangle covering the target, from the vertex index. The constants carry the y sign
// IRenderer::clipCorrection() asks for.
#ifdef VULKAN
#define VARYING(n) layout(location = n)
#define CONSTANTS layout(push_constant)
#define VERTEX_ID gl_VertexIndex
#else
#define VARYING(n)
#define CONSTANTS layout(std140)
#define VERTEX_ID gl_VertexID
#endif

CONSTANTS uniform Constants { vec4 flip; };

VARYING(0) out vec2 vUv;

void main()
{
    vec2 corner = vec2(float((VERTEX_ID << 1) & 2), float(VERTEX_ID & 2));
    vUv = vec2(corner.x, 1.0 - corner.y);
    gl_Position = vec4(corner.x * 2.0 - 1.0, (corner.y * 2.0 - 1.0) * flip.x, 0.0, 1.0);
}
