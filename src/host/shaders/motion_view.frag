#version 450
// heliasdream-vk Phase 2: the motion-vector view (Show Motion Vectors), drawn by
// the presenter over the picture (host/present_pass.cpp). Red is the
// horizontal motion, green the vertical, both by size (8 render pixels a
// frame is full); blue marks the game's object velocity (bright: used,
// dim - test5 - covered by the game's 160x90 velocity target but the camera's
// motion kept); where the camera motion was not valid the picture shows
// through darkened.
layout(set = 0, binding = 0) uniform sampler2D motion;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;
void main() {
    const vec4 m = textureLod(motion, v_uv, 0.0);
    if (m.a < 0.5 && m.b < 0.5) {
        out_colour = vec4(0.0, 0.0, 0.0, 0.5);
        return;
    }
    const vec2 a = clamp(abs(m.rg) / 8.0, 0.0, 1.0);
    out_colour = vec4(sqrt(a), m.b, 0.75);
}
