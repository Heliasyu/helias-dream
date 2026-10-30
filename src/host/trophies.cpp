#include "host/trophies.h"

#include "host/haptics.h"
#include "host/overlay.h"
#include "host/settings.h"
#include "hle/fs.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#if defined(HELIAS_HAVE_SDL3)
#include <SDL3/SDL.h>
#endif

namespace {

// The table follows Lantern Engine's launcher (github.com/Awakenchan/LanternEngine,
// GPL), from pyL1nx/bloodborne_pc's trophy research; the ids are the PS4's.
constexpr TrophyInfo kTrophies[] = {
    {0, "Bloodborne", "All trophies acquired. Hats off!", 1, false},
    {1, "Yharnam Sunrise", "You lived through the hunt, and saw another day.", 2, false},
    {2, "Honoring Wishes", "Captivated by the moon presence, you pledge to watch over the hunter's dream.", 2, false},
    {3, "Childhood's Beginning", "You became an infant Great One, lifting humanity into its next childhood.", 2, false},
    {4, "Yharnam, Pthumerian Queen", "Defeat Yharnam, Blood Queen of the Old Labyrinth.", 2, false},
    {5, "Hunter's Essence", "Acquire all hunter weapons.", 2, false},
    {6, "Hunter's Craft", "Acquire all special hunter tools.", 2, false},
    {7, "Weapon Master", "Acquire a weapon of the highest level.", 3, false},
    {8, "Blood Gem Master", "Acquire an extremely precious blood gem.", 3, false},
    {9, "Rune Master", "Acquire an extremely precious Caryll Rune.", 3, false},
    {10, "Cainhurst", "Gain entry to Cainhurst, the lost and ruined castle.", 3, false},
    {11, "The Choir", "Gain entry to the realm of the Choir, the high stratum of the Healing Church.", 3, false},
    {12, "The Source of the Dream", "Discover the abandoned old workshop, the source of the hunter's dream.", 3, false},
    {13, "Nightmare Lecture Building", "Gain entry into the Byrgenwerth lecture building, that drifts within the realm of nightmare.", 3, false},
    {14, "Father Gascoigne", "Defeat the beast that once was Father Gascoigne.", 4, false},
    {15, "Vicar Amelia", "Defeat the beast that once was Vicar Amelia.", 4, false},
    {16, "Shadow of Yharnam", "Defeat the Shadow of Yharnam.", 4, false},
    {17, "Rom, the Vacuous Spider", "Defeat Great One: Rom, the Vacuous Spider.", 4, false},
    {18, "The One Reborn", "Defeat the One Reborn.", 4, false},
    {19, "Micolash, Host of the Nightmare", "Defeat Micolash, Host of the Nightmare.", 4, false},
    {20, "Mergo's Wet Nurse", "Defeat Great One: Mergo's Wet Nurse.", 4, false},
    {21, "Cleric Beast", "Defeat Cleric Beast.", 4, false},
    {22, "Blood-starved Beast", "Defeat Blood-starved Beast.", 4, false},
    {23, "The Witch of Hemwick", "Defeat the Witch of Hemwick.", 4, false},
    {24, "Darkbeast Paarl", "Defeat Darkbeast Paarl.", 4, false},
    {25, "Amygdala", "Defeat Great One: Amygdala.", 4, false},
    {26, "Martyr Logarius", "Defeat Martyr Logarius.", 4, false},
    {27, "Celestial Emissary", "Defeat Great One: Celestial Emissary.", 4, false},
    {28, "Ebrietas, Daughter of the Cosmos", "Defeat Great One: Ebrietas, Daughter of the Cosmos.", 4, false},
    {29, "Blood Gem Contact", "Acquire a blood gem that imbues hunter weapons with special strength.", 4, false},
    {30, "Rune Contact", "Acquire a Caryll Rune that endows hunters with special strength.", 4, false},
    {31, "Chalice of Pthumeru", "Acquire the Chalice of Pthumeru that seals the catacombs that form a web deep below Yharnam.", 4, false},
    {32, "Chalice of Ailing Loran", "Acquire the Chalice of Ailing Loran that seals the tragic land lost to the sands.", 4, false},
    {33, "Chalice of Isz", "Acquire the Great Chalice of Isz that seals the home of the cosmic kin.", 4, false},
    {34, "Old Hunter's Essence", "Acquire all old hunter weapons.", 2, true},
    {35, "Orphan of Kos", "Defeat Great One: Orphan of Kos.", 3, true},
    {36, "Ludwig, the Holy Blade", "Defeat the beast that was once Ludwig, the Holy Blade.", 4, true},
    {37, "Lady Maria of the Astral Clocktower", "Defeat Lady Maria of the Astral Clocktower.", 4, true},
    {38, "Living Failures", "Defeat the failed attempts to become Great Ones.", 4, true},
    {39, "Laurence, the First Vicar", "Defeat the beast that was once Laurence, the First Vicar.", 4, true},
};

struct Popup {
    int id;
    int earned, total;
};
std::mutex g_mu;
std::deque<Popup> g_queue;
bool g_showing = false;
Popup g_current{};
std::chrono::steady_clock::time_point g_since;

constexpr double kIn = 0.45, kHold = 5.5, kOut = 0.5;  // seconds

// The medal's two colours (top, bottom) by grade, as the launcher's.
void medal(int grade, std::uint32_t& top, std::uint32_t& bottom) {
    switch (grade) {
        case 1: top = 0xffffffffu; bottom = 0x8a9bbaffu; break;  // platinum
        case 2: top = 0xf8ecc8ffu; bottom = 0x8a6a36ffu; break;  // gold
        case 3: top = 0xf2f4f7ffu; bottom = 0x6d7584ffu; break;  // silver
        default: top = 0xf1d2b6ffu; bottom = 0x6e4628ffu; break;  // bronze
    }
}

const char* grade_name(int grade) {
    switch (grade) {
        case 1: return "Platinum trophy earned";
        case 2: return "Gold trophy earned";
        case 3: return "Silver trophy earned";
        default: return "Bronze trophy earned";
    }
}

std::uint32_t with_alpha(std::uint32_t rgba, float a) {
    const float base = static_cast<float>(rgba & 0xffu);
    return (rgba & 0xffffff00u) | static_cast<std::uint32_t>(std::lround(std::clamp(base * a, 0.0f, 255.0f)));
}

}  // namespace

