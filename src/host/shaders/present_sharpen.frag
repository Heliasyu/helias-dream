#version 450
// heliasdream-vk rc2, HELIAS_SHARPEN_AT_PRESENT=1 (debugging; off by default): the
// presenter's pass (present.frag) with FSR's sharpening folded in - AMD's RCAS
// (fsr1/ffx_fsr1.h, MIT) cross of five taps, without its noise removal, on the
// display image as it is shown. It saves FSR 4.1.1's own sharpening pass
// (~0.3 ms at 4K) but sharpens what the game drew after the upscale too - the
// HUD, subtitles and menus - so it is not the default: there the sharpening
// stays on the scene alone, before the HUD (host/fsr4_pass.cpp).
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(push_constant) uniform Push { layout(offset = 16) float con; } pc;  // 2^-(2 - 2 * sharpness)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;
void main() {
    const vec4 e4 = textureLod(picture, v_uv, 0.0);
    const vec3 b = textureLodOffset(picture, v_uv, 0.0, ivec2(0, -1)).rgb;
    const vec3 d = textureLodOffset(picture, v_uv, 0.0, ivec2(-1, 0)).rgb;
    const vec3 f = textureLodOffset(picture, v_uv, 0.0, ivec2(1, 0)).rgb;
    const vec3 h = textureLodOffset(picture, v_uv, 0.0, ivec2(0, 1)).rgb;
    const vec3 e = e4.rgb;
    const vec3 mn = min(min(b, d), min(f, h));
    const vec3 mx = max(max(b, d), max(f, h));
    const vec3 hit_min = mn / max(4.0 * mx, vec3(1e-5));
    const vec3 hit_max = (vec3(1.0) - mx) / min(4.0 * mn - 4.0, vec3(-1e-5));
    const vec3 lobe3 = max(-hit_min, hit_max);
    const float lobe = max(-0.1875, min(max(lobe3.r, max(lobe3.g, lobe3.b)), 0.0)) * pc.con;
    const vec3 o = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    out_colour = vec4(clamp(o, 0.0, 1.0), 1.0);  // alpha 1: see present.frag
}
