#pragma once

// heliasdream-vk Phase 1: where the scene's size is split from the UI's, as data.
//
// The sites are Kyo's "Decoupled 1080p UI (<w>x<h> render res)" patches for
// Bloodborne 1.09 (the shadPS4 community patch list, shadps4-emu/ps4_cheats
// PATCHES/Bloodborne.xml), which render the scene at one size while the
// post-processing, Scaleform and the display buffers keep the output entry's.
// Each of those patches is the same list of writes with the size as
// immediates; here the list is a table and the size a parameter, so a live
// change can rewrite it (engine/scene_split.cpp). Addresses are Binary
// Ninja's (0x400000 base), as everywhere in heliasdream; they are the same as the
// community list's.
//
// What each write does, as far as the addresses and heliasdream's own notes on the
// functions around them say (engine/live_resolution.cpp names them):
//
//   0x2697aea/af0   sub_26979b0, the graphics manager's construction: the
//                   size its scene context is first built at
//   0x26f8ce1/ce7   sub_26f8c70, a scene context made on demand
//   0x26f93d8/3df   sub_26f9330, the same (two sizes)
//   0x26f99db/9e2
//   0x26c65f6       sub_26c6540, a registered scene context's resize slot:
//   0x26c6624/62e   a branch taken and the size it rebuilds at, twice
//   0x26c6735
//   0x26c6760/76a
//   0x269c7d5       sub_269c200, the graphics manager's resize listener: the
//                   description its scene context is rebuilt from
//                   ([rsp+0x20], [rsp+0x24]) - a stub (13 bytes are too few
//                   for the two stores)
//   0x26c3af3       GXSceneContext::Initialize (sub_26c39f0): the colour
//                   target is always the output entry's DefRenderTarget, so
//                   what follows the scene (YEBIS, the UI) is at the
//                   output's size; the depth keeps its own (render size)
//   0x26c3de4/df0   Initialize: its half-size targets from its own
//                   description (the render size), not the output's
//   0x125b1a3/1aa   half and quarter targets from the scene's size ([rbx])
//   0x125b1d6/1de
//   0x25d818d       YEBIS: its viewport from the output entry's size (+0x80,
//                   +0x84 of the GX device's +0x90) - the post chain at the
//                   output's size
//   0x1258f2c       a size read that becomes "the output entry's" (a call
//                   to a stub returning eax = width, ecx = height)
//
// The game then scales its HDR scene up to the output size in one pass
// before post-processing (IFreemz's "decoupled UI" finding; the pixel shader
// ccbf44a6, host/render.cpp) - the pass Phase 3 replaces with FSR.
//
// No dependencies: tests/scene_split_test.cpp takes it as it is.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace scene_split {

// What a site's bytes are made of.
enum class Kind : std::uint8_t {
    Width,   // opcode, the render width (imm32), then nops
    Height,  // opcode, the render height (imm32), then nops
    Fixed,   // the bytes as given
    Branch,  // a short Jcc made a jmp (one byte, EB): the original must be 0x70..0x7f
    Stub,    // a rel32 jump or call to a host stub (engine/scene_split.cpp writes it)
};

struct Site {
    std::uint64_t at;
    Kind kind;
    std::uint8_t n;               // bytes written
    const std::uint8_t* bytes;    // Fixed: all n; Width/Height: the opcode (1 byte); Branch/Stub: null
    const char* what;
};

namespace detail {
inline constexpr std::uint8_t kB9[] = {0xb9}, kBa[] = {0xba}, kBe[] = {0xbe}, kBb[] = {0xbb}, kB8[] = {0xb8};
// jmp 0x26c3b2b: the colour path that keeps the output entry's DefRenderTarget.
inline constexpr std::uint8_t kKeepColour[] = {0xeb, 0x36};
// mov rax, [rip -> 0x59406c8 the GX device]; mov rax, [rax+0x90]; mov ecx, [rax+0x80];
// mov r8d, [rax+0x84]; vmovss xmm0, [rip -> 0x4d29794]; xor esi, esi; xor edx, edx;
// vxorps xmm1, xmm1, xmm1; xchg ax, ax
inline constexpr std::uint8_t kYebisViewport[] = {
    0x48, 0x8b, 0x05, 0x34, 0x85, 0x36, 0x03, 0x48, 0x8b, 0x80, 0x90, 0x00, 0x00, 0x00, 0x8b, 0x88,
    0x80, 0x00, 0x00, 0x00, 0x44, 0x8b, 0x80, 0x84, 0x00, 0x00, 0x00, 0xc5, 0xfa, 0x10, 0x05, 0xe4,
    0x15, 0x75, 0x02, 0x31, 0xf6, 0x31, 0xd2, 0xc5, 0xf0, 0x57, 0xc9, 0x66, 0x90};
inline constexpr std::uint8_t kHalfW[] = {0x8b, 0x03, 0xd1, 0xe8, 0x89, 0x04, 0x24};                     // mov eax, [rbx]; shr eax, 1; mov [rsp], eax
inline constexpr std::uint8_t kHalfH[] = {0x8b, 0x43, 0x04, 0xd1, 0xe8, 0x89, 0x44, 0x24, 0x04};         // mov eax, [rbx+4]; shr eax, 1; mov [rsp+4], eax
inline constexpr std::uint8_t kQuarterW[] = {0x8b, 0x03, 0xc1, 0xe8, 0x02, 0x89, 0x04, 0x24};             // mov eax, [rbx]; shr eax, 2; mov [rsp], eax
inline constexpr std::uint8_t kQuarterH[] = {0x8b, 0x43, 0x04, 0xc1, 0xe8, 0x02, 0x89, 0x44, 0x24, 0x04}; // mov eax, [rbx+4]; shr eax, 2; mov [rsp+4], eax
inline constexpr std::uint8_t kCtxHalfW[] = {0x41, 0x8b, 0x07, 0xd1, 0xe8};                              // mov eax, [r15]; shr eax, 1
inline constexpr std::uint8_t kCtxHalfH[] = {0x41, 0x8b, 0x47, 0x04, 0xd1, 0xe8};                        // mov eax, [r15+4]; shr eax, 1
}  // namespace detail

