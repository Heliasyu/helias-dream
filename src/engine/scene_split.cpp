// heliasdream-vk Phase 1: the scene/UI resolution split (engine/scene_split.h).
#include "engine/scene_split.h"

#include "core/elf.h"
#include "core/memory.h"
#include "engine/addr.h"
#include "engine/graphics_patch.h"
#include "engine/live_resolution.h"
#include "engine/scene_split_sites.h"
#include "host/settings.h"
#include "host/upscaler_presets.h"
#include "log.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

ElfImage* g_image = nullptr;
std::uint64_t g_slide = 0;
bool g_available = false;
// Each site's bytes at load (after heliasdream's own patches), in the table's order.
std::vector<std::vector<std::uint8_t>> g_original;
std::uint8_t* g_listener_stub = nullptr;  // near the listener: the two stores, then jmp back
std::uint8_t* g_size_stub = nullptr;      // near the size read: the output entry's size, ret
std::atomic<std::uint64_t> g_render{0};   // w << 32 | h the sites hold; 0 for the game's bytes
std::atomic<bool> g_active{false};

std::uint64_t guest(std::uint64_t bn) { return g_slide + (bn - kPreferredGuestSlide); }
std::uint8_t* at_(std::uint64_t bn) { return static_cast<std::uint8_t*>(guest_ptr(g_image->mem, guest(bn))); }

bool code_write(std::uint64_t bn, const std::uint8_t* bytes, std::size_t n) {
    const std::uint64_t at = guest(bn), lo = at & ~0xfffull, hi = (at + n + 0xfff) & ~0xfffull;
    if (std::memcmp(at_(bn), bytes, n) == 0) return true;
    if (!guest_protect_rwx(&g_image->mem, lo, hi - lo)) return false;
    std::memcpy(at_(bn), bytes, n);
    guest_protect_rx(&g_image->mem, lo, hi - lo);  // text and read-only data share the segment
    return true;
}

std::string hex(const std::uint8_t* p, std::size_t n) {
    std::string s;
    char b[4];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", p[i]);
        s += b;
    }
    return s;
}

std::int32_t rel32_to(const void* from_end, std::uint64_t to) {
    return static_cast<std::int32_t>(static_cast<std::int64_t>(to) - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(from_end)));
}
bool in_rel32(std::uint64_t from_end, std::uint64_t to) {
    const std::int64_t d = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from_end);
    return d > INT32_MIN + 0x1000ll && d < INT32_MAX - 0x1000ll;
}

// The listener stub: mov dword [rsp+0x20], w; mov dword [rsp+0x24], h; jmp 0x269c7e2.
void write_listener_stub(std::uint32_t w, std::uint32_t h) {
    const std::vector<std::uint8_t> stores = scene_split::listener_stores(w, h);
    std::memcpy(g_listener_stub, stores.data(), stores.size());
    g_listener_stub[16] = 0xe9;
    const std::int32_t back = rel32_to(g_listener_stub + 21, guest(scene_split::kListenerBack));
    std::memcpy(g_listener_stub + 17, &back, 4);
}

// The size stub (Kyo's cave at 0x4d3baf0, here on the host): movabs rax, the
// GX device's slot; mov rax, [rax]; mov rax, [rax+0x90]; mov ecx, [rax+0x84];
// mov eax, [rax+0x80]; ret.
void write_size_stub() {
    std::uint8_t* c = g_size_stub;
    *c++ = 0x48, *c++ = 0xb8;
    const std::uint64_t slot = guest(scene_split::kGxDeviceSlot);
    std::memcpy(c, &slot, 8);
    c += 8;
    static const std::uint8_t rest[] = {0x48, 0x8b, 0x00, 0x48, 0x8b, 0x80, 0x90, 0x00, 0x00, 0x00, 0x8b, 0x88,
                                        0x84, 0x00, 0x00, 0x00, 0x8b, 0x80, 0x80, 0x00, 0x00, 0x00, 0xc3};
    std::memcpy(c, rest, sizeof(rest));
}

