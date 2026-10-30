#version 450
// HDR output (host/present_pass.cpp): the finished SDR frame - picture,
// menus, counter - encoded for an HDR10 display (BT.2020, PQ). SDR is decoded
// with a 2.2 power curve and shown with its white at paper white; above a
// knee the brightest channel is expanded so SDR white reaches
// 1 + (peak / paper - 1) * boost times paper white (Lantern Engine's "expand"
// mode, docs/hdr.md), hue kept, and nothing passes the display's peak.
//
// Helia's Dream, the UI kept apart (host/hdr_ui.h): with a UI brightness set,
// UI is shown at that brightness and not expanded - the game's (a pixel of the
// display buffer that differs from its copy from before its UI) and the
// overlay's (where it lowered the layer's alpha: host/overlay.cpp).
layout(set = 0, binding = 0) uniform sampler2D frame;
layout(set = 0, binding = 1) uniform sampler2D source;  // the display buffer, as the picture drew it
layout(set = 0, binding = 2) uniform sampler2D preui;   // the same frame before its UI
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;
layout(push_constant) uniform Hdr {
    layout(offset = 16) vec4 p;  // paper white (nits), peak (nits), boost (0..1), knee (linear)
    vec4 ui;                     // game UI apart (1/0), UI brightness (x paper white), overlay apart (1/0)
    vec4 shown;                  // the picture's rectangle in the target (pixels)
    vec4 src;                    // its region of the display buffer (pixels)
} pc;

vec3 pq(vec3 nits) {
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

bool is_ui(float layer_alpha) {
    if (pc.ui.z > 0.5 && layer_alpha < 0.999) return true;
    if (pc.ui.x < 0.5) return false;
    const vec2 t = (gl_FragCoord.xy - pc.shown.xy) / max(pc.shown.zw, vec2(1.0));
    if (any(lessThan(t, vec2(0.0))) || any(greaterThanEqual(t, vec2(1.0)))) return false;
    const ivec2 at = ivec2(pc.src.xy + floor(t * pc.src.zw));
    const vec3 a = texelFetch(source, at, 0).rgb, b = texelFetch(preui, min(at, textureSize(preui, 0) - 1), 0).rgb;
    return any(greaterThan(abs(a - b), vec3(3.0 / 255.0)));
}

void main() {
    const vec4 layer = textureLod(frame, v_uv, 0.0);
    const vec3 sdr = layer.rgb;
    vec3 lin = pow(max(sdr, vec3(0.0)), vec3(2.2));
    const float paper = pc.p.x, peak = max(pc.p.y, paper), boost = pc.p.z, knee = pc.p.w;
    float white = paper;
    if (is_ui(layer.a)) {
        white = paper * pc.ui.y;
    } else {
        const float top = 1.0 + (peak / paper - 1.0) * boost;  // where SDR white lands, in paper whites
        const float m = max(max(lin.r, lin.g), lin.b);
        if (m > knee && top > 1.0) {
            // Smooth from the knee: slope 1 there, SDR white at `top`.
            const float t = (m - knee) / (1.0 - knee);
            const float expanded = knee + (m - knee) + (top - 1.0) * t * t;
            lin *= expanded / m;
        }
    }
    // BT.709 to BT.2020 primaries.
    const mat3 to2020 = mat3(0.6274040, 0.0690970, 0.0163916,
                             0.3292820, 0.9195400, 0.0880132,
                             0.0433136, 0.0113612, 0.8955950);
    vec3 nits = to2020 * lin * white;
    nits = min(nits, vec3(peak));
    out_colour = vec4(pq(nits), 1.0);
}
