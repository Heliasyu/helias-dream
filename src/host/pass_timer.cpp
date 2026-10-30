// heliasdream-vk Phase 5: the renderer passes' GPU time (host/pass_timer.h).
#include "host/pass_timer.h"

#include "host/gpu_internal.h"
#include "host/options.h"
#include "host/settings.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>

namespace gpu {
namespace {

constexpr int kPasses = static_cast<int>(TimedPass::kCount);
constexpr std::uint32_t kTimerPairs = 64;  // timestamp pairs: several passes a frame, a few frames in flight
const char* const kNames[kPasses] = {"MV", "reactive", "opaque", "upscale", "copy", "sharpen"};

VkQueryPool g_pool = VK_NULL_HANDLE;
bool g_tried = false;
double g_tick_ns = 1.0;
std::uint64_t g_tick_mask = ~0ull;
std::uint64_t g_slot_frame[kTimerPairs] = {};  // submission + 1 each slot's pair went into; 0 free
int g_slot_pass[kTimerPairs] = {};
std::uint32_t g_next = 0;
double g_sum[kPasses] = {}, g_log_sum[kPasses] = {};
int g_n[kPasses] = {}, g_log_n[kPasses] = {};
std::chrono::steady_clock::time_point g_shown_at{}, g_logged_at{};
std::atomic<const char*> g_upscaler_name{"FSR 3.1"};  // a string literal

std::mutex g_text_mu;
std::string g_text;  // under g_text_mu: the overlay's line

bool ready_locked() {
    if (g_tried) return g_pool != VK_NULL_HANDLE;
    g_tried = true;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(g.phys, &props);
    std::uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qf(n);
    vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &n, qf.data());
    const std::uint32_t bits = g.family < n ? qf[g.family].timestampValidBits : 0;
    if (!bits || props.limits.timestampPeriod <= 0.0f) {
        host_log("pass-timer: no GPU timestamps on this queue: the renderer's passes are not timed");
        return false;
    }
    g_tick_ns = props.limits.timestampPeriod;
    g_tick_mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2 * kTimerPairs;
    if (vkCreateQueryPool(g.device, &qi, nullptr, &g_pool) != VK_SUCCESS) g_pool = VK_NULL_HANDLE;
    return g_pool != VK_NULL_HANDLE;
}

std::string line(const double* sum, const int* n, const char* sep) {
    std::string out;
    char buf[48];
    for (int p = 0; p < kPasses; ++p) {
        if (!n[p]) continue;
        const char* name = p == static_cast<int>(TimedPass::Upscale) ? g_upscaler_name.load() : kNames[p];
        std::snprintf(buf, sizeof(buf), "%s%s %.2f", out.empty() ? "" : sep, name, sum[p] / n[p]);
        out += buf;
    }
    if (!out.empty()) out += " ms";
    return out;
}

}  // namespace

bool pass_timer_on_locked() {
    static std::uint64_t serial = ~0ull;
    static bool on = false;
    if (host_opt_serial() != serial) {
        serial = host_opt_serial();
        on = host_settings().debug_upscaler_time;
        if (!on) {
            std::lock_guard<std::mutex> l(g_text_mu);
            g_text.clear();
        }
    }
    // rc2: also the GPU sampler's frame, so a capture has FSR's and the
    // sharpening's times with the overlay line off.
    return on || host_gpu_sample_running();
}

std::int32_t pass_timer_begin_locked(VkCommandBuffer cmd, TimedPass pass, VkPipelineStageFlagBits stage) {
    if (!cmd || !pass_timer_on_locked() || !ready_locked()) return -1;
    const std::uint32_t slot = g_next % kTimerPairs;
    if (g_slot_frame[slot]) return -1;  // still in flight: this one goes untimed
    ++g_next;
    g_slot_pass[slot] = static_cast<int>(pass);
    vkCmdResetQueryPool(cmd, g_pool, 2 * slot, 2);
    vkCmdWriteTimestamp(cmd, stage, g_pool, 2 * slot);
    return static_cast<std::int32_t>(slot);
}

void pass_timer_end_locked(VkCommandBuffer cmd, std::int32_t slot) {
    if (slot < 0 || !cmd) return;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_pool, 2 * static_cast<std::uint32_t>(slot) + 1);
    g_slot_frame[slot] = g.flushes + 1;
}

void pass_timer_collect_locked() {
    if (!g_pool) return;
    for (std::uint32_t k = 0; k < kTimerPairs; ++k) {
        if (!g_slot_frame[k] || g_slot_frame[k] > g.completed_submits) continue;
        std::uint64_t t[2] = {};
        if (vkGetQueryPoolResults(g.device, g_pool, 2 * k, 2, sizeof(t), t, sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            const double ms = static_cast<double>((t[1] - t[0]) & g_tick_mask) * g_tick_ns * 1e-6;
            const int p = g_slot_pass[k];
            if (ms >= 0.0 && ms < 1000.0 && p >= 0 && p < kPasses) {
                g_sum[p] += ms;
                ++g_n[p];
                g_log_sum[p] += ms;
                ++g_log_n[p];
            }
        }
        g_slot_frame[k] = 0;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - g_shown_at >= std::chrono::milliseconds(500)) {
        g_shown_at = now;
        bool any = false;
        for (int p = 0; p < kPasses; ++p) any |= g_n[p] != 0;
        if (any) {
            std::string s = line(g_sum, g_n, "  ");
            std::lock_guard<std::mutex> l(g_text_mu);
            g_text = std::move(s);
        }
        for (int p = 0; p < kPasses; ++p) g_sum[p] = 0.0, g_n[p] = 0;
    }
    if (now - g_logged_at >= std::chrono::seconds(10)) {
        // The frames counted are the upscaled ones: the game's, loading screens
        // included, and those the presenter later drops for a newer one. test4
        // read ~119 of them a second while 112 were shown, so this is not the
        // FPS; `present: 300 frames in ... (N fps shown)` is. The window is also
        // the time since the last line, not exactly 10 s.
        const double secs = std::chrono::duration<double>(now - g_logged_at).count();
        const bool first = g_logged_at == std::chrono::steady_clock::time_point{};
        g_logged_at = now;
        int frames = 0;
        for (int p = 0; p < kPasses; ++p) frames = g_log_n[p] > frames ? g_log_n[p] : frames;
        if (frames && !first)
            host_log("pass-timer: GPU per pass over the last %.1f s (%d upscaled frames, %.1f a second - not the FPS shown): %s", secs, frames,
                     frames / secs, line(g_log_sum, g_log_n, ", ").c_str());
        else if (frames)
            host_log("pass-timer: GPU per pass over the first upscaled frames (%d): %s", frames, line(g_log_sum, g_log_n, ", ").c_str());
        for (int p = 0; p < kPasses; ++p) g_log_sum[p] = 0.0, g_log_n[p] = 0;
    }
}

void pass_timer_upscaler_name(const char* name) { g_upscaler_name.store(name); }

}  // namespace gpu

std::string host_gpu_pass_timing() {
    std::lock_guard<std::mutex> l(gpu::g_text_mu);
    return gpu::g_text;
}
