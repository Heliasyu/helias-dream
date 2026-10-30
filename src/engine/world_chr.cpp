#include "engine/world_chr.h"

#include "core/elf.h"
#include "core/portable.h"
#include "core/thunk.h"
#include "engine/addr.h"
#include "host/window.h"
#include "log.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kWorldChrMan = 0x593e878;  // WorldChrMan*
constexpr std::uint64_t kCameraRoot = 0x593e860;   // the camera owner's root object

std::uint64_t g_slide = 0;
bool g_ok = false;
bool g_warp_enabled = false;  // HELIAS_TEST_WARP=1, or a gameplay plugin the player turned on
bool g_unlock = false;        // HELIAS_TEST_UNLOCK_LAMPS=1

template <typename T>
bool rd(std::uint64_t at, T* out) {
    return at && host_read_safe(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), out, sizeof(T));
}

std::uint64_t ptr(std::uint64_t at) {
    std::uint64_t p = 0;
    return rd(at, &p) ? p : 0;
}

std::uint64_t slot(std::uint64_t bn) { return g_ok ? g_slide + (bn - kPreferredGuestSlide) : 0; }

bool finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// HELIAS_CHR_SCAN=<file>: a research recorder for the main player's action
// state (host/haptics.h wants to know what the character is doing, not what
// is held). Every frame: the time, the pad, then 0x1000 bytes each of the
// ChrIns and of three of its modules, found in the module container (+0x3b0)
// by their vtables (tools/class_catalog): SprjChrTimeActModule (the animation
// it plays), SprjChrActionRequestModule and SprjChrBehaviorScriptModule. A
// module not found is zeros. The container's slots, with each one's vtable,
// go to the log once. Then 0x4000 bytes of the local player's game data
// (GameDataMan +0x08, engine/player_data.h) in four more blocks - where the
// inventory's counts are, Quicksilver Bullets among them. Off unless set.
constexpr std::uint64_t kScanVtables[3] = {0x5735190, 0x5734350, 0x57350f0};
constexpr std::size_t kScanBytes = 0x1000;
struct ScanHead {
    std::uint64_t t_us;
    std::uint32_t buttons;
    std::uint8_t l2, r2;
    std::uint16_t blocks;  // 8: the ChrIns, the three modules, then the game data
};
void read_block(std::uint64_t at, std::uint8_t* out) {
    std::memset(out, 0, kScanBytes);
    if (!at) return;
    for (std::size_t o = 0; o < kScanBytes; o += 0x100) host_read_safe(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at + o)), out + o, 0x100);
}
void chr_scan() {
    static const char* path = std::getenv("HELIAS_CHR_SCAN");
    if (!path || !*path || !g_ok) return;
    static std::FILE* f = nullptr;
    static bool failed = false, listed = false;
    if (failed) return;
    if (!f) {
        f = std::fopen(path, "wb");
        if (!f) {
            failed = true;
            host_log("world: HELIAS_CHR_SCAN cannot open %s", path);
            return;
        }
        host_log("world: HELIAS_CHR_SCAN to %s (%zu bytes a frame)", path, sizeof(ScanHead) + 8 * kScanBytes);
    }
    const std::uint64_t man = ptr(slot(kWorldChrMan));
    const std::uint64_t chr = man ? ptr(man + 0x60) : 0;
    const std::uint64_t modules = chr ? ptr(chr + 0x3b0) : 0;
    if (!modules) return;
    std::uint64_t found[3] = {0, 0, 0};
    for (std::uint64_t s = 0; s < 0x200; s += 8) {
        const std::uint64_t m = ptr(modules + s);
        const std::uint64_t vt = m ? ptr(m) : 0;
        if (!listed && m) {
            host_log("world: chr scan: module slot +0x%03llx = 0x%llx, vtable 0x%llx", static_cast<unsigned long long>(s),
                     static_cast<unsigned long long>(m), static_cast<unsigned long long>(vt ? vt - g_slide + kPreferredGuestSlide : 0));
        }
        for (int k = 0; k < 3; ++k) {
            if (vt && vt == slot(kScanVtables[k]) && !found[k]) found[k] = m;
        }
    }
    listed = true;
    const PadState pad = host_pad_state();
    ScanHead h{};
    h.t_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    h.buttons = pad.buttons;
    h.l2 = pad.l2;
    h.r2 = pad.r2;
    h.blocks = 8;
    static std::uint8_t buf[8][kScanBytes];
    read_block(chr, buf[0]);
    for (int k = 0; k < 3; ++k) read_block(found[k], buf[1 + k]);
    constexpr std::uint64_t kGameDataMan = 0x593b130;
    const std::uint64_t gdm = ptr(slot(kGameDataMan));
    const std::uint64_t record = gdm ? ptr(gdm + 0x08) : 0;
    for (int k = 0; k < 4; ++k) read_block(record ? record + static_cast<std::uint64_t>(k) * kScanBytes : 0, buf[4 + k]);
    std::fwrite(&h, sizeof(h), 1, f);
    std::fwrite(buf, sizeof(buf), 1, f);
}

}  // namespace

