#version 450
// Helia's Dream's frosted glass (host/present_pass.cpp, present_pass_glass):
// the game's picture, blurred, inside a rounded rectangle of the overlay (the
// F10 menu's pane). Drawn over the picture before the overlay, so the menu's
// rows and text sit on the blur with no pane colour of their own.
//
// One pass, no intermediate image: a golden-angle spiral of taps over a disc,
// turned per pixel by interleaved gradient noise, so the few taps a pixel has
// read as the fine grain of frosted glass rather than as rings.
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;
layout(push_constant) uniform Glass {
    layout(offset = 16) vec4 rect;  // x0, y0, x1, y1 in framebuffer pixels
    vec4 params;                    // corner radius (px), blur radius in uv (x, y), brightness
} g;

float noise(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    const vec2 p = gl_FragCoord.xy;
    const vec2 centre = (g.rect.xy + g.rect.zw) * 0.5;
    const vec2 half_size = (g.rect.zw - g.rect.xy) * 0.5;
    const float r = g.params.x;
    const vec2 q = abs(p - centre) - (half_size - vec2(r));
    const float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    const float mask = clamp(0.5 - d, 0.0, 1.0);
    if (mask <= 0.0) discard;

    const int kTaps = 56;
    const float turn = noise(p) * 6.2831853;
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < kTaps; ++i) {
        const float t = (float(i) + 0.5) / float(kTaps);
        const float a = turn + float(i) * 2.39996323;
        const vec2 off = vec2(cos(a), sin(a)) * sqrt(t) * g.params.yz;
        const float w = 1.0 - 0.55 * t;
        sum += textureLod(picture, v_uv + off, 0.0).rgb * w;
        weight += w;
    }
    vec3 c = sum / weight;
    // A touch less colour and light, so white text reads on any scene.
    c = mix(c, vec3(dot(c, vec3(0.299, 0.587, 0.114))), 0.12) * g.params.w;
    out_colour = vec4(c, mask);
}
