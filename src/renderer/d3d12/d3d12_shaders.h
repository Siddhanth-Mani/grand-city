#pragma once
// HLSL for the Direct3D 12 backend, compiled at start-up with FXC (SM 5.0).
//
// Lighting is physically based: GGX specular, Schlick Fresnel, a hemisphere
// ambient term and sky reflections, a 3x3 PCF sun shadow map, up to 16 point
// lights (streetlamps, headlights, police bars) and ACES tone mapping. The
// city needs no textures: building facades, roads, pavements, sand and sea
// are generated per pixel from the world position (see facade() / ground()).
namespace gc6 {
// Split into several literals: MSVC caps a single string literal at 16 KB.
inline const char* kShaderSource = R"HLSL(
cbuffer Frame : register(b0) {
    float4x4 viewProj;
    float4x4 shadowVP;
    float4x4 invViewProj;
    float4 camPos;      // xyz camera, w fog density
    float4 sunDir;      // xyz towards the sun (or moon at night), w intensity
    float4 sunColour;   // rgb, w night factor (0 day .. 1 night)
    float4 skyZenith;   // rgb, w time in seconds (clouds, waves)
    float4 skyHorizon;  // rgb, w exposure
    float4 misc;        // x point-light count, y shadow texel, z shadows on, w wetness
    float4 lightPos[16];// xyz position, w radius
    float4 lightCol[16];// rgb colour * intensity, w 1 = downward lamp
};
cbuffer Draw : register(b1) {
    float4x4 model;
    float4 colour;      // rgb albedo (sRGB), w emissive strength
    float4 params;      // x metalness, y roughness, z shading model, w variant
};
Texture2D fontTex : register(t0);
Texture2D<float> shadowTex : register(t1);
SamplerState samp : register(s0);
SamplerComparisonState shadowSamp : register(s1);

static const float PI = 3.14159265;

float hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}
float noise2(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash12(i), hash12(i + float2(1, 0)), u.x),
                lerp(hash12(i + float2(0, 1)), hash12(i + float2(1, 1)), u.x), u.y);
}
float fbm(float2 p) {
    float s = 0.0, a = 0.5;
    [unroll] for (int k = 0; k < 5; ++k) { s += a * noise2(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return s;
}
float band(float v, float lo, float hi) { return step(lo, v) * step(v, hi); }
float3 toLinear(float3 c) { return c * c * (c * 0.2 + 0.8); } // close to pow(c, 2.2)

// Sky radiance in direction d. detail adds the sun disc, clouds and stars
// (used for the background); reflections and fog use the smooth version.
float3 skyColour(float3 d, bool detail) {
    float night = sunColour.w;
    float up = saturate(d.y);
    float3 c = lerp(skyHorizon.rgb, skyZenith.rgb, pow(up, 0.5));
    if (d.y < 0.0) c = lerp(skyHorizon.rgb, skyHorizon.rgb * 0.45, saturate(-d.y * 3.0));
    float3 L = normalize(sunDir.xyz);
    float sd = saturate(dot(d, L));
    float3 glow = sunColour.rgb * (1.0 - night);
    c += glow * (pow(sd, 6.0) * 0.30 + pow(sd, 180.0) * 1.2);
    if (detail) {
        c += glow * smoothstep(0.99945, 0.99975, sd) * 40.0;                    // sun
        c += float3(0.9, 0.92, 1.0) * smoothstep(0.9991, 0.9994, sd) * 2.5 * night; // moon
        if (d.y > 0.0) {
            float2 uv = d.xz / (d.y + 0.10) * 1.4 + float2(skyZenith.w * 0.006, skyZenith.w * 0.002);
            float cover = 0.50 - misc.w * 0.18;
            float cl = smoothstep(cover, cover + 0.30, fbm(uv));
            float shade = fbm(uv * 1.7 + 4.3);
            float3 lit = lerp(skyHorizon.rgb * 0.8 + 0.06, sunColour.rgb * 1.15 + 0.10, 0.45 + 0.55 * shade);
            lit = lerp(lit, skyZenith.rgb * 2.2 + 0.006, night);
            c = lerp(c, lit, cl * smoothstep(0.0, 0.20, d.y) * 0.9);
            float2 sp = floor(d.xz / (d.y + 0.35) * 260.0);
            float star = step(0.9965, hash12(sp)) * hash12(sp + 7.0);
            c += night * star * (1.0 - cl) * up * 1.6;
        }
    }
    return c;
}

float shadowFactor(float3 wp, float3 N) {
    if (misc.z < 0.5) return 1.0;
    float4 sp = mul(shadowVP, float4(wp + N * 0.05, 1.0));
    float3 uvz = sp.xyz / sp.w;
    float2 uv = float2(uvz.x * 0.5 + 0.5, -uvz.y * 0.5 + 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || uvz.z > 1.0) return 1.0;
    float s = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowTex.SampleCmpLevelZero(shadowSamp, uv + float2(x, y) * misc.y, uvz.z - 0.00025);
    s /= 9.0;
    float edge = saturate(max(abs(uvz.x), abs(uvz.y)) * 8.0 - 7.0);
    return lerp(s, 1.0, edge);
}

)HLSL"
R"HLSL(// Building walls. variant: integer part = style (0 concrete offices,
// 1 glass tower, 2 brick, 3 stone), fraction = per-building seed.
void facade(float3 p, float3 N, inout float3 albedo, inout float rough, inout float f0,
            inout float3 emissive, inout float ao) {
    float night = sunColour.w;
    float seed = frac(params.w) * 97.0;
    int style = (int)params.w;
    if (N.y > 0.6) {                                  // flat roof: bitumen + gravel
        albedo = float3(0.10, 0.10, 0.105) * (0.7 + 0.6 * noise2(p.xz * 0.9 + seed));
        rough = 0.95;
        return;
    }
    if (N.y < -0.6) return;
    float u = abs(N.x) > abs(N.z) ? p.z : p.x;
    float3 wall = albedo;
    ao = lerp(0.55, 1.0, saturate(p.y / 3.0));
    if (p.y < 4.6) {                                  // ground floor: shopfronts
        float bay = 4.2;
        float cellId = floor(u / bay);
        float fx = frac(u / bay);
        float h = hash12(float2(cellId, seed));
        if (p.y > 3.55 && p.y < 4.3) {                // fascia / shop sign
            float3 signCol = lerp(float3(0.9, 0.25, 0.15), float3(0.15, 0.5, 0.9), step(0.5, h));
            signCol = lerp(signCol, float3(0.95, 0.75, 0.25), step(0.8, h));
            bool lettering = fx > 0.25 && fx < 0.75 && p.y > 3.72 && p.y < 4.12;
            albedo = lettering ? signCol : float3(0.03, 0.03, 0.035);
            rough = 0.5;
            if (lettering) emissive = signCol * (0.05 + night * 1.3);
            return;
        }
        if (fx > 0.05 && fx < 0.95 && p.y > 0.3 && p.y < 3.4) {
            albedo = float3(0.015, 0.018, 0.02); rough = 0.03; f0 = 0.07; ao = 1.0;
            float3 warm = lerp(float3(1.0, 0.85, 0.65), float3(0.85, 0.92, 1.0), step(0.6, h));
            float open = step(0.3, frac(h * 5.7));
            emissive = warm * (0.02 + night * 0.20 * open) * (0.4 + 0.8 * h) * smoothstep(0.2, 1.6, p.y);
            return;
        }
        albedo = wall * 0.5; rough = 0.75;
        return;
    }
    float floorH = style == 1 ? 3.7 : 3.4;
    float bayW = style == 1 ? 1.75 : (style == 2 ? 2.5 : 3.0);
    float2 g = float2(u / bayW, (p.y - 4.6) / floorH);
    float2 cell = floor(g), f = frac(g);
    float2 lo, hi;
    if (style == 1)      { lo = float2(0.03, 0.08); hi = float2(0.97, 0.97); }
    else if (style == 2) { lo = float2(0.27, 0.22); hi = float2(0.73, 0.80); }
    else if (style == 3) { lo = float2(0.12, 0.18); hi = float2(0.88, 0.84); }
    else                 { lo = float2(0.15, 0.26); hi = float2(0.85, 0.86); }
    if (all(f > lo) && all(f < hi)) {
        float h = hash12(cell + seed);
        albedo = style == 1 ? float3(0.02, 0.045, 0.05) : float3(0.018, 0.02, 0.024);
        rough = 0.03; f0 = style == 1 ? 0.10 : 0.07;
        float lit = step(h, 0.38) * night + step(h, 0.04) * (1.0 - night) * 0.04;
        float3 warm = lerp(float3(1.0, 0.76, 0.50), float3(0.78, 0.86, 1.0), step(0.72, frac(h * 7.3)));
        // Blinds: the top part of some lit windows is shaded.
        float blind = step(0.5, frac(h * 3.7)) * step(1.0 - (f.y - lo.y) / (hi.y - lo.y), 0.35);
        emissive = warm * lit * (0.28 + 0.32 * frac(h * 13.1)) * (1.0 - blind * 0.7);
        albedo += float3(0.010, 0.009, 0.008) * frac(h * 5.1);
        return;
    }
    // Recess shadow right around each opening, darker floor ledges.
    float2 dl = f - lo, dh = hi - f;
    float near = min(min(abs(dl.x), abs(dh.x)), min(abs(dl.y), abs(dh.y)));
    float rim = (f.x > lo.x - 0.03 && f.x < hi.x + 0.03 && f.y > lo.y - 0.04 && f.y < hi.y + 0.04) ? 1.0 : 0.0;
    float shadeEdge = 1.0 - rim * 0.45 * (1.0 - saturate(near / 0.04));
    if (style == 2) {                                  // running-bond brick
        float row = floor(p.y * 13.0);
        float2 b = float2(u * 4.2 + 0.5 * row, p.y * 13.0);
        float mortar = saturate(step(frac(b.x), 0.07) + step(frac(b.y), 0.12));
        albedo = lerp(wall * (0.8 + 0.4 * hash12(floor(b) + seed)), float3(0.33, 0.31, 0.29), mortar);
        rough = 0.9;
    } else if (style == 1) {                           // dark spandrel panels
        albedo = wall * 0.35; rough = 0.3; f0 = 0.06;
    } else {
        albedo = wall * (0.88 + 0.24 * noise2(float2(u, p.y) * 0.35 + seed));
        rough = 0.85;
    }
    albedo *= shadeEdge * (1.0 - 0.3 * step(f.y, 0.05));
    albedo *= 0.85 + 0.15 * noise2(float2(u * 0.7, p.y * 0.06) + seed);   // rain streaks
}

)HLSL"
R"HLSL(// Roads, kerbs, pavements, plazas, beach and sea.
void ground(float3 p, inout float3 N, inout float3 albedo, inout float rough, inout float f0) {
    float2 q = p.xz;
    float wet = misc.w;
    float grain = noise2(q * 2.7) * 0.5 + noise2(q * 0.35) * 0.5;
    if (N.y < 0.5) {                                   // kerb faces
        albedo = float3(0.30, 0.30, 0.29) * (0.85 + 0.3 * grain); rough = 0.8;
        return;
    }
    if (q.y < -196.0) {                                // sea
        float t = skyZenith.w;
        float2 w = float2(noise2(q * 0.21 + t * 0.35) - 0.5, noise2(q * 0.23 + 9.0 - t * 0.31) - 0.5)
                 + 0.5 * float2(noise2(q * 0.9 - t * 0.6) - 0.5, noise2(q * 0.8 + 3.0 + t * 0.5) - 0.5);
        N = normalize(float3(w.x * 0.35, 1.0, w.y * 0.35));
        albedo = float3(0.006, 0.03, 0.04); rough = 0.05; f0 = 0.025;
        return;
    }
    if (q.y < -158.0) {                                // beach, wetter towards the sea
        float wetSand = smoothstep(-186.0, -196.0, q.y);
        albedo = float3(0.52, 0.44, 0.30) * (0.85 + 0.3 * grain) * (1.0 - 0.45 * wetSand);
        rough = lerp(0.95, 0.35, wetSand);
        return;
    }
    if (abs(q.x) > 204.0 || q.y > 204.0) {             // countryside beyond the city
        albedo = float3(0.09, 0.14, 0.05) * (0.7 + 0.6 * fbm(q * 0.05));
        rough = 0.95;
        return;
    }
    float s = 40.0;
    float2 m = abs(q) % s;
    float2 d = min(m, s - m);
    bool rx = d.x < 7.0, rz = d.y < 7.0;
    if (rx || rz) {
        float3 c = float3(0.060, 0.061, 0.066) * (0.75 + 0.5 * grain);
        c *= 1.0 - 0.25 * smoothstep(0.55, 0.75, noise2(q * 0.15));            // patch repairs
        if (!(rx && rz)) {
            float along = rx ? q.y : q.x;
            float across = rx ? d.x : d.y;
            float other = rx ? d.y : d.x;
            float worn = 0.75 + 0.25 * noise2(q * 1.7);
            float yellow = band(across, 0.08, 0.20) * step(10.0, other);
            float lane = band(across, 3.42, 3.56) * step(frac(along / 9.0), 0.42) * step(11.4, other);
            float edge = band(across, 6.2, 6.35) * step(11.4, other);
            float zebra = band(other, 7.6, 10.4) * step(across, 6.2) * step(frac((rx ? q.x : q.y) / 1.1), 0.5);
            float stop = band(other, 10.8, 11.25) * step(across, 6.3);
            c = lerp(c, float3(0.55, 0.40, 0.07), yellow * worn);
            c = lerp(c, float3(0.62, 0.62, 0.60), saturate(lane + edge + zebra + stop) * worn);
        }
        albedo = c * (1.0 - 0.45 * wet);
        rough = lerp(0.85, 0.08, wet * smoothstep(0.35, 0.6, noise2(q * 0.4) + wet * 0.3));
        return;
    }
    float e = min(d.x, d.y);
    if (e < 7.3) { albedo = float3(0.36, 0.36, 0.35) * (0.85 + 0.3 * grain); rough = 0.8; return; }
    if (e < 11.0) {                                    // pavement slabs
        float2 t = frac(q / 1.2);
        float joint = (t.x < 0.035 || t.y < 0.035) ? 0.65 : 1.0;
        albedo = float3(0.27, 0.265, 0.25) * (0.85 + 0.3 * hash12(floor(q / 1.2))) * joint * (1.0 - 0.35 * wet);
        rough = lerp(0.85, 0.35, wet);
        return;
    }
    float h = hash12(floor(q / 40.0) + 3.0);
    if (h < 0.3 || (abs(q.x) < 25.0 && abs(q.y) < 25.0)) {
        albedo = float3(0.07, 0.12, 0.04) * (0.7 + 0.6 * fbm(q * 0.3)); rough = 0.95;   // lawn
    } else {
        float2 t = frac(q / 2.5);
        float joint = (t.x < 0.02 || t.y < 0.02) ? 0.7 : 1.0;
        albedo = float3(0.30, 0.27, 0.23) * (0.85 + 0.3 * hash12(floor(q / 2.5))) * joint; rough = 0.8;
    }
}

)HLSL"
R"HLSL(float3 finish(float3 c) {
    c *= skyHorizon.w;
    c = saturate((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14));
    return pow(c, 1.0 / 2.2);
}