bool world_player_anim(std::int32_t* anim) {
    // The module is looked up in the container by its vtable once per
    // character, then only its vtable checked again on each read.
    constexpr std::uint64_t kTimeActVtable = 0x5735190;
    static std::uint64_t cached_chr = 0, cached_module = 0;
    const std::uint64_t man = ptr(slot(kWorldChrMan));
    const std::uint64_t chr = man ? ptr(man + 0x60) : 0;
    if (!chr) return false;
    const std::uint64_t want = slot(kTimeActVtable);
    if (chr != cached_chr || !cached_module || ptr(cached_module) != want) {
        cached_chr = chr;
        cached_module = 0;
        const std::uint64_t modules = ptr(chr + 0x3b0);
        for (std::uint64_t s = 0; modules && s < 0x200 && !cached_module; s += 8) {
            const std::uint64_t m = ptr(modules + s);
            if (m && ptr(m) == want) cached_module = m;
        }
        if (!cached_module) return false;
    }
    std::int32_t v = 0;
    if (!rd(cached_module + 0x20, &v)) return false;
    if (anim) *anim = v;
    return true;
}

namespace {

// HELIAS_FIND_BULLETS=1: a research search for the Quicksilver Bullets count
// (host/haptics.h wants L2 slack the moment the gun is empty). Around the
// local player's game data (GameDataMan +0x08, +-128 MiB), the int32s that
// went down by exactly one 0.6 s after a shot's animation (117051000) began,
// kept while every later shot takes one more off and Blood Bullets (100141)
// add some; an empty pull (117054500) leaves them alone. The survivors - and
// the pointers in the game data that lead near them - go to the log.
struct BulletSearch {
    bool on = false;
    std::uint64_t lo = 0, hi = 0;           // the window searched
    std::vector<std::int32_t> before;      // the window as the first shot began
    std::vector<std::uint64_t> where;      // the candidates
    std::vector<std::int32_t> last;
    std::vector<std::array<std::int32_t, 16>> full;  // each one's neighbours before the first shot, the slot still full
    int pending = 0;                       // the animation whose result is due
    std::chrono::steady_clock::time_point due{};
    std::int32_t prev_anim = -1;
} g_bullets;

std::uint64_t game_record() {
    constexpr std::uint64_t kGameDataMan = 0x593b130;
    const std::uint64_t gdm = ptr(slot(kGameDataMan));
    return gdm ? ptr(gdm + 0x08) : 0;
}

void read_window(std::uint64_t lo, std::uint64_t hi, std::vector<std::int32_t>* out) {
    out->assign((hi - lo) / 4, 0);
    constexpr std::uint64_t kChunk = 1 << 16;
    for (std::uint64_t a = lo; a < hi; a += kChunk) {
        host_read_safe(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a)), out->data() + (a - lo) / 4,
                       static_cast<std::size_t>(std::min(kChunk, hi - a)));
    }
}