const TrophyInfo* trophy_info(int id) {
    for (const TrophyInfo& t : kTrophies)
        if (t.id == id) return &t;
    return nullptr;
}

int trophy_count() { return static_cast<int>(sizeof(kTrophies) / sizeof(kTrophies[0])); }

int trophy_count_of_grade(int grade) {
    int n = 0;
    for (const TrophyInfo& t : kTrophies) n += t.grade == grade;
    return n;
}

namespace {
constexpr int kIconCell = 120, kIconCols = 8;

struct IconSheet {
    bool tried = false, ok = false;
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
};
IconSheet g_icons;  // under g_icons_mu
std::mutex g_icons_mu;

std::uint32_t be32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }
std::uint64_t be64(const std::uint8_t* p) { return std::uint64_t(be32(p)) << 32 | be32(p + 4); }

#if defined(HELIAS_HAVE_SDL3)
// One TROP<id>.PNG into its cell: decoded, halved (2x2 box), the corners
// rounded so it sits in the pop-up's rounded tile.
bool place_icon(const std::uint8_t* png, std::size_t size, int id, IconSheet& sheet) {
    SDL_Surface* raw = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, size), true);
    if (!raw) return false;
    SDL_Surface* s = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    if (!s) return false;
    const bool fits = s->w == kIconCell * 2 && s->h == kIconCell * 2;
    if (fits) {
        const int cx = (id % kIconCols) * kIconCell, cy = (id / kIconCols) * kIconCell;
        const float r = kIconCell * 0.06f;  // a slight rounding: the frames are ornate to their corners
        for (int y = 0; y < kIconCell; ++y) {
            for (int x = 0; x < kIconCell; ++x) {
                int sum[4] = {0, 0, 0, 0};
                for (int dy = 0; dy < 2; ++dy) {
                    const auto* row = static_cast<const std::uint8_t*>(s->pixels) + (y * 2 + dy) * s->pitch;
                    for (int dx = 0; dx < 2; ++dx)
                        for (int c = 0; c < 4; ++c) sum[c] += row[(x * 2 + dx) * 4 + c];
                }
                // Distance outside the rounded corner, for a soft edge a pixel wide.
                const float qx = std::max(0.0f, std::max(r - (x + 0.5f), (x + 0.5f) - (kIconCell - r)));
                const float qy = std::max(0.0f, std::max(r - (y + 0.5f), (y + 0.5f) - (kIconCell - r)));
                const float cover = std::clamp(r + 0.5f - std::sqrt(qx * qx + qy * qy), 0.0f, 1.0f);
                std::uint8_t* out = &sheet.rgba[((cy + y) * sheet.w + cx + x) * 4];
                for (int c = 0; c < 3; ++c) out[c] = static_cast<std::uint8_t>(sum[c] / 4);
                out[3] = static_cast<std::uint8_t>(std::lround(sum[3] / 4.0f * cover));
            }
        }
    }
    SDL_DestroySurface(s);
    return fits;
}
#endif

