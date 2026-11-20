// lit.hlsl — main lit shader (Direct3D 12, HLSL Shader Model 6.0)
// Textured, directional sun + ambient, distance fog. Compile with dxc:
//   dxc -T vs_6_0 -E VSMain lit.hlsl -Fo lit_vs.cso
//   dxc -T ps_6_0 -E PSMain lit.hlsl -Fo lit_ps.cso
cbuffer CameraCB : register(b0) {
    float4x4 viewProj;
    float3 sunDir;
    float sunIntensity;
    float3 sunColour;
    float _pad;
    float3 cameraPos;
    float fogDensity;
};
cbuffer ModelCB : register(b1) {
    float4x4 model;
    float3 albedo;
    float _pad2;
};
Texture2D albedoTex : register(t0);
SamplerState samplerLinear : register(s0);

struct VSIn { float3 pos : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut {
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};
VSOut VSMain(VSIn input) {
    VSOut o;
    float4 world = mul(model, float4(input.pos, 1.0f));
    o.pos = mul(viewProj, world);
    o.normal = normalize(mul((float3x3)model, input.normal));
    o.uv = input.uv;
    o.worldPos = world.xyz;
    return o;
}
float4 PSMain(VSOut input) : SV_Target {
    float3 albedo = albedoTex.Sample(samplerLinear, input.uv).rgb * albedo;
    float ndl = saturate(dot(input.normal, -sunDir));
    float3 colour = albedo * (0.25f + sunColour * sunIntensity * ndl); // ambient + sun
    float dist = length(cameraPos - input.worldPos);
    float fog = 1.0f - exp(-fogDensity * fogDensity * dist * dist);
    colour = lerp(colour, float3(0.55f, 0.62f, 0.75f), saturate(fog)); // sky-coloured fog
    return float4(colour, 1.0f);
}
