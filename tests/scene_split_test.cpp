// heliasdream-vk Phase 1: the scene/UI split's site table (engine/scene_split_sites.h)
// against Kyo's "Decoupled 1080p UI" patches for 1.09, byte for byte: the
// table with a size as its parameter has to write what each of those
// patches writes at that size. The two places where the community patches
// jump to a code cave in the eboot are stubs on the host here; for those the
// cave's own bytes are checked against what the stubs do (the stores, the
// jump back). Also the split's render-size rounding and the bilinear S#.
#include "engine/scene_split_sites.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

namespace {

struct Line {
    std::uint64_t at;
    const char* hex;
};
struct Patch {
    std::uint32_t w, h;
    std::vector<Line> lines;
};

// shadps4-emu/ps4_cheats PATCHES/Bloodborne.xml, "Decoupled 1080p UI (<w>x<h>
// render res)" by Kyo, AppVer 01.09 - copied verbatim.
const Patch kKyo[] = {
    {640, 360, {
        {0x02697aea, "B98002000090"},
        {0x02697af0, "BA6801000090"},
        {0x026f8ce1, "B98002000090"},
        {0x026f8ce7, "BE6801000090"},
        {0x026c3af3, "EB"},
        {0x0269c7d5, "E9FAF269029090909090909090"},
        {0x04d3bad4, "C744242080020000C744242468010000E9F90C96FD"},
        {0x026c65f6, "EB"},
        {0x026c6624, "BA800200009090"},
        {0x026c662e, "B8680100009090"},
        {0x026c6735, "EB"},
        {0x026c6760, "BA800200009090"},
        {0x026c676a, "B8680100009090"},
        {0x026f93d8, "BB800200009090"},
        {0x026f93df, "BE680100009090"},
        {0x026f99db, "BE800200009090"},
        {0x026f99e2, "B9680100009090"},
        {0x025d818d, "488B0534853603488B80900000008B8880000000448B8084000000C5FA1005E415750231F631D2C5F057C96690"},
        {0x0125b1a3, "8B03D1E8890424"},
        {0x0125b1aa, "8B4304D1E889442404"},
        {0x0125b1d6, "8B03C1E802890424"},
        {0x0125b1de, "8B4304C1E80289442404"},
        {0x026c3de4, "418B07D1E8"},
        {0x026c3df0, "418B4704D1E8"},
        {0x04d3baf0, "488B05D14BC000488B80900000008B88840000008B8080000000C3"},
        {0x01258f2c, "E8BF2BAE030F1F440000"},
    }},
    {1280, 720, {
        {0x02697aea, "B90005000090"},
        {0x02697af0, "BAD002000090"},
        {0x026f8ce1, "B90005000090"},
        {0x026f8ce7, "BED002000090"},
        {0x026c3af3, "EB"},
        {0x0269c7d5, "E9FAF269029090909090909090"},
        {0x04d3bad4, "C744242000050000C7442424D0020000E9F90C96FD"},
        {0x026c65f6, "EB"},
        {0x026c6624, "BA000500009090"},
        {0x026c662e, "B8D00200009090"},
        {0x026c6735, "EB"},
        {0x026c6760, "BA000500009090"},
        {0x026c676a, "B8D00200009090"},
        {0x026f93d8, "BB000500009090"},
        {0x026f93df, "BED00200009090"},
        {0x026f99db, "BE000500009090"},
        {0x026f99e2, "B9D00200009090"},
        {0x025d818d, "488B0534853603488B80900000008B8880000000448B8084000000C5FA1005E415750231F631D2C5F057C96690"},
        {0x0125b1a3, "8B03D1E8890424"},
        {0x0125b1aa, "8B4304D1E889442404"},
        {0x0125b1d6, "8B03C1E802890424"},
        {0x0125b1de, "8B4304C1E80289442404"},
        {0x026c3de4, "418B07D1E8"},
        {0x026c3df0, "418B4704D1E8"},
        {0x04d3baf0, "488B05D14BC000488B80900000008B88840000008B8080000000C3"},
        {0x01258f2c, "E8BF2BAE030F1F440000"},
    }},
    {1600, 900, {
        {0x02697aea, "B94006000090"},
        {0x02697af0, "BA8403000090"},
        {0x026f8ce1, "B94006000090"},
        {0x026f8ce7, "BE8403000090"},
        {0x026c3af3, "EB"},
        {0x0269c7d5, "E9FAF269029090909090909090"},
        {0x04d3bad4, "C744242040060000C744242484030000E9F90C96FD"},
        {0x026c65f6, "EB"},
        {0x026c6624, "BA400600009090"},
        {0x026c662e, "B8840300009090"},
        {0x026c6735, "EB"},
        {0x026c6760, "BA400600009090"},
        {0x026c676a, "B8840300009090"},
        {0x026f93d8, "BB400600009090"},
        {0x026f93df, "BE840300009090"},
        {0x026f99db, "BE400600009090"},
        {0x026f99e2, "B9840300009090"},
        {0x025d818d, "488B0534853603488B80900000008B8880000000448B8084000000C5FA1005E415750231F631D2C5F057C96690"},
        {0x0125b1a3, "8B03D1E8890424"},
        {0x0125b1aa, "8B4304D1E889442404"},
        {0x0125b1d6, "8B03C1E802890424"},
        {0x0125b1de, "8B4304C1E80289442404"},
        {0x026c3de4, "418B07D1E8"},
        {0x026c3df0, "418B4704D1E8"},
        {0x04d3baf0, "488B05D14BC000488B80900000008B88840000008B8080000000C3"},
        {0x01258f2c, "E8BF2BAE030F1F440000"},
    }},
    {2560, 1440, {
        {0x04b36dda, "00F7"},
        {0x04b36de2, "00A0"},
        {0x02697aea, "B9000A000090"},
        {0x02697af0, "BAA005000090"},
        {0x026f8ce1, "B9000A000090"},
        {0x026f8ce7, "BEA005000090"},
        {0x026c3af3, "EB"},
        {0x0269c7d5, "E9FAF269029090909090909090"},
        {0x04d3bad4, "C7442420000A0000C7442424A0050000E9F90C96FD"},
        {0x026c65f6, "EB"},
        {0x026c6624, "BA000A00009090"},
        {0x026c662e, "B8A00500009090"},
        {0x026c6735, "EB"},
        {0x026c6760, "BA000A00009090"},
        {0x026c676a, "B8A00500009090"},
        {0x026f93d8, "BB000A00009090"},
        {0x026f93df, "BEA00500009090"},
        {0x026f99db, "BE000A00009090"},
        {0x026f99e2, "B9A00500009090"},
        {0x025d818d, "488B0534853603488B80900000008B8880000000448B8084000000C5FA1005E415750231F631D2C5F057C96690"},
        {0x0125b1a3, "8B03D1E8890424"},
        {0x0125b1aa, "8B4304D1E889442404"},
        {0x0125b1d6, "8B03C1E802890424"},
        {0x0125b1de, "8B4304C1E80289442404"},
        {0x026c3de4, "418B07D1E8"},
        {0x026c3df0, "418B4704D1E8"},
        {0x04d3baf0, "488B05D14BC000488B80900000008B88840000008B8080000000C3"},
        {0x01258f2c, "E8BF2BAE030F1F440000"},
    }},
};

std::vector<std::uint8_t> unhex(const char* s) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; s[i] && s[i + 1]; i += 2) out.push_back(static_cast<std::uint8_t>(std::stoul(std::string(s + i, 2), nullptr, 16)));
    return out;
}

