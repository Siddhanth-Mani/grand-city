#version 450
// Vulkan equivalent of lit.hlsl (fragment). Compile: glslangValidator -V lit.frag
layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 0) out vec4 outColour;
layout(binding = 0) uniform Camera { mat4 viewProj; vec3 sunDir; float sunIntensity; vec3 sunColour; float pad; vec3 cameraPos; float fogDensity; } cam;
layout(binding = 1) uniform Model { mat4 model; vec3 albedo; float pad2; } mdl;
layout(binding = 2) uniform sampler2D albedoTex;
void main() {
    vec3 albedo = texture(albedoTex, inUV).rgb * mdl.albedo;
    float ndl = clamp(dot(normalize(inNormal), -cam.sunDir), 0.0, 1.0);
    vec3 colour = albedo * (0.25 + cam.sunColour * cam.sunIntensity * ndl);
    float dist = length(cam.cameraPos - inWorldPos);
    float fog = 1.0 - exp(-cam.fogDensity * cam.fogDensity * dist * dist);
    colour = mix(colour, vec3(0.55, 0.62, 0.75), clamp(fog, 0.0, 1.0));
    outColour = vec4(colour, 1.0);
}
