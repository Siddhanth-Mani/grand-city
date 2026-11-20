#version 450
// Vulkan equivalent of lit.hlsl (vertex). Compile: glslangValidator -V lit.vert
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(binding = 0) uniform Camera { mat4 viewProj; vec3 sunDir; float sunIntensity; vec3 sunColour; float pad; vec3 cameraPos; float fogDensity; } cam;
layout(binding = 1) uniform Model { mat4 model; vec3 albedo; float pad2; } mdl;
layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outWorldPos;
void main() {
    vec4 world = mdl.model * vec4(inPos, 1.0);
    gl_Position = cam.viewProj * world;
    outNormal = normalize(mat3(mdl.model) * inNormal);
    outUV = inUV;
    outWorldPos = world.xyz;
}