void bullets_report(const char* after) {
    BulletSearch& b = g_bullets;
    host_log("world: bullets search: %zu candidate(s) after %s", b.where.size(), after);
    if (b.where.empty() || b.where.size() > 8) return;
    const std::uint64_t record = game_record();
    for (std::size_t i = 0; i < b.where.size(); ++i) {
        host_log("world: bullets search:   0x%llx = %d (record %s0x%llx)", static_cast<unsigned long long>(b.where[i]), b.last[i],
                 b.where[i] >= record ? "+" : "-",
                 static_cast<unsigned long long>(b.where[i] >= record ? b.where[i] - record : record - b.where[i]));
        // The slot's neighbours: an inventory entry's other fields (the item's
        // id, round 14: the count moved to another slot when Blood Bullets
        // refilled an emptied one).
        std::int32_t around[16] = {};
        for (int k = 0; k < 16; ++k) rd(b.where[i] - 0x20 + static_cast<std::uint64_t>(k) * 4, &around[k]);
        host_log("world: bullets search:     -0x20..+0x1c: %d %d %d %d %d %d %d %d | %d %d %d %d %d %d %d %d", around[0], around[1],
                 around[2], around[3], around[4], around[5], around[6], around[7], around[8], around[9], around[10], around[11],
                 around[12], around[13], around[14], around[15]);
        if (i < b.full.size()) {
            const auto& f = b.full[i];
            host_log("world: bullets search:     before the first shot: %d %d %d %d %d %d %d %d | %d %d %d %d %d %d %d %d (hex %08x %08x %08x %08x %08x %08x %08x %08x | %08x %08x %08x %08x)",
                     f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15],
                     static_cast<unsigned>(f[0]), static_cast<unsigned>(f[1]), static_cast<unsigned>(f[2]), static_cast<unsigned>(f[3]),
                     static_cast<unsigned>(f[4]), static_cast<unsigned>(f[5]), static_cast<unsigned>(f[6]), static_cast<unsigned>(f[7]),
                     static_cast<unsigned>(f[8]), static_cast<unsigned>(f[9]), static_cast<unsigned>(f[10]), static_cast<unsigned>(f[11]));
        }
        // A pointer in the game data (or one level below it) to just before
        // the count: the path to keep.
        for (std::uint64_t o = 0; record && o < 0x4000; o += 8) {
            const std::uint64_t p = ptr(record + o);
            if (p && p <= b.where[i] && b.where[i] - p < 0x20000) {
                host_log("world: bullets search:     record+0x%llx -> +0x%llx", static_cast<unsigned long long>(o),
                         static_cast<unsigned long long>(b.where[i] - p));
            }
            for (std::uint64_t o2 = 0; p && o2 < 0x400; o2 += 8) {
                const std::uint64_t q = ptr(p + o2);
                if (q && q <= b.where[i] && b.where[i] - q < 0x8000) {
                    host_log("world: bullets search:     [record+0x%llx]+0x%llx -> +0x%llx", static_cast<unsigned long long>(o),
                             static_cast<unsigned long long>(o2), static_cast<unsigned long long>(b.where[i] - q));
                }
            }
        }
    }
}

