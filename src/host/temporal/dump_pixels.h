#pragma once

// heliasdream-vk test3: the FSR stage dump's pixels (host/fsr_dump.h) - each
// image's texels to 8-bit RGB for a PNG, and the numbers a log line carries
// (luminance min / mean / max, NaN and infinite texels, the mean of the left
// three quarters and the right quarter, test2's band). Pure: no Vulkan;
// tests/temporal_test.cpp takes it as it is.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace temporal {

enum class DumpKind : int {
    Color = 0,  // RGBA16F HDR colour: Reinhard, then sRGB's 2.2
    Depth,      // R32F 0..1: stretched between the image's own min and max (near bright)
    Motion,     // RGBA16F: r, g motion in pixels (0.5 grey = none, +-16 px to 0 / 1); b, a as they are
    Mask,       // R8 0..1: grey
};

inline const char* dump_kind_name(DumpKind k) {
    switch (k) {
    case DumpKind::Color: return "colour";
    case DumpKind::Depth: return "depth";
    case DumpKind::Motion: return "motion";
    case DumpKind::Mask: return "mask";
    }
    return "?";
}

inline float dump_half(std::uint16_t h) {
    const std::uint32_t sign = static_cast<std::uint32_t>(h >> 15) << 31;
    std::uint32_t exp = (h >> 10) & 0x1f, man = h & 0x3ff, bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;
        } else {
            exp = 127 - 15 + 1;
            while (!(man & 0x400)) {
                man <<= 1;
                --exp;
            }
            man &= 0x3ff;
            bits = sign | exp << 23 | man << 13;
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000u | man << 13;
    } else {
        bits = sign | (exp + 127 - 15) << 23 | man << 13;
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

inline std::uint8_t dump_unit8(float v) {
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f) return 255;
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}
inline std::uint8_t dump_tone8(float v) {
    if (!(v > 0.0f)) return 0;
    if (std::isinf(v)) return 255;
    return dump_unit8(std::pow(v / (1.0f + v), 1.0f / 2.2f));
}

// Bytes per texel the dump reads for a kind (its image's format).
inline std::size_t dump_bpp(DumpKind k) { return k == DumpKind::Mask ? 1 : k == DumpKind::Depth ? 4 : 8; }

// A texel's four channels as floats (Mask and Depth: one, in [0]).
inline void dump_texel(DumpKind k, const std::uint8_t* p, float out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (k == DumpKind::Mask) {
        out[0] = p[0] / 255.0f;
    } else if (k == DumpKind::Depth) {
        std::memcpy(&out[0], p, 4);
    } else {
        std::uint16_t h[4];
        std::memcpy(h, p, 8);
        for (int c = 0; c < 4; ++c) out[c] = dump_half(h[c]);
    }
}

struct DumpStats {
    double min = 0, max = 0, mean = 0;  // luminance (colour), the value (depth, mask), the length (motion)
    double mean_left = 0, mean_right = 0;  // x < 3/4 of the width, and the rest
    std::uint64_t nan = 0, inf = 0, texels = 0;
};

inline double dump_value(DumpKind k, const float t[4]) {
    if (k == DumpKind::Color) return 0.2126 * t[0] + 0.7152 * t[1] + 0.0722 * t[2];
    if (k == DumpKind::Motion) return std::sqrt(static_cast<double>(t[0]) * t[0] + static_cast<double>(t[1]) * t[1]);
    return t[0];
}

// The stats over w x h texels, rows `pitch` bytes apart.
inline DumpStats dump_stats(DumpKind k, const std::uint8_t* px, std::uint32_t w, std::uint32_t h, std::size_t pitch) {
    DumpStats s;
    double sum = 0, sum_l = 0, sum_r = 0;
    std::uint64_t n = 0, n_l = 0, n_r = 0;
    bool any = false;
    const std::uint32_t split = w - w / 4;
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            float t[4];
            dump_texel(k, px + y * pitch + x * dump_bpp(k), t);
            bool bad = false;
            for (int c = 0; c < 4; ++c) {
                if (std::isnan(t[c])) ++s.nan, bad = true;
                else if (std::isinf(t[c])) ++s.inf, bad = true;
            }
            if (bad) continue;
            const double v = dump_value(k, t);
            if (!any || v < s.min) s.min = v;
            if (!any || v > s.max) s.max = v;
            any = true;
            sum += v;
            ++n;
            if (x < split) sum_l += v, ++n_l;
            else sum_r += v, ++n_r;
        }
    s.texels = static_cast<std::uint64_t>(w) * h;
    s.mean = n ? sum / n : 0;
    s.mean_left = n_l ? sum_l / n_l : 0;
    s.mean_right = n_r ? sum_r / n_r : 0;
    return s;
}

// One row of 8-bit RGB (w * 3 bytes) from a row of texels. `lo`, `hi`: the
// depth's stretch (its stats' min and max).
inline void dump_row_rgb8(DumpKind k, const std::uint8_t* row, std::uint32_t w, double lo, double hi, std::uint8_t* out) {
    for (std::uint32_t x = 0; x < w; ++x) {
        float t[4];
        dump_texel(k, row + x * dump_bpp(k), t);
        std::uint8_t* o = out + x * 3;
        switch (k) {
        case DumpKind::Color:
            for (int c = 0; c < 3; ++c) o[c] = dump_tone8(t[c]);
            break;
        case DumpKind::Depth: {
            const double span = hi - lo;
            const double v = span > 0 ? (hi - t[0]) / span : 0.0;  // near bright
            o[0] = o[1] = o[2] = dump_unit8(static_cast<float>(v));
            break;
        }
        case DumpKind::Motion:
            o[0] = dump_unit8(0.5f + t[0] / 32.0f);
            o[1] = dump_unit8(0.5f + t[1] / 32.0f);
            o[2] = dump_unit8(t[2] * 0.5f + t[3] * 0.5f);
            break;
        case DumpKind::Mask: o[0] = o[1] = o[2] = dump_unit8(t[0]); break;
        }
    }
}

}  // namespace temporal
