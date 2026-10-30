#pragma once

// heliasdream-vk's upscaler choices and quality presets (docs/vk/PLAN.md), as
// plain data: the settings (host/settings.h: upscaler, upscale_quality,
// upscale_sharpness) index these tables, and the render size a preset asks
// for is computed here, the one place, so the menus, the log and the later
// upscaler passes cannot disagree. No dependencies: tests/upscaler_test.cpp
// takes it as it is.

#include <cmath>
#include <cstdint>

namespace upscaler {

// kTaa (Helia's Dream): FSR 3.1 at the output's own size - AMD's Native AA
// mode, a temporal AA with no upscale - whatever the quality preset says.
// kFsr4: AMD's FSR 4 INT8 model (host/fsr4v07_pass.h), assets downloaded.
// kDlss: NVIDIA DLSS through the driver's NGX core (host/dlss_pass.h).
enum Kind : int { kOff = 0, kFsr1 = 1, kFsr31 = 2, kFsr411 = 3, kTaa = 4, kFsr4 = 5, kDlss = 6, kKindCount = 7 };

// The temporal ones: motion vectors, jitter and FSR's pass at the scale pass.
inline bool is_temporal(int k) { return k == kFsr31 || k == kFsr411 || k == kTaa || k == kFsr4 || k == kDlss; }

// AMD's quality modes: output size / render size.
enum Preset : int { kNativeAa = 0, kQuality = 1, kBalanced = 2, kPerformance = 3, kUltraPerformance = 4, kPresetCount = 5 };

inline const char* kind_name(int k) {
    static const char* const kNames[kKindCount] = {"Off", "FSR 1", "FSR 3.1", "FSR 4.1.1", "Native TAA", "FSR 4", "DLSS"};
    return k >= 0 && k < kKindCount ? kNames[k] : "?";
}

inline const char* preset_name(int p) {
    static const char* const kNames[kPresetCount] = {"Native AA", "Quality", "Balanced", "Performance", "Ultra Performance"};
    return p >= 0 && p < kPresetCount ? kNames[p] : "?";
}

// The FSR 3 / FSR 4 ratios: 1.0 (native AA), 1.5, 1.7, 2.0, 3.0.
inline double preset_ratio(int p) {
    static const double kRatio[kPresetCount] = {1.0, 1.5, 1.7, 2.0, 3.0};
    return p >= 0 && p < kPresetCount ? kRatio[p] : 1.0;
}

// The render size for an output size: each side divided by the ratio and
// rounded to the nearest pixel (3840x2160 Performance is 1920x1080), never
// below 1.
inline void render_size(int preset, std::uint32_t out_w, std::uint32_t out_h, std::uint32_t* w, std::uint32_t* h) {
    const double r = preset_ratio(preset);
    const auto side = [r](std::uint32_t v) {
        const double s = std::floor(static_cast<double>(v) / r + 0.5);
        return static_cast<std::uint32_t>(s < 1.0 ? 1.0 : s);
    };
    *w = side(out_w);
    *h = side(out_h);
}

// The settings' sharpness (0..1, FSR 3's convention) as FSR 1's RCAS stops
// below its maximum: 1 - sharpness, so 0.8 is the 0.2 stops heliasdream's FSR 1
// has always used (host/fsr.cpp) and 1.0 is RCAS at full strength.
inline float rcas_stops(float sharpness) {
    const float s = sharpness < 0.0f ? 0.0f : sharpness > 1.0f ? 1.0f : sharpness;
    return 1.0f - s;
}

}  // namespace upscaler
