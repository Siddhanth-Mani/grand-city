#version 450
// PBR vertex shader (Vulkan / SPIR-V). Matches lit_pbr.hlsl inputs.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outShadowPos;
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
    mat4 lightViewProj;
    vec3 camPos;
    float time;
} ubo;
layout(push_constant) uniform ModelPC { mat4 model; } pc;
void main() {
    vec4 world = pc.model * vec4(inPos, 1.0);
    outWorldPos = world.xyz;
    outNormal = normalize(mat3(pc.model) * inNormal);
    outUV = inUV;
    outShadowPos = ubo.lightViewProj * world;
    gl_Position = ubo.viewProj * world;
}