void bullets_tick() {
    BulletSearch& b = g_bullets;
    static const bool want = [] {
        const char* e = std::getenv("HELIAS_FIND_BULLETS");
        return e && e[0] == '1';
    }();
    if (!want || !g_ok) return;
    std::int32_t anim = -1;
    if (!world_player_anim(&anim)) return;
    const auto now = std::chrono::steady_clock::now();
    if (anim != b.prev_anim) {
        b.prev_anim = anim;
        if ((anim == 117051000 || anim == 117054500 || anim == 100141) && !b.pending) {
            if (anim == 117051000 && b.where.empty() && b.before.empty()) {
                const std::uint64_t record = game_record();
                if (!record) return;
                b.lo = (record > (128ull << 20) ? record - (128ull << 20) : 0) & ~0xfffull;
                b.hi = record + (128ull << 20);
                read_window(b.lo, b.hi, &b.before);
                host_log("world: bullets search: the first shot - 0x%llx..0x%llx taken", static_cast<unsigned long long>(b.lo),
                         static_cast<unsigned long long>(b.hi));
            }
            if (!b.before.empty() || !b.where.empty()) {
                b.pending = anim;
                b.due = now + std::chrono::milliseconds(anim == 100141 ? 1500 : 600);
            }
        }
    }
    if (!b.pending || now < b.due) return;
    const int what = b.pending;
    b.pending = 0;
    if (!b.before.empty()) {
        std::vector<std::int32_t> after;
        read_window(b.lo, b.hi, &after);
        for (std::size_t i = 0; i < after.size(); ++i) {
            if (b.before[i] >= 1 && b.before[i] <= 30 && after[i] == b.before[i] - 1) {
                b.where.push_back(b.lo + i * 4);
                b.last.push_back(after[i]);
                std::array<std::int32_t, 16> f{};
                for (int k = 0; k < 16; ++k) {
                    const std::size_t j = i + static_cast<std::size_t>(k) - 8;
                    if (i + k >= 8 && j < b.before.size()) f[k] = b.before[j];
                }
                b.full.push_back(f);
            }
        }
        b.before.clear();
        b.before.shrink_to_fit();
        bullets_report("the first shot");
        return;
    }
    std::size_t kept = 0;
    for (std::size_t i = 0; i < b.where.size(); ++i) {
        std::int32_t v = 0;
        if (!rd(b.where[i], &v)) continue;
        const bool ok = what == 117051000 ? v == b.last[i] - 1 : what == 117054500 ? v == b.last[i] : v > b.last[i];
        if (ok) {
            b.where[kept] = b.where[i];
            b.last[kept] = v;
            if (i < b.full.size()) b.full[kept] = b.full[i];
            ++kept;
        }
    }
    b.where.resize(kept);
    b.last.resize(kept);
    if (b.full.size() > kept) b.full.resize(kept);
    bullets_report(what == 117051000 ? "a shot" : what == 117054500 ? "an empty pull" : "Blood Bullets");
}

}  // namespace

namespace {

std::atomic<int> g_bullets_now{-1};

// The carried Quicksilver Bullets (world_player_bullets): every entry in the
// game data's first 0x40000 bytes whose two item ids are goods 900 or 901,
// summed. Round 17: Blood Bullets make 901, while the bullets taken from the
// item box were not 901 - 900 is the item's own. Every entry of goods
// 896..911 goes to the log whenever the list changes, so another id (or a
// second entry, the item box's) would show.
void bullets_read() {
    static auto next = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now < next) return;
    next = now + std::chrono::milliseconds(100);
    const std::uint64_t record = game_record();
    std::int32_t anim = 0;
    if (!record || !world_player_anim(&anim)) {
        g_bullets_now = -1;
        return;
    }
    constexpr std::uint64_t kSpan = 0x40000;
    constexpr std::uint32_t kGoodsLo = 0x40000380, kGoodsHi = 0x4000038f;
    static std::vector<std::uint32_t> words(kSpan / 4);
    for (std::uint64_t o = 0; o < kSpan; o += 0x1000) {
        if (!host_read_safe(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(record + o)), words.data() + o / 4, 0x1000)) {
            std::fill(words.begin() + static_cast<std::ptrdiff_t>(o / 4), words.begin() + static_cast<std::ptrdiff_t>((o + 0x1000) / 4), 0u);
        }
    }
    int total = 0;
    std::string where;
    for (std::size_t i = 1; i + 1 < words.size(); ++i) {
        if (words[i] < kGoodsLo || words[i] > kGoodsHi || words[i - 1] != (0xb0000000u | (words[i] & 0x0fffffffu))) continue;
        const auto count = static_cast<std::int32_t>(words[i + 1]);
        const unsigned id = words[i] & 0x0fffffffu;
        if (count > 0 && (id == 900 || id == 901)) total += count;
        char buf[64];
        std::snprintf(buf, sizeof(buf), " goods %u at +0x%zx=%d", id, (i + 1) * 4, count);
        where += buf;
    }
    static std::string said = "?";
    if (where != said) {
        said = where;
        host_log("world: Quicksilver Bullets entries:%s (%d carried)", where.empty() ? " none" : where.c_str(), total);
    }
    g_bullets_now = total;
}

}  // namespace