struct V3 { float4 pos : SV_Position; float3 world : TEXCOORD0; float3 normal : TEXCOORD1; };
V3 VSMain(float3 pos : POSITION, float3 nrm : NORMAL) {
    V3 o;
    float4 w = mul(model, float4(pos, 1.0));
    o.world = w.xyz;
    o.pos = mul(viewProj, w);
    o.normal = mul((float3x3)model, nrm);
    return o;
}
float4 VSShadow(float3 pos : POSITION, float3 nrm : NORMAL) : SV_Position {
    return mul(shadowVP, mul(model, float4(pos, 1.0)));
}

)HLSL"
R"HLSL(float4 PSMain(V3 i) : SV_Target {
    float3 N = i.normal;
    if (dot(N, N) < 1e-4) N = cross(ddx(i.world), ddy(i.world));
    N = normalize(N);
    float3 toCam = camPos.xyz - i.world;
    float dist = length(toCam);
    float3 V = toCam / dist;
    if (dot(N, V) < 0.0) N = -N;                      // two-sided procedural meshes
    int kind = (int)(params.z + 0.5);
    float night = sunColour.w;
    float3 albedo = toLinear(colour.rgb);
    float3 emissive = albedo * colour.w * 3.0;
    float rough = params.y, metal = params.x, f0 = 0.04, ao = 1.0, coat = 0.0;
    float3 fogDir = normalize(float3(-V.x, abs(V.y) * 0.15, -V.z));
    float fog = saturate(1.0 - exp(-dist * camPos.w));
    if (kind == 6) {
        float3 c = albedo * (1.0 + colour.w * 6.0);
        return float4(finish(lerp(c, skyColour(fogDir, false), fog)), 1.0);
    }
    if (kind == 3) ground(i.world, N, albedo, rough, f0);
    else if (kind == 1) facade(i.world, N, albedo, rough, f0, emissive, ao);
    else if (kind == 2) { rough = 0.03; f0 = 0.06; metal = 0.0; }
    else if (kind == 4) {
        float n = noise2(i.world.xz * 1.3 + i.world.y * 2.1) * 0.6 + noise2(i.world.xz * 4.1 - i.world.y * 3.3) * 0.4;
        albedo *= 0.45 + 1.1 * n; rough = 0.85;
        ao = lerp(0.5, 1.0, saturate(N.y * 0.5 + 0.5));
    } else if (kind == 5) { coat = 1.0; }

    float3 L = normalize(sunDir.xyz);
    float NdotLraw = dot(N, L);
    float NdotL = kind == 4 ? saturate((NdotLraw + 0.35) / 1.35) : saturate(NdotLraw);
    float NdotV = saturate(dot(N, V)) + 1e-4;
    float sh = NdotL > 0.0 ? shadowFactor(i.world, N) : 0.0;
    float3 H = normalize(L + V);
    float a = max(rough * rough, 0.002);
    float NdotH = saturate(dot(N, H));
    float dd = NdotH * NdotH * (a * a - 1.0) + 1.0;
    float D = a * a / (PI * dd * dd);
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G = NdotV / (NdotV * (1.0 - k) + k) * saturate(NdotLraw) / (saturate(NdotLraw) * (1.0 - k) + k);
    float3 F0 = lerp(f0.xxx, albedo, metal);
    float3 F = F0 + (1.0 - F0) * pow(1.0 - saturate(dot(H, V)), 5.0);
    float3 spec = D * G * F / max(4.0 * NdotV * saturate(NdotLraw), 1e-3);
    float3 kd = (1.0 - F) * (1.0 - metal);
    float3 sunRad = sunColour.rgb * sunDir.w * 3.9 * (1.0 - 0.75 * night);
    float3 c = (kd * albedo / PI + spec) * sunRad * NdotL * sh;

    float3 skyAmb = (skyZenith.rgb * 0.6 + skyHorizon.rgb * 0.4);
    float3 bounce = skyHorizon.rgb * 0.25 * float3(0.9, 0.85, 0.8);
    float3 hemi = lerp(bounce, skyAmb, N.y * 0.5 + 0.5) * 0.72;
    float3 R = reflect(-V, N);
    float3 env = skyColour(R, false);
    // Near the horizon a city reflects the buildings across the street, not sky.
    float3 street = skyHorizon.rgb * 0.22 + float3(0.01, 0.01, 0.012);
    env = lerp(street, env, smoothstep(0.02, 0.38, R.y));
    if (R.y < 0.0) env = lerp(env, bounce * 0.6, saturate(-R.y * 4.0));
    env = lerp(env, hemi, saturate(rough * 1.2));
    float3 Fa = F0 + (max(1.0 - rough, F0) - F0) * pow(1.0 - NdotV, 5.0);
    c += (kd * albedo * hemi + env * Fa * (1.0 - 0.75 * rough)) * ao;
    if (coat > 0.0) {                                  // clear coat over the paint
        float Fc = 0.04 + 0.96 * pow(1.0 - NdotV, 5.0);
        float3 envC = lerp(street, skyColour(R, false), smoothstep(0.02, 0.38, R.y));
        if (R.y < 0.0) envC = lerp(envC, bounce * 0.5, saturate(-R.y * 6.0));
        float ac = 0.0025;
        float dc = NdotH * NdotH * (ac - 1.0) + 1.0;
        float3 specC = ac / (PI * dc * dc) * 0.25 * sunRad * saturate(NdotLraw) * sh;
        c = c * (1.0 - Fc) + (envC + specC) * Fc;
    }
    int count = (int)misc.x;
    for (int li = 0; li < count; ++li) {
        float3 Lp = lightPos[li].xyz - i.world;
        float d2 = dot(Lp, Lp);
        float r = lightPos[li].w;
        if (d2 < r * r) {
            float dl = sqrt(d2);
            float3 l = Lp / dl;
            float att = pow(saturate(1.0 - d2 / (r * r)), 2.0) / (1.0 + d2 * 0.12);
            att *= lerp(1.0, smoothstep(0.3, 0.85, l.y), lightCol[li].w);   // streetlamps point down
            float nl = saturate(dot(N, l));
            float3 h2 = normalize(l + V);
            float sp = pow(saturate(dot(N, h2)), lerp(256.0, 8.0, rough)) * (1.0 - rough) * 2.0;
            c += (kd * albedo / PI * 4.0 + sp * F0 * 8.0) * lightCol[li].rgb * nl * att * lerp(1.6, 5.0, lightCol[li].w);
        }
    }
    c += emissive;
    c = lerp(c, skyColour(fogDir, false), fog);
    return float4(finish(c), 1.0);
}

)HLSL"
R"HLSL(struct VSk { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
VSk VSSky(uint id : SV_VertexID) {
    VSk o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
float4 PSSky(VSk i) : SV_Target {
    float4 w = mul(invViewProj, float4(i.ndc, 1.0, 1.0));
    float3 d = normalize(w.xyz / w.w - camPos.xyz);
    return float4(finish(skyColour(d, true)), 1.0);
}

struct V2 { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
V2 VSOverlay(float2 pos : POSITION, float2 uv : TEXCOORD0, float4 col : COLOR0) {
    V2 o;
    o.pos = float4(pos, 0.0, 1.0);
    o.uv = uv;
    o.col = col;
    return o;
}
float4 PSOverlay(V2 i) : SV_Target {
    if (i.uv.x < 0.0) return i.col;
    float a = fontTex.Sample(samp, i.uv).r;
    return float4(i.col.rgb, i.col.a * a);
}
)HLSL";
} // namespace gc6