void load_icons(IconSheet& sheet) {
#if defined(HELIAS_HAVE_SDL3)
    const std::string path = hle_fs_map_path_base("/app0/sce_sys/trophy/trophy00.trp");
    std::ifstream f(path, std::ios::binary);
    const std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // The pack: a 0x60-byte header (magic 0xDCA24D00, version, size, entry
    // count, entry size), then entries of a 32-byte name, offset and size.
    if (d.size() < 0x60 || be32(d.data()) != 0xDCA24D00u) {
        host_log("trophy: no trophy pack at %s - the pop-up shows the grade's medal", path.c_str());
        return;
    }
    const std::uint32_t count = be32(&d[16]), esize = be32(&d[20]);
    const int rows = (trophy_count() + kIconCols - 1) / kIconCols;
    sheet.w = kIconCols * kIconCell;
    sheet.h = rows * kIconCell;
    sheet.rgba.assign(static_cast<std::size_t>(sheet.w) * sheet.h * 4, 0);
    int placed = 0;
    for (std::uint32_t i = 0; i < count && esize >= 48 && 0x60 + (i + 1) * std::size_t(esize) <= d.size(); ++i) {
        const std::uint8_t* e = &d[0x60 + i * std::size_t(esize)];
        char name[33] = {};
        std::memcpy(name, e, 32);
        int id = -1;
        if (std::sscanf(name, "TROP%3d.PNG", &id) != 1 || id < 0 || id >= trophy_count()) continue;
        const std::uint64_t off = be64(e + 32), size = be64(e + 40);
        if (off > d.size() || size > d.size() - off) continue;
        placed += place_icon(&d[off], size, id, sheet);
    }
    sheet.ok = placed > 0;
    host_log("trophy: %d of %d icons from %s", placed, trophy_count(), path.c_str());
#else
    (void)sheet;
#endif
}
}  // namespace

bool trophy_icon_sheet(const std::uint8_t** rgba, int* w, int* h, int* cell, int* cols) {
    std::lock_guard<std::mutex> lk(g_icons_mu);
    if (!g_icons.tried) {
        g_icons.tried = true;
        load_icons(g_icons);
    }
    if (!g_icons.ok) return false;
    *rgba = g_icons.rgba.data();
    *w = g_icons.w;
    *h = g_icons.h;
    *cell = kIconCell;
    *cols = kIconCols;
    return true;
}

void trophy_popup(int id, int earned, int total) {
    const TrophyInfo* t = trophy_info(id);
    host_log("trophy: %s (%d) earned - %d of %d", t ? t->name : "?", id, earned, total);
    if (!host_settings().trophy_popups) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_queue.push_back({id, earned, total});
}