// The listener's stub site and where it goes back to; the size-read call site.
inline constexpr std::uint64_t kListenerSite = 0x269c7d5, kListenerBack = 0x269c7e2;
inline constexpr std::uint64_t kOutputSizeCallSite = 0x1258f2c;
inline constexpr std::uint64_t kGxDeviceSlot = 0x59406c8;

inline const std::vector<Site>& sites() {
    using namespace detail;
    static const std::vector<Site> k = {
        {0x2697aea, Kind::Width, 6, kB9, "graphics manager: its scene's width (mov ecx)"},
        {0x2697af0, Kind::Height, 6, kBa, "graphics manager: its scene's height (mov edx)"},
        {0x26f8ce1, Kind::Width, 6, kB9, "sub_26f8c70's scene context: width (mov ecx)"},
        {0x26f8ce7, Kind::Height, 6, kBe, "sub_26f8c70's scene context: height (mov esi)"},
        {0x26c3af3, Kind::Fixed, 2, kKeepColour, "Initialize: colour into the output entry's DefRenderTarget"},
        {kListenerSite, Kind::Stub, 13, nullptr, "graphics manager's listener: the scene's rebuild size (stub)"},
        {0x26c65f6, Kind::Branch, 1, nullptr, "resize slot: first branch taken"},
        {0x26c6624, Kind::Width, 7, kBa, "resize slot: width (mov edx)"},
        {0x26c662e, Kind::Height, 7, kB8, "resize slot: height (mov eax)"},
        {0x26c6735, Kind::Branch, 1, nullptr, "resize slot: second branch taken"},
        {0x26c6760, Kind::Width, 7, kBa, "resize slot: width again (mov edx)"},
        {0x26c676a, Kind::Height, 7, kB8, "resize slot: height again (mov eax)"},
        {0x26f93d8, Kind::Width, 7, kBb, "sub_26f9330's scene context: width (mov ebx)"},
        {0x26f93df, Kind::Height, 7, kBe, "sub_26f9330's scene context: height (mov esi)"},
        {0x26f99db, Kind::Width, 7, kBe, "sub_26f9330's second context: width (mov esi)"},
        {0x26f99e2, Kind::Height, 7, kB9, "sub_26f9330's second context: height (mov ecx)"},
        {0x25d818d, Kind::Fixed, sizeof(kYebisViewport), kYebisViewport, "YEBIS: viewport from the output entry"},
        {0x125b1a3, Kind::Fixed, sizeof(kHalfW), kHalfW, "half width from the scene's size"},
        {0x125b1aa, Kind::Fixed, sizeof(kHalfH), kHalfH, "half height from the scene's size"},
        {0x125b1d6, Kind::Fixed, sizeof(kQuarterW), kQuarterW, "quarter width from the scene's size"},
        {0x125b1de, Kind::Fixed, sizeof(kQuarterH), kQuarterH, "quarter height from the scene's size"},
        {0x26c3de4, Kind::Fixed, sizeof(kCtxHalfW), kCtxHalfW, "Initialize: half width from its description"},
        {0x26c3df0, Kind::Fixed, sizeof(kCtxHalfH), kCtxHalfH, "Initialize: half height from its description"},
        {kOutputSizeCallSite, Kind::Stub, 10, nullptr, "a size read: the output entry's (call to a stub)"},
    };
    return k;
}

