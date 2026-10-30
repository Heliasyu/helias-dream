// heliasdream-vk's per-pass GPU timer (host/gpu_timer.h).
#include "host/gpu_timer.h"

#include "log.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

bool GpuTimer::init(VkDevice device, VkPhysicalDevice phys, std::uint32_t family, const char* name, int log_every) {
    shutdown();
    name_ = name ? name : "gpu-timer";
    log_every_ = log_every > 0 ? log_every : 300;
    std::uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
    if (family >= n) return false;
    VkQueueFamilyProperties* props = new VkQueueFamilyProperties[n];
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, props);
    const std::uint32_t bits = props[family].timestampValidBits;
    delete[] props;
    VkPhysicalDeviceProperties dp{};
    vkGetPhysicalDeviceProperties(phys, &dp);
    if (bits == 0 || dp.limits.timestampPeriod <= 0.0f) {
        host_log("%s: the queue family keeps no timestamps; GPU times are not available", name_);
        return false;
    }
    period_ns_ = dp.limits.timestampPeriod;
    mask_ = bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
    VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qci.queryCount = 2 * kMaxScopes;
    if (vkCreateQueryPool(device, &qci, nullptr, &pool_) != VK_SUCCESS) {
        pool_ = VK_NULL_HANDLE;
        return false;
    }
    device_ = device;
    window_start_ns_ = now_ns();
    host_log("%s: GPU timestamps ready (%u valid bits, %.2f ns a tick)", name_, bits, period_ns_);
    return true;
}

void GpuTimer::shutdown() {
    if (pool_) vkDestroyQueryPool(device_, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    std::lock_guard<std::mutex> lk(mu_);
    for (Scope& s : scopes_) s = Scope{};
    used_ = 0;
    recorded_ = false;
}

void GpuTimer::collect() {
    if (!recorded_) return;
    recorded_ = false;
    std::uint64_t ts[2 * kMaxScopes][2] = {};  // value, availability
    const VkResult r = vkGetQueryPoolResults(device_, pool_, 0, static_cast<std::uint32_t>(2 * used_), sizeof(ts), ts, sizeof(ts[0]),
                                             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (r != VK_SUCCESS && r != VK_NOT_READY) return;
    std::lock_guard<std::mutex> lk(mu_);
    for (int i = 0; i < used_; ++i) {
        Scope& s = scopes_[i];
        if (!s.written || !ts[2 * i][1] || !ts[2 * i + 1][1]) continue;
        const std::uint64_t a = ts[2 * i][0] & mask_, b = ts[2 * i + 1][0] & mask_;
        const std::uint64_t ticks = (b - a) & mask_;
        const double ms = static_cast<double>(ticks) * period_ns_ / 1e6;
        if (ms < 0.0 || ms > 1000.0) continue;  // a wrapped or reset counter
        s.sum_ms += ms;
        s.window_ms += ms;
        ++s.window_n;
    }
    ++frames_;
    const std::int64_t now = now_ns();
    if (now - window_start_ns_ >= 500000000ll) {
        for (Scope& s : scopes_)
            if (s.window_n) {
                s.shown_ms = s.window_ms / s.window_n;
                s.window_ms = 0.0;
                s.window_n = 0;
            }
        window_start_ns_ = now;
    }
    if (frames_ - frames_logged_ >= static_cast<std::uint64_t>(log_every_)) {
        const double n = static_cast<double>(frames_ - frames_logged_);
        char line[512];
        int off = std::snprintf(line, sizeof(line), "%s: GPU ms a frame over %llu frames:", name_, static_cast<unsigned long long>(n));
        for (Scope& s : scopes_) {
            if (!s.name) continue;
            if (off > 0 && off < static_cast<int>(sizeof(line)))
                off += std::snprintf(line + off, sizeof(line) - static_cast<std::size_t>(off), " %s %.3f", s.name, s.sum_ms / n);
            s.sum_ms = 0.0;
        }
        host_log("%s", line);
        frames_logged_ = frames_;
    }
}

void GpuTimer::frame_begin(VkCommandBuffer cmd) {
    if (!pool_) return;
    collect();
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (Scope& s : scopes_) {
            s.open = false;
            s.written = false;
        }
    }
    used_ = 0;
    vkCmdResetQueryPool(cmd, pool_, 0, 2 * kMaxScopes);
}

int GpuTimer::begin(VkCommandBuffer cmd, const char* pass) {
    if (!pool_ || !pass) return -1;
    int slot = -1;
    {
        std::lock_guard<std::mutex> lk(mu_);
        // A pass keeps its slot from frame to frame, so its average is its own.
        for (int i = 0; i < kMaxScopes && slot < 0; ++i)
            if (scopes_[i].name && std::strcmp(scopes_[i].name, pass) == 0) slot = i;
        for (int i = 0; i < kMaxScopes && slot < 0; ++i)
            if (!scopes_[i].name) {
                scopes_[i].name = pass;
                slot = i;
            }
        if (slot < 0 || scopes_[slot].open || scopes_[slot].written) return -1;
        scopes_[slot].open = true;
    }
    if (slot + 1 > used_) used_ = slot + 1;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, static_cast<std::uint32_t>(2 * slot));
    return slot;
}

void GpuTimer::end(VkCommandBuffer cmd, int slot) {
    if (!pool_ || slot < 0 || slot >= kMaxScopes) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!scopes_[slot].open) return;
        scopes_[slot].open = false;
        scopes_[slot].written = true;
    }
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, static_cast<std::uint32_t>(2 * slot + 1));
    recorded_ = true;
}

std::string GpuTimer::summary() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::string out;
    char part[64];
    for (const Scope& s : scopes_) {
        if (!s.name) continue;
        std::snprintf(part, sizeof(part), "%s%s %.2f ms", out.empty() ? "" : "  ", s.name, s.shown_ms);
        out += part;
    }
    return out;
}
