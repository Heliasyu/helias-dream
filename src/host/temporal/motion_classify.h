#pragma once

// heliasdream-vk Phase 2: which of the game's draws feed which upscaler input.
// Pure tables and rules, no dependencies (tests/temporal_test.cpp):
//
//  - Shader hashes (the OrbShdr footer's, which heliasdream names pipelines by
//    and shadPS4 calls pgm_hash) from IFreemz's shadPS4-Bloodborne-DLSS-FSR:
//    the game's own object-velocity draws (it renders them for its motion
//    blur into a coarse 160x90 RGBA8 target), the pass that turns depth into
//    linear R32 depth, the motion blur, and the Scaleform UI's vertex shaders.
//  - Constant-buffer sizes from bbport (docs/motion_vectors.md there): 864
//    bytes is the scene block, 640..16384 a character's bone palette, 96..639
//    in steps of 48 (3x4 matrices) a weapon's or a prop's small palette.
//  - The jitter rule (both projects): a draw with the scene's depth bound,
//    not a full-screen pass (more than 6 indices, or instanced, or
//    indirect), not Scaleform.
//  - The moving gate (bbport): a small palette whose bytes match the last
//    frame's did not move - camera motion is exact for it, so it needs no
//    object motion; most of the ~1000 small palettes a frame are static map
//    pieces.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>

namespace temporal {

// IFreemz's hashes.
constexpr std::uint32_t kOpaqueVelocityPs = 0x34bc187cu;  // packing scale in its constant buffer 0
constexpr std::uint32_t kAlphaVelocityPsA = 0x749e4f9eu;  // ... in buffer 1
constexpr std::uint32_t kAlphaVelocityPsB = 0xb25e4faeu;  // ... in buffer 1
constexpr std::uint32_t kDepthProducerPs = 0xd3c8bb21u;   // samples the scene depth, writes linear R32 depth
constexpr std::uint32_t kMotionBlurPs = 0xe0305cefu;
constexpr std::uint32_t kScenePassPs = 0xccbf44a6u;       // the scale pass (Phase 1)

inline bool is_velocity_ps(std::uint32_t h) { return h == kOpaqueVelocityPs || h == kAlphaVelocityPsA || h == kAlphaVelocityPsB; }
// The constant buffer that carries the velocity's packing scale (two floats).
inline int velocity_scale_buffer(std::uint32_t h) { return h == kOpaqueVelocityPs ? 0 : is_velocity_ps(h) ? 1 : -1; }
// test5: whether two floats read there are a packing scale. test4's third
// window read (8.88889, -5): the 160x90 target's size / 18, its y negative,
// which decodes to at most +-144 render pixels. The first two read (1, 1),
// which decodes a static object's 128/255 to a 5-pixel motion and anything
// else to hundreds - the shimmer on characters. A scale's sides keep the
// target's (and the scene's) aspect and are not tiny.
inline bool velocity_scale_plausible(float sx, float sy, float aspect) {
    if (!(sx == sx) || !(sy == sy) || !(aspect > 0.0f)) return false;
    const float ax = sx < 0 ? -sx : sx, ay = sy < 0 ? -sy : sy;
    if (ax < 1.5f || ay < 1.5f || ax > 1e4f || ay > 1e4f) return false;
    const float r = (ax / ay) / aspect;
    return r > 0.9f && r < 1.1f;
}

inline bool is_ui_vs(std::uint32_t h) {
    return h == 0x34e8a281u || h == 0x81d336ceu || h == 0x09957251u || h == 0x24042a9bu || h == 0xa400228bu;
}

// "ab12cd34" -> 0xab12cd34; 0 when it is not eight hex digits.
inline std::uint32_t parse_hash(const char* s, std::size_t n = 8) {
    if (!s) return 0;
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = s[i];
        std::uint32_t d;
        if (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
        else return 0;
        v = v << 4 | d;
    }
    return v;
}

enum class BufferKind : int { Other = 0, Scene, Skeleton, SmallSkeleton };

inline BufferKind classify_buffer(std::uint64_t bytes) {
    if (bytes == 864) return BufferKind::Scene;
    if (bytes >= 640 && bytes <= 16384 && bytes % 16 == 0) return BufferKind::Skeleton;
    if (bytes >= 96 && bytes < 640 && bytes % 48 == 0) return BufferKind::SmallSkeleton;
    return BufferKind::Other;
}

inline const char* buffer_kind_name(BufferKind k) {
    switch (k) {
        case BufferKind::Scene: return "scene";
        case BufferKind::Skeleton: return "skeleton";
        case BufferKind::SmallSkeleton: return "small skeleton";
        default: return "other";
    }
}

struct DrawFacts {
    bool scene_depth = false;  // the depth attachment is this frame's scene depth
    bool ui = false;           // a Scaleform vertex shader
    bool indirect = false;
    std::uint32_t index_count = 0, instances = 1;
};

inline bool jitter_eligible(const DrawFacts& d) {
    return d.scene_depth && !d.ui && (d.indirect || d.index_count > 6 || d.instances > 1);
}

// CB_BLEND0_CONTROL: ENABLE is bit 30. A blended draw into the scene's colour
// is a transparency (particles, fog sheets, glass, hair cards): the reactive
// mask's candidates.
inline bool blend_enabled(std::uint32_t cb_blend_control) { return (cb_blend_control >> 30) & 1u; }

// bbport's moving gate: per key (shader and palette address), whether the
// palette's hash changed since the frame it was last seen. Entries not seen
// for `kForgetFrames` are dropped (and count as moved when they come back).
class MovingGate {
public:
    static constexpr std::uint64_t kForgetFrames = 120;
    bool moved(std::uint64_t key, std::uint64_t hash, std::uint64_t frame) {
        auto it = seen_.find(key);
        if (it == seen_.end()) {
            seen_.emplace(key, Entry{hash, frame});
            return true;
        }
        const bool changed = it->second.hash != hash || frame > it->second.frame + 1;
        it->second = Entry{hash, frame};
        return changed;
    }
    void sweep(std::uint64_t frame) {
        for (auto it = seen_.begin(); it != seen_.end();)
            it = frame > it->second.frame + kForgetFrames ? seen_.erase(it) : std::next(it);
    }
    std::size_t size() const { return seen_.size(); }

private:
    struct Entry {
        std::uint64_t hash, frame;
    };
    std::unordered_map<std::uint64_t, Entry> seen_;
};

}  // namespace temporal
