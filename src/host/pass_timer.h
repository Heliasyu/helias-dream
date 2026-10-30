#pragma once

// heliasdream-vk Phase 5: GPU time of the renderer's own passes, each on its own
// (Show Upscaler GPU Time): the motion-vector pass, the reactive mask, the
// opaque snapshot, the upscaler's dispatch (FSR 3.1 or FSR 4.1.1) and the copy
// of its output into the game's target. Timestamp pairs in a ring, read back
// once their submission has retired (never waited for), averaged over half a
// second for the overlay and logged every ~10 s, so a test log says where a
// 1080p -> 4K frame's GPU time goes. Off, nothing is recorded at all.
// All under g.mu (the renderer's recording thread).

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace gpu {

// Sharpen (test7): FSR 4.1.1's own sharpening pass, inside the Upscale span.
enum class TimedPass : int { Motion, Reactive, Opaque, Upscale, Copy, Sharpen, kCount };

// Whether timing is on (the setting, re-read when the settings move).
bool pass_timer_on_locked();
// Before the pass's commands: the slot to end with, or -1 (off, no
// timestamps on this queue, or the ring full).
// `stage`: where the first timestamp is taken - after a barrier, the stage it
// waited for (else top of pipe, which may run ahead of earlier work).
std::int32_t pass_timer_begin_locked(VkCommandBuffer cmd, TimedPass pass,
                                     VkPipelineStageFlagBits stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
// After them. The submission being recorded is g.flushes + 1.
void pass_timer_end_locked(VkCommandBuffer cmd, std::int32_t slot);
// Reads the retired slots (cheap; once a frame from the scale pass).
void pass_timer_collect_locked();
// Which upscaler the Upscale pass was, for its label: a string literal.
void pass_timer_upscaler_name(const char* name);

}  // namespace gpu

// The overlay's line (host/window.cpp): "MV 0.10  reactive 0.04  opaque 0.06
// FSR 3.1 1.23  copy 0.08 ms", empty before anything was timed.
std::string host_gpu_pass_timing();
