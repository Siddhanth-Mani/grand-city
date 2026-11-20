// lit_pbr.hlsl — PBR (Cook-Torrance BRDF) for Grand City 6.
// Albedo / metalness / roughness per material, GGX normal distribution,
// Schlick Fresnel, Smith geometry, image-based lighting approximated from
// an environment cubemap (roughness selects the mip level), one directional
// sun with a 3x3 PCF shadow map, and up to 16 dynamic point lights.
// Compile with dxc: dxc -T vs_6_0 -E VSMain / -T ps_6_0 -E PSMain.
#define MAX_LIGHTS 16
struct VSIn {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 tangent : TANGENT;
};
struct VSOut {
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 shadowPos : TEXCOORD3;
};
cbuffer CameraCB : register(b0) {
    float4x4 g_viewProj;
    float4x4 g_lightViewProj;
    float3 g_camPos;
    float g_time;
};
cbuffer ModelCB : register(b1) {
    float4x4 g_model;
    float3 g_albedo;
    float g_metalness;
    float g_roughness;
    float g_normalStrength;
    float3 g_emissive;
    float g_pad;
};
cbuffer SunCB : register(b2) {
    float3 g_sunDir;
    float g_sunIntensity;
    float3 g_sunColour;
    float g_shadowBias;
};
struct Light { float3 pos; float radius; float3 colour; float intensity; };
cbuffer LightsCB : register(b3) {
    Light g_lights[MAX_LIGHTS];
    int g_lightCount;
    float3 g_pad2;
};
TextureCube g_envMap : register(t0);
Texture2D g_shadowMap : register(t1);
Texture2D g_albedoMap : register(t2);
Texture2D g_normalMap : register(t3);
SamplerState g_sampler : register(s0);
SamplerComparisonState g_shadowSampler : register(s1);

VSOut VSMain(VSIn input) {
    VSOut o;
    float4 world = mul(float4(input.pos, 1.0), g_model);
    o.worldPos = world.xyz;
    o.pos = mul(world, g_viewProj);
    o.normal = normalize(mul(input.normal, (float3x3)g_model));
    o.uv = input.uv;
    o.shadowPos = mul(world, g_lightViewProj);
    return o;
}
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
float ShadowFactor(float4 shadowPos) {
    float3 proj = shadowPos.xyz / shadowPos.w;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) return 1.0;
    float depth = proj.z - g_shadowBias;
    float lit = 0.0;
    // 3x3 PCF: sample the shadow map around the fragment.
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            lit += g_shadowMap.SampleCmpLevelZero(g_shadowSampler,
                     proj.xy + float2(x, y) * (1.0 / 2048.0), depth);
    return lit / 9.0;
}
float4 PSMain(VSOut input) : SV_TARGET {
    float3 albedo = g_albedo * g_albedoMap.Sample(g_sampler, input.uv).rgb;
    float3 N = normalize(input.normal);
    float3 V = normalize(g_camPos - input.worldPos);
    float metallic = g_metalness;
    float roughness = clamp(g_roughness, 0.04, 1.0);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    // --- IBL approximation: environment cubemap, roughness picks the mip.
    // This is an approximation, not a full prefiltered split-sum IBL.
    float3 R = reflect(-V, N);
    float mip = roughness * 5.0;
    float3 envColour = g_envMap.SampleLevel(g_sampler, R, mip).rgb;
    float3 ambient = envColour * albedo * (1.0 - metallic) * 0.35;
    float3 Lo = float3(0, 0, 0);
    // --- sun (directional) with shadows ---
    {
        float3 L = normalize(-g_sunDir);
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float shadow = ShadowFactor(input.shadowPos);
        float3 radiance = g_sunColour * g_sunIntensity * shadow;
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float3 num = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        float3 spec = num / denom;
        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);
        Lo += (kD * albedo / 3.14159265 + spec) * radiance * NdotL;
    }
    // --- point lights (streetlamps, headlights, police bars) ---
    for (int i = 0; i < g_lightCount && i < MAX_LIGHTS; ++i) {
        float3 toLight = g_lights[i].pos - input.worldPos;
        float dist = length(toLight);
        if (dist > g_lights[i].radius) continue;
        float3 L = toLight / dist;
        float att = pow(1.0 - dist / g_lights[i].radius, 2.0);
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 spec = NDF * G * F / (4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001);
        Lo += (albedo / 3.14159265 + spec) * g_lights[i].colour *
              g_lights[i].intensity * att * NdotL;
    }
    float3 colour = ambient + Lo + g_emissive;
    return float4(colour, 1.0);
}