int world_player_bullets() { return g_bullets_now.load(std::memory_order_relaxed); }

void world_chr_install(ElfImage* image) {
    g_slide = image->mem.slide;
    g_ok = image->sha256 == kEboot109Sha256;
    if (const char* e = std::getenv("HELIAS_TEST_WARP"); e && e[0] == '1') {
        g_warp_enabled = true;
        host_log("world: HELIAS_TEST_WARP=1 - plugins may move the player (a test switch)");
    }
    if (const char* e = std::getenv("HELIAS_TEST_UNLOCK_LAMPS"); g_ok && e && e[0] == '1') {
        g_unlock = true;
        host_log("world: HELIAS_TEST_UNLOCK_LAMPS=1 - every lamp is lit at each load (a test switch)");
    }
}

void world_warp_allow() {
    if (!g_warp_enabled) host_log("world: warps on for a gameplay plugin the player turned on");
    g_warp_enabled = true;
}

bool world_player_position(float xyz[3], float* yaw) {
    const std::uint64_t man = ptr(slot(kWorldChrMan));
    const std::uint64_t chr = man ? ptr(man + 0x60) : 0;
    const std::uint64_t modules = chr ? ptr(chr + 0x3b0) : 0;
    const std::uint64_t transform = modules ? ptr(modules + 0x68) : 0;
    float v[3] = {}, y = 0;
    if (!transform || !rd(transform + 0x1e0, &v) || !rd(transform + 0x1d4, &y) || !finite3(v)) return false;
    if (xyz) {
        xyz[0] = v[0];
        xyz[1] = v[1];
        xyz[2] = v[2];
    }
    if (yaw) *yaw = y;
    return true;
}

bool world_camera(float pos[3], float focus[3]) {
    const std::uint64_t root = ptr(slot(kCameraRoot));
    const std::uint64_t owner = root ? ptr(root + 0x2830) : 0;
    const std::uint64_t cam = owner ? ptr(owner + 0x60) : 0;
    float p[3] = {}, f[3] = {};
    if (!cam || !rd(cam + 0x40, &p) || !rd(cam + 0xd0, &f) || !finite3(p) || !finite3(f)) return false;
    if (f[0] == 0 && f[1] == 0 && f[2] == 0) return false;  // between areas the camera is reset
    for (int i = 0; i < 3; ++i) {
        if (pos) pos[i] = p[i];
        if (focus) focus[i] = f[i];
    }
    return true;
}

bool world_player_block(std::uint32_t* block) {
    const std::uint64_t man = ptr(slot(kWorldChrMan));
    const std::uint64_t chr = man ? ptr(man + 0x60) : 0;
    std::uint32_t b = 0;
    if (!chr || !rd(chr + 0x3f8, &b) || b == 0xffffffffu) return false;
    if (block) *block = b;
    return true;
}

namespace {
constexpr std::uint64_t kWorldRes = 0x593b148;          // the world manager the warps are called on (its +8 holds WorldRes)
constexpr std::uint64_t kPreparePlayerWarp = 0x194b110;  // (WorldRes*, const int* block, const vec4* pos, const vec4* rot)
std::mutex g_warp_mu;
bool g_warp_pending = false;  // under g_warp_mu
float g_warp_xyz[3] = {}, g_warp_yaw = 0;
std::uint32_t g_warp_block = 0;
}  // namespace

