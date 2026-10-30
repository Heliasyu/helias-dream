#version 450
// What vkCmdBlitImage with a linear filter does to each pixel of its
// destination: sample the source at the pixel's centre through a view in the
// source's own format, and write it in the destination's. At one texel a
// pixel the centres meet and the copy is exact.
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;
void main() {
    // Alpha 1: the HDR encode reads a lower alpha as the overlay's (host/hdr_ui.h).
    out_colour = vec4(textureLod(picture, v_uv, 0.0).rgb, 1.0);
}
