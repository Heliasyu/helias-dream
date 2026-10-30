#version 450
// The presenter's picture with the player's grade (Graphics: Brightness,
// Contrast, Hue shift; host/present_pass.cpp). Used instead of present.frag
// only when the grade is not the game's own, so the default path stays the
// exact copy. Applied in the picture's own (gamma) values: brightness scales,
// contrast pivots round mid grey, the hue turns round the grey axis.
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;
layout(push_constant) uniform Grade {
    layout(offset = 16) vec4 g;  // brightness, contrast, cos(hue), sin(hue)
} pc;
void main() {
    vec4 c = textureLod(picture, v_uv, 0.0);
    vec3 rgb = c.rgb * pc.g.x;
    rgb = (rgb - 0.5) * pc.g.y + 0.5;
    // Rotation about (1,1,1)/sqrt(3) (Rodrigues).
    const float k = 0.57735027;
    const vec3 axis = vec3(k);
    rgb = rgb * pc.g.z + cross(axis, rgb) * pc.g.w + axis * dot(axis, rgb) * (1.0 - pc.g.z);
    out_colour = vec4(clamp(rgb, 0.0, 1.0), 1.0);  // alpha 1: see present.frag
}