bool world_player_warp_queue(std::uint32_t block, const float xyz[3], float yaw) {
    if (!g_ok || !g_warp_enabled || !xyz || !std::isfinite(yaw)) return false;
    const float v[3] = {xyz[0], xyz[1], xyz[2]};
    if (!finite3(v) || !world_player_block(nullptr)) return false;
    std::lock_guard<std::mutex> lk(g_warp_mu);
    g_warp_pending = true;
    g_warp_xyz[0] = v[0];
    g_warp_xyz[1] = v[1];
    g_warp_xyz[2] = v[2];
    g_warp_yaw = yaw;
    g_warp_block = block;
    return true;
}

namespace {

// The lamps. Each map's event script starts
// event 7000 once per lamp with (warp object entity, lamp entity, the flag
// that must be on before it can be lit - 999 for none, flag base); the lamp
// DB (FrpgNetMan +0xc70, heliasdream/engine/frpg/bonfire_db.hpp) registers the base,
// and base+10 is the lamp's lit flag, base+11 "selected" (the last one
// rested at in that map). Extracted from dvdroot_ps4/event/m*.emevd.dcx by
// tools/mapval_lamps.py; the chalice dungeons (m29) have none there.
// Map loads re-register their own lamps and clear those bits again, which
// is why the unlock runs at every load.
struct Lamp {
    std::int32_t entity, flag_base;
};
constexpr Lamp kLamps[] = {
    {2111950, 12117800},                                                                // m21_01 Abandoned Old Workshop
    {2201950, 12207800}, {2201951, 12207820},                                           // m22 Hemwick
    {2301950, 12307800}, {2301951, 12307820}, {2301952, 12307840},                      // m23 Old Yharnam
    {2401950, 12407800}, {2401951, 12407820},                                           // m24_00 Cathedral Ward
    {2411950, 12417800}, {2411951, 12417820}, {2411952, 12417840}, {2411953, 12417860},  // m24_01 Central Yharnam
    {2421950, 12427800}, {2421951, 12427820}, {2421952, 12427840},                      // m24_02 Upper Cathedral Ward
    {2501950, 12507800}, {2501951, 12507820}, {2501952, 12507840},                      // m25 Cainhurst
    {2601950, 12607800}, {2601951, 12607820}, {2601952, 12607840}, {2601953, 12607860},  // m26 Mensis
    {2701950, 12707800}, {2701951, 12707820},                                           // m27 Forbidden Woods
    {2801950, 12807800}, {2801951, 12807820}, {2801952, 12807840}, {2801953, 12807860},  // m28 Yahar'gul
    {3201950, 13207800}, {3201951, 13207820}, {3201952, 13207840}, {3201953, 13207860},  // m32 Byrgenwerth
    {3301950, 13307800}, {3301951, 13307820},                                           // m33 Nightmare Frontier
    {3401950, 13407800}, {3401951, 13407820}, {3401952, 13407840}, {3401953, 13407860},  // m34 Hunter's Nightmare
    {3501950, 13507800}, {3501951, 13507820}, {3501952, 13507840},                      // m35 Research Hall
    {3601950, 13607800}, {3601951, 13607820}, {3601952, 13607840},                      // m36 Fishing Hamlet
};
constexpr std::uint64_t kEventFlagMan = 0x593b100;       // SprjEventFlagMan*
constexpr std::uint64_t kSetEventFlag = 0x17cfcc0;       // (man, id, on)
constexpr std::uint64_t kIsEventFlag = 0x17cfc00;        // (man, id, int* exists) -> on
constexpr std::uint64_t kWarpNextStageBonfire = 0x172e050;  // (lua state, unused; return point entity id)
constexpr std::uint64_t kSetReturnPointEntity = 0x196d110;  // GameStateMan: ReturnPointParam row -> packed return point (+0x1528)
constexpr std::uint64_t kSetUseReturnPoint = 0x196d0e0;     // GameStateMan +0x1524: the next load places the player there
std::uint32_t g_unlock_block = 0;  // the block the last unlock ran in (one per load)
std::int32_t g_lamp_warp = 0;      // under g_warp_mu

void unlock_lamps(std::uint32_t block) {
    const std::uint64_t man = ptr(slot(kEventFlagMan));
    if (!man) return;
    int set = 0, already = 0, missing = 0;
    for (const Lamp& l : kLamps) {
        const std::uint32_t lit = static_cast<std::uint32_t>(l.flag_base + 10);
        std::int32_t exists = 0;
        const auto on = hle_call_guest<std::int64_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot(kIsEventFlag))), man, lit, &exists);
        if (!exists) {
            ++missing;
            continue;
        }
        if (on) {
            ++already;
            continue;
        }
        hle_call_guest<std::int64_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot(kSetEventFlag))), man, lit, 1);
        ++set;
    }
    host_log("world: HELIAS_TEST_UNLOCK_LAMPS in block 0x%08x: %d lamp lit flags set, %d were set, %d have no flag block in this save", block,
             set, already, missing);
}

}  // namespace

