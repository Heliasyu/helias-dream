#pragma once

// heliasdream-vk's presented-frame capture (docs/vk/PLAN.md, Phase 0 debug
// tooling): the swapchain image exactly as it goes to the display - after the
// upscale, with whatever overlay is up - written as a PNG, for side-by-side
// comparisons of upscalers and settings. (F12's capture is the game's display
// buffer at the render size, before any upscale, plus every render target;
// host_gpu_request_dump.)
//
// Off unless HELIAS_VK_CAPTURE=1: then the swapchain images also take
// transfer reads (the one change to the presenter's resources), and
//   - Shift+F12 captures the next presented frame,
//   - a file named capture.request appearing in the capture folder captures
//     one (bbtest mark / bbtest shot touch it; it is removed when taken),
//   - HELIAS_VK_CAPTURE_EVERY=<seconds> captures one that often.
// The folder is HELIAS_VK_CAPTURE_DIR, else logs/captures (or captures/ when
// there is no logs folder). Files: frame-<date>-<time>-<ms>-<upscaler>.png.
// Without HELIAS_VK_CAPTURE=1 Shift+F12 is F12, as before.

#include <vulkan/vulkan.h>

#include <cstdint>

namespace frame_capture {

bool enabled();  // HELIAS_VK_CAPTURE=1

// What the swapchain's images need besides their own usage: transfer source
// when enabled and the surface allows it (`supported`, the surface's
// supportedUsageFlags). Remembers whether it was granted.
VkImageUsageFlags swapchain_usage(VkImageUsageFlags supported);

// Asks for the next presented frame. Any thread. With HELIAS_FSR_DUMP=1 it
// also asks for the FSR stage dump (host/fsr_dump.h).
void request(const char* why);

// The window pump, every frame: the trigger file and the timer.
void poll();

// The presenter, at the end of recording, once the image is in
// PRESENT_SRC_KHR: when a capture is wanted, copies it into a buffer of its
// own and leaves it in PRESENT_SRC_KHR again. True when it recorded a copy.
bool record(VkCommandBuffer cmd, VkDevice device, VkPhysicalDevice phys, VkImage image, VkFormat format, VkExtent2D extent);

// The presenter, right after waiting for its last submission's fence: a copy
// that was recorded has landed, and is written to its file on a thread of
// its own.
void after_fence(VkDevice device);

void shutdown(VkDevice device);

}  // namespace frame_capture
