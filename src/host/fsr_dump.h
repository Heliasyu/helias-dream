#pragma once

// heliasdream-vk test3: the FSR stage dump - one frame's upscaler inputs, its
// output and the scale pass's target before and after, written as PNGs with
// a text file of their sizes and numbers, so a hardware run shows each
// stage of the picture.
//
// Off unless HELIAS_FSR_DUMP=1 (or HeliasDream.toml [debug] fsr_dump = true).
// Then a frame is dumped:
//   - when a file `fsr-dump.request` appears in the capture folder,
//   - at every `bbtest mark` (its capture.request; with bbtest --capture
//     the presented frame is captured too), Shift+F12 with HELIAS_VK_CAPTURE=1,
//   - once by itself at the 600th upscaled frame (HELIAS_FSR_DUMP_AUTO=0: not).
// The folder: HELIAS_VK_CAPTURE_DIR, else logs/captures (captures/ without a
// logs folder), one fsr-<date>-<time>-flip<n>/ per dump. A dump waits for the
// GPU once (a hitch of some tens of ms); the files are written on a thread
// of their own.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

#include "host/temporal/dump_pixels.h"

namespace gpu {

bool fsr_dump_enabled();

// Under g.mu, at the scale pass with an upscaler running: whether this frame
// is dumped (polls the request files every 30 flips). `upscaled` counts the
// upscaled frames (the automatic one).
bool fsr_dump_wanted_locked(std::uint64_t flip, std::uint64_t upscaled);

// Under g.mu, while the frame is dumped: copies an image (VK_IMAGE_LAYOUT_GENERAL,
// written by commands recorded before this) into a staging buffer of its own,
// in the current recording. `w` x `h` from (0, 0). False (and a line in the
// info file) when the format is not one the dump reads.
bool fsr_dump_add_locked(const char* name, VkImage image, VkFormat format, std::uint32_t w, std::uint32_t h, temporal::DumpKind kind);
// A line for the info file.
void fsr_dump_note_locked(const std::string& line);
// Submits and waits once, then writes every image added and the info file
// on a thread. Logs the folder and each image's numbers.
void fsr_dump_finish_locked(std::uint64_t flip);

}  // namespace gpu

// Any thread: the next upscaled frame is dumped (frame_capture's request,
// Shift+F12 / capture.request with HELIAS_VK_CAPTURE=1). Nothing when off.
void host_fsr_dump_request(const char* why);