void trophy_popup_frame(float display_w, float display_h) {
    // HELIAS_TROPHY_TEST=<id>: that trophy's pop-up once, a few seconds in, to see it.
    static bool tested = false;
    static const auto started = std::chrono::steady_clock::now();
    if (!tested && std::chrono::steady_clock::now() - started > std::chrono::seconds(8)) {
        tested = true;
        if (const char* e = std::getenv("HELIAS_TROPHY_TEST"); e && *e) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_queue.push_back({std::atoi(e), 27, 40});
        }
    }
    // Or on demand: a file $XDG_RUNTIME_DIR/heliasdream-trophy-test holding an
    // id shows that trophy's pop-up and is removed. Looked for once a second.
    static auto looked = started;
    if (std::chrono::steady_clock::now() - looked > std::chrono::seconds(1)) {
        looked = std::chrono::steady_clock::now();
        if (const char* dir = std::getenv("XDG_RUNTIME_DIR"); dir && *dir) {
            const std::string path = std::string(dir) + "/heliasdream-trophy-test";
            if (std::FILE* f = std::fopen(path.c_str(), "r")) {
                int id = 23;
                if (std::fscanf(f, "%d", &id) != 1) id = 23;
                std::fclose(f);
                std::remove(path.c_str());
                host_log("trophy: test pop-up for %d (%s)", id, path.c_str());
                std::lock_guard<std::mutex> lk(g_mu);
                g_queue.push_back({id, 27, 40});
            }
        }
    }
    Popup p;
    double t = 0.0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        const auto now = std::chrono::steady_clock::now();
        if (g_showing && std::chrono::duration<double>(now - g_since).count() > kIn + kHold + kOut) g_showing = false;
        if (!g_showing && !g_queue.empty()) {
            g_current = g_queue.front();
            g_queue.pop_front();
            g_showing = true;
            g_since = now;
            haptics_menu_tick(1);  // a firm tap on a DualSense, as a console's notification buzz
        }
        if (!g_showing) return;
        p = g_current;
        t = std::chrono::duration<double>(now - g_since).count();
    }
    const TrophyInfo* info = trophy_info(p.id);
    if (!info) return;
    // Slides in from the right and fades, holds, then fades out.
    const double in = std::clamp(t / kIn, 0.0, 1.0), out = std::clamp((t - kIn - kHold) / kOut, 0.0, 1.0);
    const float ease = static_cast<float>(1.0 - std::pow(1.0 - in, 3.0));
    const float a = static_cast<float>(std::min(in * 1.4, 1.0) * (1.0 - out));
    // The launcher mock-up's toast (Toast.dc.html) at 1.15x at 1080p, scaled
    // with the screen's height so it is the same share of a 4K screen: a dark
    // glass card, no border, and under it a thin pill that empties while it stays.
    const float k = 1.15f * std::max(display_h, 360.0f) / 1080.0f;
    const float w = 400.0f * k, h = 84.0f * k, r = 20.0f * k;
    const float x = display_w - w - 36.0f * k + (1.0f - ease) * 52.0f * k, y = 32.0f * k;

    host_overlay_glass(x, y, w, h, r, 20.0f * k, 0.72f);
    host_overlay_round_rect(x, y, w, h, r, with_alpha(0x0e10168cu, a), with_alpha(0x0e10168cu, a));
    // The trophy's own icon (its frame's metal is its grade); without the
    // game's trophy pack, a dark tile with the grade's medal on it.
    const float m = 60.0f * k, mx = x + 12.0f * k, my = y + (h - m) * 0.5f;
    if (!host_overlay_picture(mx, my, m, m, p.id, with_alpha(0xffffffffu, a))) {
        host_overlay_round_rect(mx, my, m, m, 15.0f * k, with_alpha(0x00000066u, a), with_alpha(0x00000066u, a));
        std::uint32_t top, bottom;
        medal(info->grade, top, bottom);
        const float d = 34.0f * k, dx = mx + (m - d) * 0.5f, dy = my + (m - d) * 0.5f;
        host_overlay_round_rect(dx - 2.0f, dy - 2.0f, d + 4.0f, d + 4.0f, (d + 4.0f) * 0.5f, with_alpha(0x0e1016ffu, a), with_alpha(0x0e1016ffu, a));
        host_overlay_round_rect(dx, dy, d, d, d * 0.5f, with_alpha(top, a), with_alpha(bottom, a));
        host_overlay_round_rect(dx + d * 0.2f, dy + d * 0.14f, d * 0.36f, d * 0.22f, d * 0.11f, with_alpha(0xffffff8cu, a), with_alpha(0xffffff00u, a));
    }
    // The grade in small capitals, the name in the serif, the progress.
    const float tx = mx + m + 14.0f * k;
    std::string label = grade_name(info->grade);
    for (char& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    host_overlay_ui_text(kOverlayFontBold, tx, y + 13.0f * k, 11.0f * k, with_alpha(0xc9d6eeffu, a), label.c_str());
    std::string name = info->name;
    const float room = x + w - 18.0f * k - tx;
    while (name.size() > 4 && host_overlay_ui_text_width(kOverlayFontTitle, 24.0f * k, name.c_str()) > room) name = name.substr(0, name.size() - 4) + "...";
    host_overlay_ui_text(kOverlayFontTitle, tx, y + 29.0f * k, 24.0f * k, with_alpha(0xe9edf5ffu, a), name.c_str());
    char line[64];
    std::snprintf(line, sizeof(line), "%d of %d \u00b7 %d%%", p.earned, p.total, p.total ? p.earned * 100 / p.total : 0);
    host_overlay_ui_text(kOverlayFontBody, tx, y + 58.0f * k, 12.0f * k, with_alpha(0xa7b0c0ffu, a), line);
    // How long it stays.
    const float bh = 4.0f * k, by = y + h + 10.0f * k;
    const float left = static_cast<float>(1.0 - std::clamp((t - kIn) / (kHold + kOut), 0.0, 1.0));
    host_overlay_round_rect(x, by, w, bh, bh * 0.5f, with_alpha(0xffffff1au, a), with_alpha(0xffffff1au, a));
    if (w * left > bh) host_overlay_round_rect(x, by, w * left, bh, bh * 0.5f, with_alpha(0xc9d6eebfu, a), with_alpha(0xc9d6eebfu, a));
}
