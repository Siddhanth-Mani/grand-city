// sky.hlsl — gradient sky dome + sun disc (Direct3D 12, SM 6.0)
struct VSOut { float4 pos : SV_Position; float3 dir : TEXCOORD0; };
VSOut VSMain(float3 pos : POSITION) {
    VSOut o;
    o.pos = float4(pos.xy, 1.0f, 1.0f); // depth = far plane
    o.dir = pos;
    return o;
}
cbuffer SkyCB : register(b0) { float3 sunDir; float timeOfDay; };
float4 PSMain(VSOut input) : SV_Target {
    float3 d = normalize(input.dir);
    float t = saturate(d.y * 0.5f + 0.5f);
    float3 horizon = float3(0.98f, 0.55f, 0.25f); // sunset orange
    float3 zenith = float3(0.08f, 0.12f, 0.30f);  // deep blue
    float3 colour = lerp(horizon, zenith, pow(t, 0.7f));
    float sunAmount = saturate(dot(d, normalize(sunDir)));
    colour += float3(1.0f, 0.8f, 0.5f) * pow(sunAmount, 350.0f) * 2.0f; // sun disc
    colour += float3(1.0f, 0.6f, 0.3f) * pow(sunAmount, 8.0f) * 0.35f;  // haze
    return float4(colour, 1.0f);
}
