#pragma once

// heliasdream-vk Phase 2: the game's per-frame scene constants, the camera they
// hold, its history and camera cuts, and the camera motion vectors' math.
// Pure (no Vulkan, no engine): tests/temporal_test.cpp takes it as it is, and
// host/shaders/motion.comp is the same math on the GPU.
//
// The scene constants are an 864-byte constant buffer most scene passes bind
// (bbport's finding, docs/upscaler.md there; IFreemz reads the same block):
//   [0] far plane (3000)  [1] 1/far  [4],[5] render size  [6],[7] 1/size
//   [8..19]   view, 3x4 rows (world -> view; +x right, +y up, +z forward)
//   [52, 57, 62, 63] projection: x scale, y scale, z scale, z offset
//                 (D3D depth 0..1: depth = zs + zo / z)
//   [180..191] the game's inverse view (float32; not used for motion: it is
//                 off by a fraction of a pixel far from the map origin -
//                 IFreemz - so the inverse is built here in double)
// The previous frame's matrices are not in it; CameraHistory keeps them.
// Where two map sections are on screen the game sets a camera per section,
// each in its own coordinates; the camera most of a frame's draws bind is
// the frame's (IFreemz's "camera majority").

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace temporal {

constexpr std::size_t kSceneConstantBytes = 864;
constexpr std::size_t kSceneConstantFloats = kSceneConstantBytes / 4;

// A 3x4 affine transform, rows, the translation in [3], [7], [11].
struct Affine {
    double m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

inline Affine affine_mul(const Affine& a, const Affine& b) {  // a * b
    Affine r;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col) {
            double v = a.m[row * 4 + 0] * b.m[0 * 4 + col] + a.m[row * 4 + 1] * b.m[1 * 4 + col] + a.m[row * 4 + 2] * b.m[2 * 4 + col];
            if (col == 3) v += a.m[row * 4 + 3];
            r.m[row * 4 + col] = v;
        }
    return r;
}

// The general inverse of the 3x3 part (the view need not be orthonormal),
// and the translation through it. False when singular.
inline bool affine_inverse(const Affine& a, Affine* out) {
    const double* m = a.m;
    const double c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!(std::fabs(det) > 1e-12) || !std::isfinite(det)) return false;
    const double id = 1.0 / det;
    double r[9];
    r[0] = c00 * id;
    r[1] = (m[2] * m[9] - m[1] * m[10]) * id;
    r[2] = (m[1] * m[6] - m[2] * m[5]) * id;
    r[3] = c01 * id;
    r[4] = (m[0] * m[10] - m[2] * m[8]) * id;
    r[5] = (m[2] * m[4] - m[0] * m[6]) * id;
    r[6] = c02 * id;
    r[7] = (m[1] * m[8] - m[0] * m[9]) * id;
    r[8] = (m[0] * m[5] - m[1] * m[4]) * id;
    Affine o;
    for (int row = 0; row < 3; ++row) {
        o.m[row * 4 + 0] = r[row * 3 + 0];
        o.m[row * 4 + 1] = r[row * 3 + 1];
        o.m[row * 4 + 2] = r[row * 3 + 2];
        o.m[row * 4 + 3] = -(r[row * 3 + 0] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]);
    }
    *out = o;
    return true;
}

inline void affine_point(const Affine& a, const double p[3], double out[3]) {
    for (int r = 0; r < 3; ++r) out[r] = a.m[r * 4] * p[0] + a.m[r * 4 + 1] * p[1] + a.m[r * 4 + 2] * p[2] + a.m[r * 4 + 3];
}

struct Projection {
    double x = 0, y = 0, zs = 0, zo = 0;  // clip.x = x * view.x, clip.y = y * view.y, depth = zs + zo / view.z
};

struct SceneCamera {
    bool valid = false;
    Affine view;
    Projection proj;
    double far_plane = 0;
    std::uint32_t width = 0, height = 0;  // the render size the constants carry
    float raw_view[12] = {}, raw_proj[4] = {};  // as the game wrote them: what "the same camera" compares
    double position[3] = {};                    // the camera in world space (inverse view's translation, in double)
    double forward[3] = {};                     // its +z axis in world space, unit
};

inline bool same_camera(const SceneCamera& a, const SceneCamera& b) {
    return std::memcmp(a.raw_view, b.raw_view, sizeof(a.raw_view)) == 0 && std::memcmp(a.raw_proj, b.raw_proj, sizeof(a.raw_proj)) == 0;
}

