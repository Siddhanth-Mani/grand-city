// unlit.hlsl — flat colour shader for HUD/debug geometry (Direct3D 12, SM 6.0)
cbuffer ModelCB : register(b0) { float4x4 viewProj; float4x4 model; float3 colour; float _pad; };
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(float3 pos : POSITION) {
    VSOut o;
    o.pos = mul(viewProj, mul(model, float4(pos, 1.0f)));
    return o;
}
float4 PSMain(VSOut input) : SV_Target { return float4(colour, 1.0f); }