// A stub site's bytes: jmp (the listener) or call (the size read) to its stub, nops after.
std::vector<std::uint8_t> stub_site_bytes(const scene_split::Site& s) {
    std::vector<std::uint8_t> out(s.n, 0x90);
    const bool listener = s.at == scene_split::kListenerSite;
    out[0] = listener ? 0xe9 : 0xe8;
    const std::uint64_t stub = reinterpret_cast<std::uintptr_t>(listener ? g_listener_stub : g_size_stub);
    const std::int32_t d = static_cast<std::int32_t>(static_cast<std::int64_t>(stub) - static_cast<std::int64_t>(guest(s.at) + 5));
    std::memcpy(out.data() + 1, &d, 4);
    if (!listener) {
        static const std::uint8_t nop5[] = {0x0f, 0x1f, 0x44, 0x00, 0x00};  // as Kyo's patch
        std::memcpy(out.data() + 5, nop5, 5);
    }
    return out;
}

}  // namespace

bool scene_split_install(ElfImage* image) {
    g_image = image;
    g_slide = image->mem.slide;
    if (const char* e = std::getenv("HELIAS_SCENE_SPLIT"); e && e[0] == '0') {
        host_log("scene-split: off (HELIAS_SCENE_SPLIT=0); the scene renders at the output size");
        return false;
    }
    if (image->sha256 != kEboot109Sha256) {
        host_log("scene-split: off - the sites are 1.09's and this eboot is not 1.09");
        return false;
    }
    const auto& sites = scene_split::sites();
    g_original.clear();
    for (const scene_split::Site& s : sites) g_original.emplace_back(at_(s.at), at_(s.at) + s.n);
    // What can be checked without a dump: the branch sites are short
    // conditional jumps, and Initialize's colour test is the game's `je` or
    // heliasdream's both-sides `jne` (live_resolution.cpp). The rest is the 1.09
    // eboot's (checked by its SHA-256 at start), where Kyo's patches were
    // made. Each site's bytes are logged, so a test session's log has them.
    bool ok = true;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const scene_split::Site& s = sites[i];
        const std::vector<std::uint8_t>& b = g_original[i];
        bool site_ok = true;
        if (s.kind == scene_split::Kind::Branch) site_ok = scene_split::is_short_jcc(b[0]);
        if (s.at == 0x26c3af3) site_ok = (b[0] == 0x74 && b[1] == 0x36) || (b[0] == 0x75 && b[1] == 0x11);
        host_log("scene-split: site 0x%llx (%u bytes) %s: %s%s", static_cast<unsigned long long>(s.at), s.n, s.what, hex(b.data(), b.size()).c_str(),
                 site_ok ? "" : "  <- not as expected");
        ok &= site_ok;
    }
    if (!ok) {
        host_log("scene-split: off - a site is not as expected; the scene renders at the output size");
        return false;
    }
    g_listener_stub = static_cast<std::uint8_t*>(engine_alloc_exec_near(guest(scene_split::kListenerSite)));
    g_size_stub = static_cast<std::uint8_t*>(engine_alloc_exec_near(guest(scene_split::kOutputSizeCallSite)));
    if (!g_listener_stub || !g_size_stub || !in_rel32(reinterpret_cast<std::uintptr_t>(g_listener_stub) + 21, guest(scene_split::kListenerBack))) {
        host_log("scene-split: off - no stub page within reach of the game's code");
        return false;
    }
    write_listener_stub(1920, 1080);
    write_size_stub();
    g_available = true;
    host_log("scene-split: available (%zu sites, Kyo's Decoupled UI patches as a table); the render size follows the upscaler preset",
             sites.size());
    return true;
}

bool scene_split_available() { return g_available; }

namespace {
// Native TAA (upscaler::kTaa): the split at the output's own size, so the
// scale pass is FSR 3.1's, an anti-aliasing pass with nothing to upscale.
// Set by scene_split_wanted when the split can be made, read by _apply: the
// sizes alone cannot say a split is wanted when they are equal.
std::atomic<bool> g_force_wanted{false};
// The output scene_split_wanted last saw (scene_split_live).
std::atomic<std::uint64_t> g_last_output{0};
bool settings_force() { return host_settings().upscaler == upscaler::kTaa; }

// The render size the settings ask for, whether the split can be made or not.
void settings_render_size(std::uint32_t ow, std::uint32_t oh, std::uint32_t* rw, std::uint32_t* rh) {
    *rw = ow;
    *rh = oh;
    const HostSettings s = host_settings();
    if (s.upscaler == upscaler::kOff || s.upscaler == upscaler::kTaa || s.upscale_quality == upscaler::kNativeAa) return;
    scene_split::render_size(upscaler::preset_ratio(s.upscale_quality), ow, oh, rw, rh);
}
}  // namespace