const scene_split::Site* site_at(std::uint64_t at) {
    for (const scene_split::Site& s : scene_split::sites())
        if (s.at == at) return &s;
    return nullptr;
}

std::int32_t rel32(const std::vector<std::uint8_t>& b, std::size_t off) {
    std::int32_t r = 0;
    std::memcpy(&r, b.data() + off, 4);
    return r;
}

}  // namespace

int main() {
    using namespace scene_split;
    constexpr std::uint64_t kCaveStores = 0x4d3bad4, kCaveSize = 0x4d3baf0;
    for (const Patch& p : kKyo) {
        int matched = 0;
        for (const Line& l : p.lines) {
            const std::vector<std::uint8_t> want = unhex(l.hex);
            if (l.at == kCaveStores) {
                // The listener's cave: the two stores, then jmp back.
                const std::vector<std::uint8_t> stores = listener_stores(p.w, p.h);
                CHECK(want.size() == stores.size() + 5);
                CHECK(std::memcmp(want.data(), stores.data(), stores.size()) == 0);
                CHECK(want[16] == 0xe9 && l.at + 21 + rel32(want, 17) == kListenerBack);
                ++matched;
                continue;
            }
            if (l.at == kCaveSize) {
                // The size cave: rip -> the GX device, +0x90, ecx = +0x84, eax = +0x80, ret.
                CHECK(want.size() == 27 && want[26] == 0xc3);
                CHECK(l.at + 7 + rel32(want, 3) == kGxDeviceSlot);
                ++matched;
                continue;
            }
            const Site* s = site_at(l.at);
            if (!s) {
                // The heap sizes the patches above 1080p add (0x4b36dxx) are
                // heliasdream's own business (live_resolution.cpp, grow_gfx_heap).
                CHECK(l.at >= 0x4b36d40 && l.at < 0x4b36e00);
                continue;
            }
            ++matched;
            if (s->kind == Kind::Stub) {
                CHECK(want.size() == s->n);
                if (s->at == kListenerSite) CHECK(want[0] == 0xe9 && l.at + 5 + rel32(want, 1) == kCaveStores);
                if (s->at == kOutputSizeCallSite) CHECK(want[0] == 0xe8 && l.at + 5 + rel32(want, 1) == kCaveSize);
                continue;
            }
            const std::vector<std::uint8_t> got = encode(*s, p.w, p.h);
            if (s->at == 0x26c3af3) {
                // Kyo writes the opcode only; heliasdream writes the displacement
                // too, because its both-sides patch changed it (75 11).
                CHECK(want.size() == 1 && got.size() == 2 && got[0] == want[0] && got[1] == 0x36);
                continue;
            }
            CHECK(got == want);
            if (got != want) std::fprintf(stderr, "  %ux%u at 0x%llx\n", p.w, p.h, static_cast<unsigned long long>(l.at));
        }
        // Every site of the table is in each patch, and nothing else is.
        CHECK(matched == static_cast<int>(sites().size()) + 2);
    }

    // Render sizes: multiples of 8, the output itself at 1:1.
    std::uint32_t w = 0, h = 0;
    render_size(1.5, 3840, 2160, &w, &h);
    CHECK(w == 2560 && h == 1440);
    render_size(2.0, 3840, 2160, &w, &h);
    CHECK(w == 1920 && h == 1080);
    render_size(1.7, 3840, 2160, &w, &h);  // 2259 x 1271
    CHECK(w == 2256 && h == 1264);
    render_size(3.0, 1920, 1080, &w, &h);
    CHECK(w == 640 && h == 360);
    render_size(1.0, 3840, 2160, &w, &h);
    CHECK(w == 3840 && h == 2160);
    render_size(100.0, 1280, 720, &w, &h);
    CHECK(w == 64 && h == 64);
    render_size(1.5, 3440, 1440, &w, &h);  // 2293 x 960
    CHECK(w == 2288 && h == 960);

    // test2's picture: the split only into display buffers of the output's size.
    CHECK(display_fits_split(3840, 2160, 3840, 2160));
    CHECK(!display_fits_split(5120, 2160, 3840, 2160));  // test2: 5120x2160 buffers, a 3840x2160 output
    CHECK(!display_fits_split(3840, 2160, 2560, 1440));
    CHECK(!display_fits_split(3840, 1600, 3840, 2160));
    CHECK(display_fits_split(0, 0, 2560, 1440));  // not known: the buffers are the starting size
    // The display buffers at the start: the output's when it starts split.
    std::uint32_t dw = 0, dh = 0;
    display_size_at_start(true, false, 5120, 2160, 3840, 2160, &dw, &dh);
    CHECK(dw == 3840 && dh == 2160);
    display_size_at_start(false, false, 5120, 2160, 3840, 2160, &dw, &dh);
    CHECK(dw == 5120 && dh == 2160);
    display_size_at_start(true, true, 5120, 2160, 3840, 2160, &dw, &dh);  // HELIAS_SPLIT_DISPLAY=max
    CHECK(dw == 5120 && dh == 2160);
    display_size_at_start(false, false, 1920, 1080, 2560, 1440, &dw, &dh);  // a start above the largest
    CHECK(dw == 2560 && dh == 1440);
    display_size_at_start(true, false, 5120, 2160, 3840, 2160, &dw, &dh);
    CHECK(display_fits_split(dw, dh, 3840, 2160));

    // Branch sites take short conditional jumps only.
    CHECK(is_short_jcc(0x74) && is_short_jcc(0x7f) && !is_short_jcc(0xeb) && !is_short_jcc(0x0f));

    // The bilinear S#: filters and clamps, nothing else.
    std::uint32_t s[4] = {0xffffffffu, 0x12345678u, 0xffffffffu, 0xabcdef01u};
    bilinear_copy_sampler(s);
    CHECK((s[0] & 0x1ff) == (2u | 2u << 3 | 2u << 6));
    CHECK(((s[0] >> 9) & 7) == 0);
    CHECK((s[0] >> 12) == (0xffffffffu >> 12));
    CHECK(s[1] == 0x12345678u && s[3] == 0xabcdef01u);
    CHECK(((s[2] >> 20) & 3) == 1 && ((s[2] >> 22) & 3) == 1 && ((s[2] >> 26) & 3) == 0);
    CHECK((s[2] & 0x3fff) == 0x3fff);

    if (g_fail) {
        std::fprintf(stderr, "scene_split_test: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("scene_split_test: %zu sites match Kyo's Decoupled UI patches at 4 sizes; render sizes and the bilinear S# ok\n",
                sites().size());
    return 0;
}
