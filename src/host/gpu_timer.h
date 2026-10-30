#pragma once

// heliasdream-vk's per-pass GPU timer (docs/vk/PLAN.md, Phase 0 debug tooling):
// Vulkan timestamp queries around named passes of one command buffer that is
// recorded again only after its fence (the presenter's), read back a frame
// later without waiting, averaged, and logged every `log_every` frames.
//
//   timer.frame_begin(cmd);              // reads last frame's, resets the pool
//   const int s = timer.begin(cmd, "upscale");
//   ... the pass ...
//   timer.end(cmd, s);
//   timer.summary()                      // "upscale 0.31 ms  present 0.45 ms"
//
// Off (the settings' debug_upscaler_time) it records nothing at all, so the
// command buffer is exactly what it was without it.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <mutex>
#include <string>

class GpuTimer {
public:
    static constexpr int kMaxScopes = 8;

    // `name` prefixes its log lines. False (and every call a no-op) when the
    // queue family keeps no timestamps.
    bool init(VkDevice device, VkPhysicalDevice phys, std::uint32_t family, const char* name, int log_every = 300);
    void shutdown();
    bool ready() const { return pool_ != VK_NULL_HANDLE; }

    // At the start of a frame's recording, after the fence of the last
    // submission of this command buffer has signalled.
    void frame_begin(VkCommandBuffer cmd);
    // A pass: the slot to end it with, or -1 (not ready, or out of slots).
    int begin(VkCommandBuffer cmd, const char* pass);
    void end(VkCommandBuffer cmd, int slot);

    // The passes' average milliseconds over the last half second, one line.
    std::string summary() const;

private:
    struct Scope {
        const char* name = nullptr;
        bool open = false, written = false;
        double sum_ms = 0.0;      // since the last log
        double shown_ms = 0.0;    // the last half second's average, for summary()
        double window_ms = 0.0;
        int window_n = 0;
    };
    void collect();

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    double period_ns_ = 1.0;
    std::uint64_t mask_ = ~0ull;
    const char* name_ = "gpu-timer";
    int log_every_ = 300;
    Scope scopes_[kMaxScopes];
    int used_ = 0;          // slots handed out this frame
    bool recorded_ = false; // last frame wrote queries
    std::uint64_t frames_ = 0, frames_logged_ = 0;
    std::int64_t window_start_ns_ = 0;
    mutable std::mutex mu_;  // summary() is read from the window thread
};