bool scene_split_forced() { return g_force_wanted.load(std::memory_order_relaxed); }

bool scene_split_live() {
    if (!g_available) return false;
    const std::uint64_t o = g_last_output.load(std::memory_order_relaxed);
    if (!o) return true;  // nothing drawn yet: the start sizes the display buffers for the settings
    unsigned dw = 0, dh = 0;
    live_resolution_display(&dw, &dh);
    return scene_split::display_fits_split(dw, dh, static_cast<std::uint32_t>(o >> 32), static_cast<std::uint32_t>(o));
}

bool scene_split_planned(const ElfImage* image, std::uint32_t ow, std::uint32_t oh) {
    if (const char* e = std::getenv("HELIAS_SCENE_SPLIT"); e && e[0] == '0') return false;
    if (!image || image->sha256 != kEboot109Sha256) return false;
    std::uint32_t rw = 0, rh = 0;
    settings_render_size(ow, oh, &rw, &rh);
    return rw != ow || rh != oh || settings_force();
}

void scene_split_wanted(std::uint32_t ow, std::uint32_t oh, std::uint32_t* rw, std::uint32_t* rh) {
    *rw = ow;
    *rh = oh;
    g_force_wanted.store(false);
    g_last_output.store(static_cast<std::uint64_t>(ow) << 32 | oh, std::memory_order_relaxed);
    if (!g_available) return;
    std::uint32_t w = ow, h = oh;
    settings_render_size(ow, oh, &w, &h);
    const bool force = settings_force();
    if (w == ow && h == oh && !force) return;
    // test3: only into display buffers of the output's size
    // (scene_split::display_fits_split - test2's dark, stretched picture).
    unsigned dw = 0, dh = 0;
    live_resolution_display(&dw, &dh);
    if (!scene_split::display_fits_split(dw, dh, ow, oh)) {
        static std::uint64_t said = 0;
        const std::uint64_t key = static_cast<std::uint64_t>(ow) << 32 | oh;
        if (said != key) {
            said = key;
            host_log("scene-split: not split at %ux%u - the display buffers are %ux%u, and the split needs them at the output's size (YEBIS "
                     "builds its chain at theirs: a dark, stretched picture with a grey band); the scene renders at %ux%u, and the "
                     "upscaler applies from the next start at this Resolution",
                     ow, oh, dw, dh, ow, oh);
        }
        return;
    }
    *rw = w;
    *rh = h;
    g_force_wanted.store(force && w == ow && h == oh);
}

bool scene_split_apply(std::uint32_t rw, std::uint32_t rh, std::uint32_t ow, std::uint32_t oh) {
    if (!g_available) return rw == ow && rh == oh;
    const bool split = rw != ow || rh != oh || g_force_wanted.load();
    const auto& sites = scene_split::sites();
    if (split) write_listener_stub(rw, rh);
    bool ok = true;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const scene_split::Site& s = sites[i];
        std::vector<std::uint8_t> b;
        if (!split)
            b = g_original[i];
        else if (s.kind == scene_split::Kind::Stub)
            b = stub_site_bytes(s);
        else
            b = scene_split::encode(s, rw, rh);
        ok &= code_write(s.at, b.data(), b.size());
    }
    if (!ok) {
        // A protection change failed: put every site back as it was.
        for (std::size_t i = 0; i < sites.size(); ++i) code_write(sites[i].at, g_original[i].data(), g_original[i].size());
        host_log("scene-split: a site could not be written; the game's bytes are back (the scene at the output size)");
        g_render.store(0);
        g_active.store(false);
        return false;
    }
    const std::uint64_t was = g_render.exchange(split ? static_cast<std::uint64_t>(rw) << 32 | rh : 0);
    const bool active_was = g_active.exchange(split);
    if (was != g_render.load() || active_was != split)
        host_log("scene-split: render size %ux%u for an output of %ux%u (%s)", rw, rh, ow, oh,
                 split ? "scene targets at the render size; post-processing, UI and display at the output; the scale pass bilinear"
                       : "not split - the game's own bytes at every site");
    return true;
}

bool scene_split_active() { return g_active.load(std::memory_order_relaxed); }

void scene_split_render_size(std::uint32_t* w, std::uint32_t* h) {
    const std::uint64_t r = g_render.load(std::memory_order_relaxed);
    *w = static_cast<std::uint32_t>(r >> 32);
    *h = static_cast<std::uint32_t>(r);
}
