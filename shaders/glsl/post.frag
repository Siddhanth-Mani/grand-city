#version 450
// Post chain fragment (Vulkan): ACES tone mapping + vignette.
// The bloom bright-pass/blur and FXAA passes share this shader with
// different push-constant modes; the HLSL version (post.hlsl) is the
// reference implementation with per-pass entry points.
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColour;
layout(set = 0, binding = 0) uniform sampler2D inputTex;
layout(push_constant) uniform PostPC {
    float exposure; float bloomThreshold; float bloomStrength; int mode; // 0=composite 1=bright 2=fxaa
    vec2 texelSize;
} pc;
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec3 c = texture(inputTex, inUV).rgb;
    if (pc.mode == 1) {
        float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
        outColour = vec4(c * step(pc.bloomThreshold, lum), 1.0);
        return;
    }
    if (pc.mode == 2) {
        float luma = dot(c, vec3(0.299, 0.587, 0.114));
        vec2 t = pc.texelSize;
        float range = max(max(dot(texture(inputTex, inUV + vec2(0, -t.y)).rgb, vec3(0.299, 0.587, 0.114)),
                              dot(texture(inputTex, inUV + vec2(0, t.y)).rgb, vec3(0.299, 0.587, 0.114))),
                          max(dot(texture(inputTex, inUV + vec2(-t.x, 0)).rgb, vec3(0.299, 0.587, 0.114)),
                              dot(texture(inputTex, inUV + vec2(t.x, 0)).rgb, vec3(0.299, 0.587, 0.114))))
                      - min(min(dot(texture(inputTex, inUV + vec2(0, -t.y)).rgb, vec3(0.299, 0.587, 0.114)),
                              dot(texture(inputTex, inUV + vec2(0, t.y)).rgb, vec3(0.299, 0.587, 0.114))),
                          min(dot(texture(inputTex, inUV + vec2(-t.x, 0)).rgb, vec3(0.299, 0.587, 0.114)),
                              dot(texture(inputTex, inUV + vec2(t.x, 0)).rgb, vec3(0.299, 0.587, 0.114))));
        if (range < 0.08) { outColour = vec4(c, 1.0); return; }
        vec3 avg = (texture(inputTex, inUV + vec2(-t.x, -t.y)).rgb +
                    texture(inputTex, inUV + vec2(t.x, t.y)).rgb +
                    texture(inputTex, inUV + vec2(-t.x, t.y)).rgb +
                    texture(inputTex, inUV + vec2(t.x, -t.y)).rgb) * 0.25;
        outColour = vec4(mix(c, avg, 0.5), 1.0);
        return;
    }
    c = aces(c * pc.exposure);
    vec2 d = inUV - 0.5;
    c *= 1.0 - 0.35 * dot(d, d) * 2.0;
    outColour = vec4(c, 1.0);
}
