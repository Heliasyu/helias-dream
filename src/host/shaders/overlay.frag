#version 450
layout(set = 0, binding = 0) uniform sampler2D atlas;
// Colour pictures (the trophies' icons): a vertex whose u is 2 or more.
layout(set = 0, binding = 1) uniform sampler2D pictures;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 0) out vec4 out_colour;
void main() {
    if (v_uv.x >= 2.0) {
        // A picture is one quad, so a 2x2 pixel group never mixes it with the
        // atlas; no mips, so the level is fixed anyway.
        vec4 p = textureLod(pictures, v_uv - vec2(2.0, 0.0), 0.0);
        out_colour = vec4(p.rgb * v_colour.rgb, p.a * v_colour.a);
        return;
    }
    // The atlas is coverage only; the colour comes from the vertex.
    out_colour = vec4(v_colour.rgb, v_colour.a * texture(atlas, v_uv).r);
}
