#version 450
// Fullscreen triangle for the post chain (Vulkan). Matches post.hlsl VSMain.
layout(location = 0) out vec2 outUV;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
    outUV = p;
}
