#version 450
// PBR fragment shader (Vulkan / SPIR-V): Cook-Torrance GGX + Schlick +
// Smith, IBL approximated from an environment cubemap (roughness -> mip),
// one sun with PCF shadows, up to 16 point lights. Mirrors lit_pbr.hlsl.
#define MAX_LIGHTS 16
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inShadowPos;
layout(location = 0) out vec4 outColour;
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj; mat4 lightViewProj; vec3 camPos; float time;
} ubo;
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec3 albedo; float metalness; float roughness; float normalStrength; vec3 emissive;
} mat;
layout(set = 1, binding = 1) uniform SunUBO {
    vec3 sunDir; float sunIntensity; vec3 sunColour; float shadowBias;
} sun;
struct Light { vec3 pos; float radius; vec3 colour; float intensity; };
layout(set = 1, binding = 2) uniform LightsUBO { Light lights[MAX_LIGHTS]; int lightCount; } lightData;
layout(set = 1, binding = 3) uniform samplerCube envMap;
layout(set = 1, binding = 4) uniform sampler2DShadow shadowMap;
const float PI = 3.14159265;
float distGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness; float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}
float geomGGX(float NdotV, float roughness) {
    float r = roughness + 1.0; float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float geomSmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geomGGX(max(dot(N, V), 0.0), roughness) * geomGGX(max(dot(N, L), 0.0), roughness);
}
vec3 fresnel(float cosTheta, vec3 F0) { return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0); }
float shadowFactor(vec4 sp) {
    vec3 proj = sp.xyz / sp.w;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) return 1.0;
    float depth = proj.z - sun.shadowBias;
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            lit += texture(shadowMap, vec3(proj.xy + vec2(x, y) / 2048.0, depth));
    return lit / 9.0;
}
void main() {
    vec3 N = normalize(inNormal);
    vec3 V = normalize(ubo.camPos - inWorldPos);
    float metallic = mat.metalness;
    float roughness = clamp(mat.roughness, 0.04, 1.0);
    vec3 F0 = mix(vec3(0.04), mat.albedo, metallic);
    vec3 R = reflect(-V, N);
    vec3 envColour = textureLod(envMap, R, roughness * 5.0).rgb;
    vec3 ambient = envColour * mat.albedo * (1.0 - metallic) * 0.35;
    vec3 Lo = vec3(0.0);
    vec3 L = normalize(-sun.sunDir);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float shadow = shadowFactor(inShadowPos);
    vec3 F = fresnel(max(dot(H, V), 0.0), F0);
    vec3 spec = distGGX(N, H, roughness) * geomSmith(N, V, L, roughness) * F /
                (4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    Lo += (kD * mat.albedo / PI + spec) * sun.sunColour * sun.sunIntensity * shadow * NdotL;
    for (int i = 0; i < lightData.lightCount && i < MAX_LIGHTS; ++i) {
        vec3 toLight = lightData.lights[i].pos - inWorldPos;
        float dist = length(toLight);
        if (dist > lightData.lights[i].radius) continue;
        vec3 Li = toLight / dist;
        float att = pow(1.0 - dist / lightData.lights[i].radius, 2.0);
        vec3 Hi = normalize(V + Li);
        float NdotLi = max(dot(N, Li), 0.0);
        vec3 Fi = fresnel(max(dot(Hi, V), 0.0), F0);
        vec3 speci = distGGX(N, Hi, roughness) * geomSmith(N, V, Li, roughness) * Fi /
                     (4.0 * max(dot(N, V), 0.0) * NdotLi + 0.0001);
        Lo += (mat.albedo / PI + speci) * lightData.lights[i].colour *
              lightData.lights[i].intensity * att * NdotLi;
    }
    outColour = vec4(ambient + Lo + mat.emissive, 1.0);
}
