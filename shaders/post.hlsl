// post.hlsl — post-processing chain for Grand City 6.
// Fullscreen triangle, then: bright-pass -> blur (H+V) -> bloom composite
// -> ACES tone mapping -> vignette -> FXAA-lite. Each pass is a separate
// entry point; the backend binds the previous pass as input. Toggles come
// from settings.ini [post]; see docs/SHADERS.md.
Texture2D g_input : register(t0);
SamplerState g_sampler : register(s0);
cbuffer PostCB : register(b0) {
    float g_exposure;
    float g_bloomThreshold;
    float g_bloomStrength;
    float2 g_texelSize; // 1/width, 1/height
};
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = p;
    return o;
}
// Bright-pass: keep only pixels brighter than the threshold (bloom source).
float4 BrightPass(VSOut input) : SV_TARGET {
    float3 c = g_input.Sample(g_sampler, input.uv).rgb;
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    return float4(c * step(g_bloomThreshold, lum), 1.0);
}
// 9-tap Gaussian blur, horizontal when g_texelSize.y == 0, vertical otherwise.
float4 Blur(VSOut input) : SV_TARGET {
    float3 c = float3(0, 0, 0);
    float w[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
    c += g_input.Sample(g_sampler, input.uv).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += g_input.Sample(g_sampler, input.uv + g_texelSize * i).rgb * w[i];
        c += g_input.Sample(g_sampler, input.uv - g_texelSize * i).rgb * w[i];
    }
    return float4(c, 1.0);
}
// ACES fitted tone mapping (Narkowicz), exposure applied first.
float3 ACES(float3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
float4 Composite(VSOut input) : SV_TARGET {
    float3 c = g_input.Sample(g_sampler, input.uv).rgb * g_exposure;
    c = ACES(c);
    // Vignette: darken the corners.
    float2 d = input.uv - 0.5;
    c *= 1.0 - 0.35 * dot(d, d) * 2.0;
    return float4(c, 1.0);
}
// FXAA-lite: single-pass luma edge anti-aliasing (a small neighbourhood
// blend, not the full FXAA algorithm — see docs/SHADERS.md).
float4 FXAA(VSOut input) : SV_TARGET {
    float2 texel = g_texelSize;
    float3 c = g_input.Sample(g_sampler, input.uv).rgb;
    float luma = dot(c, float3(0.299, 0.587, 0.114));
    float lumaN = dot(g_input.Sample(g_sampler, input.uv + float2(0, -texel.y)).rgb, float3(0.299, 0.587, 0.114));
    float lumaS = dot(g_input.Sample(g_sampler, input.uv + float2(0, texel.y)).rgb, float3(0.299, 0.587, 0.114));
    float lumaW = dot(g_input.Sample(g_sampler, input.uv + float2(-texel.x, 0)).rgb, float3(0.299, 0.587, 0.114));
    float lumaE = dot(g_input.Sample(g_sampler, input.uv + float2(texel.x, 0)).rgb, float3(0.299, 0.587, 0.114));
    float range = max(max(lumaN, lumaS), max(lumaW, lumaE)) - min(min(lumaN, lumaS), min(lumaW, lumaE));
    if (range < 0.08) return float4(c, 1.0); // not an edge: leave alone
    float3 avg = (g_input.Sample(g_sampler, input.uv + float2(-texel.x, -texel.y)).rgb +
                  g_input.Sample(g_sampler, input.uv + float2(texel.x, texel.y)).rgb +
                  g_input.Sample(g_sampler, input.uv + float2(-texel.x, texel.y)).rgb +
                  g_input.Sample(g_sampler, input.uv + float2(texel.x, -texel.y)).rgb) * 0.25;
    return float4(lerp(c, avg, 0.5), 1.0);
}