bool world_lamp_warp_queue(std::int32_t return_point) {
    if (!g_ok || !g_warp_enabled || return_point <= 0 || !world_player_block(nullptr)) return false;
    std::lock_guard<std::mutex> lk(g_warp_mu);
    g_lamp_warp = return_point;
    return true;
}

void world_chr_tick() {
    chr_scan();
    bullets_tick();
    if (g_ok) bullets_read();
    if (g_unlock) {
        std::uint32_t b = 0;
        if (world_player_block(&b) && b != g_unlock_block) {
            g_unlock_block = b;
            unlock_lamps(b);
        }
    }
    std::int32_t lamp = 0;
    {
        std::lock_guard<std::mutex> lk(g_warp_mu);
        lamp = g_lamp_warp;
        g_lamp_warp = 0;
    }
    if (lamp) {
        std::uint32_t from = 0;
        world_player_block(&from);
        // The lamp as the return point, as resting at it makes it, and the
        // load told to use it; then the stage change.
        hle_call_guest<std::int64_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot(kSetReturnPointEntity))), lamp);
        hle_call_guest<std::int64_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot(kSetUseReturnPoint))), 1);
        hle_call_guest<std::int64_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot(kWarpNextStageBonfire))), 0, lamp);
        host_log("world: lamp travel from block 0x%08x to return point %d (WarpNextStage_Bonfire)", from, lamp);
    }
    float xyz[3], yaw;
    std::uint32_t want = 0;
    {
        std::lock_guard<std::mutex> lk(g_warp_mu);
        if (!g_warp_pending) return;
        g_warp_pending = false;
        xyz[0] = g_warp_xyz[0];
        xyz[1] = g_warp_xyz[1];
        xyz[2] = g_warp_xyz[2];
        yaw = g_warp_yaw;
        want = g_warp_block;
    }
    // Block 0 is the player's own: the in-map path. Another block goes
    // through the same routine, which rebinds the player to it - only for a
    // block that is resident (a neighbour the world streams in).
    std::uint32_t block = 0, from = 0;
    const std::uint64_t res = ptr(slot(kWorldRes));
    if (!res || !world_player_block(&from)) {
        host_log("world: warp dropped (no player or no WorldRes)");
        return;
    }
    block = want ? want : from;
    alignas(16) std::int32_t blk[4] = {static_cast<std::int32_t>(block), 0, 0, 0};
    alignas(16) float pos[4] = {xyz[0], xyz[1], xyz[2], 0.0f};
    alignas(16) float rot[4] = {0.0f, yaw, 0.0f, 0.0f};  // pitch, yaw, roll (radians), as the transform keeps them at +0x1d0
    hle_call_guest<std::int64_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot(kPreparePlayerWarp))), res, blk, pos, rot);
    std::uint32_t now = 0;
    world_player_block(&now);
    host_log("world: warp from block 0x%08x into 0x%08x at %.3f %.3f %.3f yaw %.3f; now in 0x%08x", from, block, xyz[0], xyz[1], xyz[2],
             yaw, now);
}