// Whether `f` (floats, `count` of them) is a scene-constant block, and its
// camera. `why` says what failed (for the one log line a run gets).
inline bool parse_scene_constants(const float* f, std::size_t count, SceneCamera* out, const char** why = nullptr) {
    const auto fail = [why](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (!f || count < 192) return fail("shorter than 192 floats");
    for (std::size_t i = 0; i < 192; ++i)
        if (!std::isfinite(f[i])) return fail("not finite");
    // The signature: far and 1/far, the render size and its reciprocals.
    if (!(f[0] >= 100.0f && f[0] <= 100000.0f) || std::fabs(f[1] * f[0] - 1.0f) > 1e-3f) return fail("no far plane at [0], [1]");
    if (!(f[4] >= 64.0f && f[4] <= 16384.0f && f[5] >= 64.0f && f[5] <= 16384.0f) || f[4] != std::floor(f[4]) || f[5] != std::floor(f[5]))
        return fail("no render size at [4], [5]");
    if (std::fabs(f[6] * f[4] - 1.0f) > 1e-3f || std::fabs(f[7] * f[5] - 1.0f) > 1e-3f) return fail("no 1/size at [6], [7]");
    SceneCamera c;
    c.far_plane = f[0];
    c.width = static_cast<std::uint32_t>(f[4]);
    c.height = static_cast<std::uint32_t>(f[5]);
    for (int i = 0; i < 12; ++i) {
        c.raw_view[i] = f[8 + i];
        c.view.m[i] = f[8 + i];
    }
    c.raw_proj[0] = f[52];
    c.raw_proj[1] = f[57];
    c.raw_proj[2] = f[62];
    c.raw_proj[3] = f[63];
    c.proj = {f[52], f[57], f[62], f[63]};
    if (!(std::fabs(c.proj.x) > 1e-4) || !(std::fabs(c.proj.y) > 1e-4) || !(std::fabs(c.proj.zo) > 1e-9)) return fail("no projection at [52], [57], [63]");
    // The view's rotation rows are unit length (a camera, not a scaled transform).
    for (int r = 0; r < 3; ++r) {
        const double* row = &c.view.m[r * 4];
        const double len = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        if (std::fabs(len - 1.0) > 0.02) return fail("the view at [8..19] is not a rotation");
    }
    Affine inv;
    if (!affine_inverse(c.view, &inv)) return fail("the view is singular");
    c.position[0] = inv.m[3];
    c.position[1] = inv.m[7];
    c.position[2] = inv.m[11];
    const double fz[3] = {inv.m[2], inv.m[6], inv.m[10]};
    const double fl = std::sqrt(fz[0] * fz[0] + fz[1] * fz[1] + fz[2] * fz[2]);
    for (int i = 0; i < 3; ++i) c.forward[i] = fz[i] / fl;
    c.valid = true;
    *out = c;
    if (why) *why = nullptr;
    return true;
}

// test3: bbport's signature for the block (vk_camera_motion.cpp): the far
// plane and its reciprocal at [0], [1] and a size of at least 64 at [4], [5]
// - not which size (test2 required the render or output size and found
// nothing). Cheap: what every candidate buffer is tested with before the
// full parse. NaN fails every comparison.
// The signature's head alone: [0], [1], [4], [5] (24 bytes), for a V# that
// carries no size (test3: most of the GX token draws' V#s have 0 records).
inline bool scene_constants_head(const float* f) {
    if (!f) return false;
    const float far = f[0];
    if (!(far >= 100.0f && far <= 100000.0f) || !(std::fabs(f[1] * far - 1.0f) <= 1e-3f)) return false;
    return f[4] >= 64.0f && f[4] <= 16384.0f && f[5] >= 64.0f && f[5] <= 16384.0f;
}

inline bool scene_constants_signature(const float* f, std::size_t count) {
    if (!f || count < 192) return false;
    const float far = f[0];
    if (!(far >= 100.0f && far <= 100000.0f) || !(std::fabs(f[1] * far - 1.0f) <= 1e-3f)) return false;
    return f[4] >= 64.0f && f[4] <= 16384.0f && f[5] >= 64.0f && f[5] <= 16384.0f;
}

// The first offset (in floats, a multiple of 4: 16-byte aligned) in `count`
// floats where the block's signature starts with a whole block after it;
// kNotFound when none. For a buffer that holds the block among others.
constexpr std::size_t kNotFound = ~static_cast<std::size_t>(0);
inline std::size_t find_scene_constants(const float* f, std::size_t count) {
    if (!f || count < 192) return kNotFound;
    for (std::size_t o = 0; o + 192 <= count; o += 4)
        if (scene_constants_signature(f + o, count - o)) return o;
    return kNotFound;
}

// Which camera blocks the frame takes (test3): the render or output size
// always; another size only from a G-buffer draw (bbport takes the first
// constants of each G-buffer pass: five or more colour targets and a depth;
// here four or more), so a reflection's or an item preview's camera with
// its own size is not taken from a draw that is not the scene's.
inline bool camera_size_accepted(std::uint32_t w, std::uint32_t h, std::uint32_t render_w, std::uint32_t render_h, std::uint32_t output_w,
                                 std::uint32_t output_h, bool gbuffer_draw) {
    if (w == render_w && h == render_h) return true;
    if (w == output_w && h == output_h) return true;
    return gbuffer_draw;
}

// Why the history was reset this frame - the upscaler's "reset" input, and a
// line in the test session's log.
enum class Cut : int {
    None = 0,
    First,       // nothing before it
    NoCamera,    // this frame bound no scene constants (a menu over a black scene, a load)
    Gap,         // frames without the scene between this one and the last (a load, a menu)
    Teleport,    // the camera moved more than kTeleportUnits (a map section switch, a warp, a cutscene cut)
    Rotation,    // its forward axis turned more than kCutDegrees (a cutscene cut, a camera reset)
    Projection,  // the field of view jumped more than kFovJump (a cutscene cut, a zoom snap)
    Resize,      // the render size changed
};

inline const char* cut_name(Cut c) {
    switch (c) {
        case Cut::None: return "none";
        case Cut::First: return "first frame";
        case Cut::NoCamera: return "no scene camera";
        case Cut::Gap: return "frames without the scene";
        case Cut::Teleport: return "camera teleport";
        case Cut::Rotation: return "camera turned";
        case Cut::Projection: return "field of view jump";
        case Cut::Resize: return "render size changed";
    }
    return "?";
}

// IFreemz: crossing into another map section moves the coordinates by ~200
// units in one frame. A sprint is well under 1 unit a frame.
constexpr double kTeleportUnits = 20.0;
constexpr double kCutDegrees = 45.0;  // the fastest stick turn is ~10 degrees a frame at 30 fps
constexpr double kFovJump = 0.10;     // 10% in one frame
constexpr std::uint64_t kMaxFrameGap = 2;

// What the motion pass gets (host/shaders/motion.comp's push constants):
// the previous view times the inverse current view, built in double, so a
// small camera-relative transform reaches the GPU instead of two large
// world-space ones; the two projections; and the jitter.
struct Reprojection {
    float reproject[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float proj[4] = {};
    float prev_proj[4] = {};
};

inline Reprojection make_reprojection(const SceneCamera& prev, const SceneCamera& cur) {
    Reprojection r;
    Affine inv;
    if (!affine_inverse(cur.view, &inv)) return r;
    const Affine m = affine_mul(prev.view, inv);
    for (int i = 0; i < 12; ++i) r.reproject[i] = static_cast<float>(m.m[i]);
    r.proj[0] = static_cast<float>(cur.proj.x);
    r.proj[1] = static_cast<float>(cur.proj.y);
    r.proj[2] = static_cast<float>(cur.proj.zs);
    r.proj[3] = static_cast<float>(cur.proj.zo);
    r.prev_proj[0] = static_cast<float>(prev.proj.x);
    r.prev_proj[1] = static_cast<float>(prev.proj.y);
    r.prev_proj[2] = static_cast<float>(prev.proj.zs);
    r.prev_proj[3] = static_cast<float>(prev.proj.zo);
    return r;
}

// The cameras one frame bound, counted; the frame's camera is the most used.
class CameraHistory {
public:
    // Every scene-constant block a scene draw binds.
    void observe(const SceneCamera& c) {
        if (!c.valid) return;
        for (Seen& s : seen_)
            if (same_camera(s.camera, c)) {
                ++s.uses;
                return;
            }
        if (seen_.size() < 8) seen_.push_back({c, 1});
    }
    std::size_t cameras_this_frame() const { return seen_.size(); }

    // At the end of the frame's scene (the scale pass), with a running frame
    // counter (the flip): takes the frame's camera, moves the last one to
    // previous, and says whether the history is cut.
    Cut end_frame(std::uint64_t frame) {
        Cut cut = Cut::None;
        if (seen_.empty()) {
            cut = Cut::NoCamera;
            current_.valid = false;
        } else {
            const Seen* most = &seen_[0];
            for (const Seen& s : seen_)
                if (s.uses > most->uses) most = &s;
            previous_ = current_;
            current_ = most->camera;
            if (!previous_.valid) cut = have_frame_ ? Cut::Gap : Cut::First;
            else if (frame > last_frame_ + kMaxFrameGap || frame < last_frame_) cut = Cut::Gap;
            else cut = compare(previous_, current_);
        }
        if (cut == Cut::NoCamera && !have_frame_) cut = Cut::First;
        have_frame_ = true;
        last_frame_ = frame;
        last_cut_ = cut;
        seen_.clear();
        return cut;
    }

    static Cut compare(const SceneCamera& prev, const SceneCamera& cur) {
        if (prev.width != cur.width || prev.height != cur.height) return Cut::Resize;
        double d2 = 0;
        for (int i = 0; i < 3; ++i) d2 += (cur.position[i] - prev.position[i]) * (cur.position[i] - prev.position[i]);
        if (d2 > kTeleportUnits * kTeleportUnits) return Cut::Teleport;
        const double dot = prev.forward[0] * cur.forward[0] + prev.forward[1] * cur.forward[1] + prev.forward[2] * cur.forward[2];
        if (dot < std::cos(kCutDegrees * 3.14159265358979323846 / 180.0)) return Cut::Rotation;
        if (std::fabs(cur.proj.x / prev.proj.x - 1.0) > kFovJump || std::fabs(cur.proj.y / prev.proj.y - 1.0) > kFovJump) return Cut::Projection;
        return Cut::None;
    }

    // Whether motion can be computed from the two cameras (no cut, both valid).
    bool usable() const { return last_cut_ == Cut::None && current_.valid && previous_.valid; }
    const SceneCamera& current() const { return current_; }
    const SceneCamera& previous() const { return previous_; }
    Cut last_cut() const { return last_cut_; }

private:
    struct Seen {
        SceneCamera camera;
        std::uint32_t uses = 0;
    };
    std::vector<Seen> seen_;
    SceneCamera current_, previous_;
    std::uint64_t last_frame_ = 0;
    bool have_frame_ = false;
    Cut last_cut_ = Cut::First;
};

// Projects a world point with a camera into a w x h render target shifted by
// `jitter` (render pixels, +y down): its pixel position and its depth. False
// behind the camera. What a jittered scene draw does (tests).
inline bool project(const SceneCamera& c, const double world[3], std::uint32_t w, std::uint32_t h, const double jitter[2], double* px,
                    double* py, double* depth) {
    double v[3];
    affine_point(c.view, world, v);
    if (!(v[2] > 1e-6)) return false;
    const double nx = c.proj.x * v[0] / v[2], ny = c.proj.y * v[1] / v[2];
    *px = (nx * 0.5 + 0.5) * w + jitter[0];
    *py = (0.5 - ny * 0.5) * h + jitter[1];
    *depth = c.proj.zs + c.proj.zo / v[2];
    return true;
}

// The camera motion of render pixel (x, y) with depth `raw_depth`, in render
// pixels, current to previous (+y down) - motion.comp's math in double.
// Depth 1 (cleared, the sky) is a direction: rotated, not translated.
inline void camera_motion(const Reprojection& r, std::uint32_t x, std::uint32_t y, double raw_depth, std::uint32_t w, std::uint32_t h,
                          const double jitter[2], double* dx, double* dy) {
    const double u = (x + 0.5 - jitter[0]) / w, v = (y + 0.5 - jitter[1]) / h;
    const double nx = 2 * u - 1, ny = 1 - 2 * v;
    const bool sky = raw_depth >= 1.0;
    const double z = sky ? 1.0 : r.proj[3] / std::fmin(raw_depth - r.proj[2], -1e-8);
    const double view[4] = {nx * z / r.proj[0], ny * z / r.proj[1], z, sky ? 0.0 : 1.0};
    double pv[3];
    for (int i = 0; i < 3; ++i)
        pv[i] = r.reproject[i * 4] * view[0] + r.reproject[i * 4 + 1] * view[1] + r.reproject[i * 4 + 2] * view[2] + r.reproject[i * 4 + 3] * view[3];
    const double pz = std::fmax(pv[2], 1e-4);
    const double pnx = pv[0] * r.prev_proj[0] / pz, pny = pv[1] * r.prev_proj[1] / pz;
    const double pu = pnx * 0.5 + 0.5, pvv = 0.5 - pny * 0.5;
    *dx = (pu - u) * w;
    *dy = (pvv - v) * h;
}

}  // namespace temporal