// A non-stub site's bytes for a render size of w x h; empty for a stub.
inline std::vector<std::uint8_t> encode(const Site& s, std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> out;
    switch (s.kind) {
    case Kind::Width:
    case Kind::Height: {
        const std::uint32_t v = s.kind == Kind::Width ? w : h;
        out.assign(s.n, 0x90);
        out[0] = s.bytes[0];
        std::memcpy(out.data() + 1, &v, 4);
        break;
    }
    case Kind::Fixed: out.assign(s.bytes, s.bytes + s.n); break;
    case Kind::Branch: out.assign(1, 0xeb); break;
    case Kind::Stub: break;
    }
    return out;
}

// The listener stub's body: mov dword [rsp+0x20], w; mov dword [rsp+0x24], h;
// then the 5-byte jmp back the caller appends (16 bytes here).
inline std::vector<std::uint8_t> listener_stores(std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> out = {0xc7, 0x44, 0x24, 0x20, 0, 0, 0, 0, 0xc7, 0x44, 0x24, 0x24, 0, 0, 0, 0};
    std::memcpy(out.data() + 4, &w, 4);
    std::memcpy(out.data() + 12, &h, 4);
    return out;
}

// Whether a branch site's original byte is a short conditional jump.
inline bool is_short_jcc(std::uint8_t b) { return b >= 0x70 && b <= 0x7f; }

// The render size the split uses for an output of ow x oh at a ratio
// (output / render): each side divided and rounded down to a multiple of 8
// (the game halves and quarters it; bbport saw the game align its targets
// to 8), at least 64, never above the output. A ratio of 1 or less is the
// output itself - no split.
inline void render_size(double ratio, std::uint32_t ow, std::uint32_t oh, std::uint32_t* w, std::uint32_t* h) {
    if (!(ratio > 1.0) || !ow || !oh) {
        *w = ow;
        *h = oh;
        return;
    }
    const auto side = [ratio](std::uint32_t v) {
        auto s = static_cast<std::uint32_t>(static_cast<double>(v) / ratio + 0.5);
        s &= ~7u;
        if (s < 64) s = 64;
        return s > v ? v : s;
    };
    *w = side(ow);
    *h = side(oh);
}

// test2/test3: the split's Initialize site (0x26c3af3, kKeepColour) makes
// the graphics manager's scene take the output entry's DefRenderTarget for
// its colour whatever its size, and YEBIS then takes that target for its back
// buffer and builds its whole chain at its size. Kyo's patches were made for
// display buffers of the output's size; live resolution makes them the
// largest size a change can reach (5120x2160), so a 3840x2160 output built
// YEBIS at 5120x2160 (2560x1080, 3136x882 ...), read the 3840-wide picture
// across 5120 and came out dark and stretched by 4/3 with a grey band from
// x = 3840 * 3840 / 5120 = 2880 - the same picture live resolution's
// both-sides test fixed for the unsplit scene (engine/live_resolution.cpp).
// So the split is only made when the display buffers are the output's size
// on both sides (0 x 0: not known, the buffers are the starting size).
inline bool display_fits_split(std::uint32_t dw, std::uint32_t dh, std::uint32_t ow, std::uint32_t oh) {
    return (!dw && !dh) || (dw == ow && dh == oh);
}

// The display buffers' size at the start: the largest size a live change
// can reach (max_w x max_h, or the starting size when larger), unless the
// scene is split at the start, when they are the starting (output) size so
// the split's DefRenderTarget is the output's (display_fits_split). Then a
// larger Resolution applies at the next start, and another one live runs
// unsplit until the next start. `keep_max`: HELIAS_SPLIT_DISPLAY=max, as
// before.
inline void display_size_at_start(bool split_at_start, bool keep_max, std::uint32_t max_w, std::uint32_t max_h, std::uint32_t w,
                                  std::uint32_t h, std::uint32_t* dw, std::uint32_t* dh) {
    if (split_at_start && !keep_max && w && h) {
        *dw = w;
        *dh = h;
        return;
    }
    *dw = max_w > w ? max_w : w;
    *dh = max_h > h ? max_h : h;
}

// The S# words of a sampler made a plain bilinear copy: linear min and mag
// filters, no mip filtering (level 0), no anisotropy, clamp to edge on every
// axis. The rest (LOD bias, border colour, compare) stays. As Awakenchan's
// engine (built on bbport) found, a point-filtered read of a smaller scene
// is a nearest-neighbour upscale (blocky effects and edges); this is the
// bilinear one.
inline void bilinear_copy_sampler(std::uint32_t s[4]) {
    s[0] &= ~0xfffu;              // CLAMP_X/Y/Z (bits 0-8) and MAX_ANISO_RATIO (9-11)
    s[0] |= 2u | 2u << 3 | 2u << 6;  // clamp to edge
    s[2] &= ~(3u << 20 | 3u << 22 | 3u << 26);
    s[2] |= 1u << 20 | 1u << 22;  // XY_MAG_FILTER, XY_MIN_FILTER bilinear; MIP_FILTER none
}

}  // namespace scene_split
