#pragma once

// Helia's Dream: HDR with the UI kept apart (the UI brightness setting).
//
// HDR output expands the finished SDR frame's highlights (host/present_pass.h),
// so a white menu text was lifted as high as a lamp. With UI brightness set,
// the presenter tells the game's UI from its picture: the frame is copied just
// before its first UI draw (Scaleform's movie vertex shader,
// temporal::is_ui_vs), and when the game copies the finished frame into a
// display buffer that copy goes with it, kept per display buffer so a later
// frame cannot overwrite it before the buffer is shown. At present, a pixel of
// the display buffer that differs from its copy is UI: shown at the UI
// brightness, not expanded. A display buffer written any other way has no
// copy, and its frame is plain expansion. Costs two copies of the frame on the
// GPU per frame, only while the setting is on and HDR runs.
//
// The renderer's half, under g.mu; the presenter's, hdr_ui_source, any thread.

#include <cstdint>

#include <vulkan/vulkan.h>

namespace gpu {

struct RtImage;

// Whether the copies are wanted now (HDR on and UI brightness set).
bool hdr_ui_wanted();

// Before the draw of a UI pipeline into `target`: copy it once this frame.
void hdr_ui_before_ui_locked(RtImage* target);
// Before a draw into display buffer `display` that samples `source`: the
// copy of `source` taken before its UI becomes the display buffer's (or the
// display buffer's is dropped when there is none).
void hdr_ui_display_written_locked(RtImage* display, RtImage* source);

}  // namespace gpu

// The presenter's: display buffer `va`'s copy from before its UI, when there
// is a current one - its image (GENERAL), format and size. Any thread.
bool host_gpu_hdr_ui_source(std::uint64_t va, VkImage* image, VkFormat* format, std::uint32_t* width, std::uint32_t* height);
