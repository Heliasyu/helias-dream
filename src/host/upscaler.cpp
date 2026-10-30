// heliasdream-vk's upscaler settings and their scaffolding (host/upscaler.h).
#include "host/upscaler.h"

#include "engine/scene_split.h"
#include "engine/scene_split_sites.h"
#include "host/upscaler_presets.h"
#include "host/options.h"
#include "host/settings.h"
#include "log.h"

#include <atomic>
#include <cstdint>

#if !defined(HELIAS_HAVE_FSR_VULKAN)
// No FSR-Vulkan in this build (HELIAS_FSR_VULKAN=OFF, or no Vulkan).
int upscaler_fsr3_embedded_modules() { return 0; }
bool upscaler_fsr3_linked() { return false; }
#endif

namespace {

// The chosen upscaler (settings upscaler), for upscaler_scene_pass on any thread.
std::atomic<int> g_kind{upscaler::kFsr1};

void log_settings(const char* why) {
    const HostSettings s = host_settings();
    // test3: from the Resolution setting, as the split computes it (it said
    // "from 3840x2160" whatever the Resolution, and rounded differently).
    const std::uint32_t ow = s.res_width > 0 ? static_cast<std::uint32_t>(s.res_width) : 1920;
    const std::uint32_t oh = s.res_height > 0 ? static_cast<std::uint32_t>(s.res_height) : 1080;
    std::uint32_t w = ow, h = oh;
    if (s.upscaler != upscaler::kOff) scene_split::render_size(upscaler::preset_ratio(s.upscale_quality), ow, oh, &w, &h);
    g_kind.store(s.upscaler, std::memory_order_relaxed);
    host_log("upscaler: %s: %s, %s (%ux%u from %ux%u), sharpness %.1f, reactive mask %s, jitter %s; motion vectors view %s, GPU time %s "
             "(a preset below Native AA renders the scene smaller: FSR 3.1 upscales it, otherwise bilinear; FSR 4.1.1 replays its assets when built, else FSR 3.1)",
             why, upscaler::kind_name(s.upscaler), upscaler::preset_name(s.upscale_quality), w, h, ow, oh, static_cast<double>(s.upscale_sharpness),
             s.upscale_reactive ? "on" : "off", s.upscale_jitter ? "on" : "off", s.debug_motion_vectors ? "on" : "off", s.debug_upscaler_time ? "on" : "off");
}

}  // namespace

void upscaler_log_startup() {
    const int modules = upscaler_fsr3_embedded_modules();
    if (upscaler_fsr3_linked()) {
        host_log("upscaler: FSR-Vulkan linked: FSR 3.1.5 and 3.1.4 upscalers, %d of 11 FSR 3.1.5 shader modules embedded%s", modules,
                 modules == 11 ? "" : "  <- the shader tables are incomplete");
    } else {
        host_log("upscaler: FSR-Vulkan not in this build");
    }
    log_settings("settings at start");
}

void upscaler_note_settings() {
    static int last[7] = {-1, -1, -1, -1, -1, -1, -1};
    const HostSettings s = host_settings();
    g_kind.store(s.upscaler, std::memory_order_relaxed);
    const int now[7] = {s.upscaler, s.upscale_quality, static_cast<int>(s.upscale_sharpness * 10.0f + 0.5f), s.debug_motion_vectors ? 1 : 0,
                        s.debug_upscaler_time ? 1 : 0, s.upscale_reactive ? 1 : 0, s.upscale_jitter ? 1 : 0};
    bool changed = false;
    for (int i = 0; i < 7; ++i) changed |= now[i] != last[i];
    const bool first = last[0] < 0;
    for (int i = 0; i < 7; ++i) last[i] = now[i];
    if (changed && !first) log_settings("changed");
}

void upscaler_debug_motion_vectors_hook(bool drawn) {
    // Phase 2: the presenter drew the motion image over the picture (drawn),
    // or there was none - the scene is not split, or no scene this frame
    // (menus, loads), or the picture went the magnified (FSR 1) path. Each
    // change is said once.
    static std::atomic<int> said{-1};
    const int now = drawn ? 1 : 0;
    if (said.exchange(now) != now)
        host_log(drawn ? "upscaler: the motion-vector view is drawn over the picture"
                       : "upscaler: the motion-vector view is on, but there are no motion vectors to show (the scene is not split, no scene, "
                         "or the picture is magnified by FSR 1)");
}

ScenePass upscaler_scene_pass() {
    // Phase 1: the split takes the bilinear copy. Phase 3: FSR 3.1 (and FSR
    // 4.1.1, replayed from its assets, else FSR 3.1) upscales instead, where this
    // build links it; the bilinear copy stays its fallback (host/fsr3_pass.h).
    if (!scene_split_active()) return ScenePass::Game;
#if defined(HELIAS_HAVE_FSR_VULKAN)
    const int k = g_kind.load(std::memory_order_relaxed);
    if (upscaler::is_temporal(k)) return ScenePass::Fsr31;
#endif
    return ScenePass::Bilinear;
}

const char* upscaler_scene_pass_name(ScenePass p) {
    switch (p) {
    case ScenePass::Game: return "the game's own";
    case ScenePass::Bilinear: return "bilinear";
    case ScenePass::Fsr31: return "FSR 3.1";
    }
    return "?";
}
