#pragma once

// heliasdream-vk Phase 3: the rules of the FSR 3.1 pass (host/fsr3_pass.cpp), as
// plain data with no Vulkan, so tests/fsr3_policy_test.cpp takes them as
// they are: when a context fits, when the history resets, what sharpening
// the setting asks for, and how heliasdream's conventions (host/upscaler_inputs.h)
// map to FSR's.

#include <cstdint>

namespace temporal {

struct Fsr3Sizes {
    std::uint32_t render_w = 0, render_h = 0, output_w = 0, output_h = 0;
    bool operator==(const Fsr3Sizes& o) const {
        return render_w == o.render_w && render_h == o.render_h && output_w == o.output_w && output_h == o.output_h;
    }
    bool operator!=(const Fsr3Sizes& o) const { return !(*this == o); }
};

// FSR 3.1 upscales by 1x (native AA) to 3x (Ultra Performance) on each side.
// Anything else - a scene larger than the output, a ratio past 3 - keeps the
// bilinear copy. A small slack: render sizes are rounded to multiples of 8.
inline bool fsr3_sizes_ok(const Fsr3Sizes& s) {
    if (!s.render_w || !s.render_h || !s.output_w || !s.output_h) return false;
    if (s.render_w > s.output_w || s.render_h > s.output_h) return false;
    return s.output_w <= 3 * s.render_w + 8 && s.output_h <= 3 * s.render_h + 8;
}

// Where the upscaled scene goes in the scale pass's target (test1's dark
// screen): live resolution keeps the display buffers at the largest
// Resolution entry (5120x2160), while the active output is the Resolution
// setting's size (3840x2160). FSR's output size and the copy back are that
// size, never the target's extent. The pass's viewport gives the corner
// when it is that size too; a viewport of another size is reported
// (`from` = ViewportDiffers) and the setting's size is used at 0,0. Without
// a setting that fits, the viewport, else the target.
struct OutputRect {
    std::uint32_t x = 0, y = 0, w = 0, h = 0;
    bool operator==(const OutputRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const OutputRect& o) const { return !(*this == o); }
};
enum class OutputFrom : int { Viewport, Setting, ViewportDiffers, Target };
inline const char* output_from_name(OutputFrom f) {
    switch (f) {
    case OutputFrom::Viewport: return "the Resolution setting, placed by the scale pass's viewport";
    case OutputFrom::Setting: return "the Resolution setting";
    case OutputFrom::ViewportDiffers: return "the Resolution setting; the scale pass's viewport is another size";
    case OutputFrom::Target: return "the target's extent";
    }
    return "?";
}
// vp_*: the pass's viewport in pixels (a flipped one's negative height is
// taken as its absolute value); 0 width or height: none.
inline OutputRect fsr3_output_rect(std::uint32_t target_w, std::uint32_t target_h, float vp_x, float vp_y, float vp_w, float vp_h,
                                   std::uint32_t active_w, std::uint32_t active_h, OutputFrom* from = nullptr) {
    if (vp_h < 0.0f) {
        vp_y += vp_h;
        vp_h = -vp_h;
    }
    const auto round = [](float v) { return v <= 0.0f ? 0u : static_cast<std::uint32_t>(v + 0.5f); };
    const std::uint32_t x = round(vp_x), y = round(vp_y), w = round(vp_w), h = round(vp_h);
    const bool vp_ok = vp_x > -0.5f && vp_y > -0.5f && w >= 16 && h >= 16 && x + w <= target_w && y + h <= target_h;
    OutputFrom f = OutputFrom::Target;
    OutputRect r{0, 0, target_w, target_h};
    if (active_w && active_h && active_w <= target_w && active_h <= target_h) {
        if (vp_ok && w == active_w && h == active_h) {
            f = OutputFrom::Viewport;
            r = {x, y, w, h};
        } else {
            f = vp_ok || (w && h) ? OutputFrom::ViewportDiffers : OutputFrom::Setting;
            r = {0, 0, active_w, active_h};
        }
    } else if (vp_ok) {
        f = OutputFrom::Viewport;
        r = {x, y, w, h};
    }
    if (from) *from = f;
    return r;
}

// A context is made for exact sizes: FSR's resources are sized from its
// maximum render size, so making it for the output size would hold 4K
// resources at every preset. A preset or resolution change makes a new one
// (and the history starts over, as it would anyway).
inline bool fsr3_context_fits(const Fsr3Sizes& made, const Fsr3Sizes& want) { return made == want; }

enum class Fsr3Reset : int { None = 0, NewContext, Cut, Gap, Requested };
inline const char* fsr3_reset_name(Fsr3Reset r) {
    switch (r) {
    case Fsr3Reset::None: return "none";
    case Fsr3Reset::NewContext: return "a new context";
    case Fsr3Reset::Cut: return "a camera cut";
    case Fsr3Reset::Gap: return "frames without the upscaler";
    case Fsr3Reset::Requested: return "asked for";
    }
    return "?";
}

// The history of one context: the last frame (flip) it upscaled.
struct Fsr3History {
    bool primed = false;
    std::uint64_t last_frame = 0;
};

// Whether this frame's dispatch resets the history, and why. A gap: the
// upscaler did not run for more than `max_gap` flips (a load, a menu that
// hides the scene, a fallback frame) - its history is of another picture.
// Call once per dispatch; it records the frame.
inline Fsr3Reset fsr3_reset(Fsr3History& h, std::uint64_t frame, bool cut, bool requested, std::uint64_t max_gap = 3) {
    Fsr3Reset r = Fsr3Reset::None;
    if (!h.primed) r = Fsr3Reset::NewContext;
    else if (cut) r = Fsr3Reset::Cut;
    else if (frame > h.last_frame + max_gap || frame < h.last_frame) r = Fsr3Reset::Gap;
    else if (requested) r = Fsr3Reset::Requested;
    h.primed = true;
    h.last_frame = frame;
    return r;
}

// The setting's sharpness (0..1, settings upscale_sharpness) as FSR's RCAS:
// 0 turns sharpening off; otherwise FSR's own 0..1 (1 the sharpest).
struct Fsr3Sharpen {
    bool enable = false;
    float sharpness = 0.0f;
};
inline Fsr3Sharpen fsr3_sharpen(float setting) {
    const float s = setting < 0.0f ? 0.0f : setting > 1.0f ? 1.0f : setting;
    return {s > 0.0f, s};
}

// heliasdream's jitter is the viewport shift of the scene's draws in render
// pixels, +x right, +y down (host/upscaler_inputs.h): the picture's content
// moved by (+x, +y). FSR's jitterOffset is the same quantity - the offset its
// samples add to the projection, 2x/w and -2y/h in D3D clip space, which
// moves content right and down in texture space. So it is passed as it is;
// `sign_x` / `sign_y` (HELIAS_FSR_JITTER_SIGN for a test session) flip it
// should a hardware test show the picture wobbling in step with the jitter.
// Motion vectors are render pixels, current to previous, +y down - FSR's
// own convention once scaled by motionVectorScale / renderSize: the scale is 1.
struct Fsr3Conventions {
    float jitter_x = 0.0f, jitter_y = 0.0f;
    float mv_scale_x = 1.0f, mv_scale_y = 1.0f;
};
inline Fsr3Conventions fsr3_conventions(float jitter_x, float jitter_y, int sign_x = 1, int sign_y = 1) {
    Fsr3Conventions c;
    c.jitter_x = sign_x < 0 ? -jitter_x : jitter_x;
    c.jitter_y = sign_y < 0 ? -jitter_y : jitter_y;
    return c;
}

// FSR's camera inputs must be finite and positive; the game's are near 0.05,
// far 3000, a vertical FOV from the projection. Bad values get defaults.
struct Fsr3Camera {
    float near_plane = 0.05f, far_plane = 3000.0f, fov_y = 1.0f;
};
inline Fsr3Camera fsr3_camera(float near_plane, float far_plane, float fov_y) {
    Fsr3Camera c;
    const auto ok = [](float v) { return v == v && v > 0.0f && v < 1e30f; };
    if (ok(near_plane)) c.near_plane = near_plane;
    if (ok(far_plane) && far_plane > c.near_plane) c.far_plane = far_plane;
    if (ok(fov_y) && fov_y < 3.1f) c.fov_y = fov_y;
    return c;
}

// test4: FSR 3.1 writes the scale pass's target directly - no copy of its
// own output (0.23 ms a frame at 3840x2160 in test3). The target takes
// STORAGE when it is made (RGBA16F = VkFormat 97, one layer, the display
// buffers' size, the scene split on and FSR 3.1 or 4.1.1 chosen - 4.1.1
// falls back to 3.1 without its assets), and a frame goes direct when the
// output fills it from 0,0 and no stage dump wants FSR's own image.
constexpr int kRgba16fFormat = 97;
inline bool direct_target_wanted(int format, std::uint32_t w, std::uint32_t h, std::uint32_t layers, std::uint32_t display_w,
                                 std::uint32_t display_h, bool split, bool fsr3_kind, bool env_off) {
    if (env_off || !split || !fsr3_kind || format != kRgba16fFormat || layers != 1) return false;
    return w && h && w == display_w && h == display_h;
}
inline bool direct_output_ok(bool target_storage, int target_format, std::uint32_t target_w, std::uint32_t target_h, const OutputRect& out,
                             std::uint32_t fsr_w, std::uint32_t fsr_h, bool dumping) {
    if (!target_storage || target_format != kRgba16fFormat || dumping) return false;
    return out.x == 0 && out.y == 0 && out.w == fsr_w && out.h == fsr_h && target_w >= fsr_w && target_h >= fsr_h;
}

}  // namespace temporal
