#pragma once

// heliasdream-vk Phase 2: the sub-pixel jitter a temporal upscaler needs.
// Halton(2,3), the sequence FSR 3.1 / FSR 4 / DLSS / XeSS all take, with the
// FidelityFX SDK's phase count (ffxFsr3GetJitterPhaseCount: 8 x the
// upscale ratio squared, truncated) and offsets (ffxFsr3GetJitterOffset:
// halton(index % phases + 1) - 0.5 on each axis). Pure; no dependencies -
// tests/temporal_test.cpp takes it as it is.
//
// The offset is in render pixels, +x right, +y down: the shift heliasdream
// applies to the viewport of the scene's geometry draws (host/render.cpp).
// Each backend maps it to its own convention when it is handed over
// (host/upscaler_inputs.h).

#include <cmath>
#include <cstdint>

namespace temporal {

// The radical inverse of index in base (index 0 is 0).
inline double halton(std::uint32_t index, std::uint32_t base) {
    double f = 1.0, r = 0.0;
    while (index > 0) {
        f /= static_cast<double>(base);
        r += f * static_cast<double>(index % base);
        index /= base;
    }
    return r;
}

// FidelityFX: int(8 * (display / render)^2), at least 1. 18 phases for
// Quality (1.5x), 23 for Balanced (1.7x), 32 for Performance (2x), 72 for
// Ultra Performance (3x); 8 at native.
inline std::uint32_t jitter_phase_count(std::uint32_t render_width, std::uint32_t display_width) {
    if (!render_width || !display_width) return 1;
    const double ratio = static_cast<double>(display_width) / static_cast<double>(render_width);
    const auto n = static_cast<std::int32_t>(8.0 * ratio * ratio);
    return n < 1 ? 1u : static_cast<std::uint32_t>(n);
}

struct Jitter {
    float x = 0.0f, y = 0.0f;  // render pixels, each in [-0.5, 0.5)
    std::uint32_t phase = 0, phases = 1;
};

// The jitter of frame `frame` (any running counter) with `phases` phases.
inline Jitter jitter_offset(std::uint64_t frame, std::uint32_t phases) {
    Jitter j;
    j.phases = phases ? phases : 1;
    j.phase = static_cast<std::uint32_t>(frame % j.phases);
    j.x = static_cast<float>(halton(j.phase + 1, 2) - 0.5);
    j.y = static_cast<float>(halton(j.phase + 1, 3) - 0.5);
    return j;
}

// test3: the jitter pauses once this many frames ran without a scene camera
// (every one a history reset: the jitter would only flicker).
constexpr std::uint64_t kJitterPauseFrames = 60;
inline bool jitter_paused_without_camera(std::uint64_t frames_without_camera) { return frames_without_camera >= kJitterPauseFrames; }

}  // namespace temporal
