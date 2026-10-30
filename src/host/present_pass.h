#pragma once

// The presenter's one pass over the swapchain image (host/window.cpp). The
// game's display image is drawn into the picture's rectangle - a full-screen
// triangle sampling it, which is what vkCmdBlitImage does with a linear filter
// - and the host overlay (the FPS counter, the pointer, the text box, F10's
// screen) and the plugin menu (F9) go into the same rendering. The bars beside
// a picture of another shape are the rendering's clear.
//
// What it replaces, per presented frame: a transfer-layout change, a clear of
// the whole image when there are bars, the blit, and - with anything on the
// overlay - a second layout change and a second rendering that loaded the
// image the blit had just stored. Now: one layout change in, one rendering,
// one out. A magnified picture (FSR 1, host/fsr.cpp) keeps its compute passes.

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

// Presenter-owned, for the swapchain's format; safe to call repeatedly.
bool present_pass_init(VkDevice device, VkPhysicalDevice phys, VkFormat colour_format);
void present_pass_shutdown(VkDevice device);
bool present_pass_ready();
// rc2, HELIAS_SHARPEN_AT_PRESENT=1 (debugging): FSR, having left its own
// sharpening out, asks the presenter to sharpen (RCAS, `sharpness` 0..1).
// Any thread; good for 100 ms (a frame without FSR shows plain). False when
// there is no sharpening pipeline: the caller keeps FSR's own pass.
bool present_pass_set_sharpen(float sharpness);
bool present_sharpen_at_present();  // HELIAS_SHARPEN_AT_PRESENT=1

// The game's display image and the region of it shown.
struct PresentSource {
    VkImage image = VK_NULL_HANDLE;  // in GENERAL, written by earlier submissions on the renderer's queue
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0, height = 0;  // the image's size
    std::uint32_t x = 0, y = 0, w = 0, h = 0;  // the region
};

// Records the layout change of `dst` (from UNDEFINED, ordered after
// COLOR_ATTACHMENT_OUTPUT, the stage the acquire is waited at), begins a
// rendering over `dst_view` that clears to `clear` unless `shown` covers the
// whole image, and draws `src` (null: nothing, the clear is the frame) into
// `shown`. The rendering stays open for the overlay and the menu; end it with
// present_pass_end. False, with nothing recorded, when the pass cannot sample
// the source (the caller blits instead). Call after the presenter's fence: the
// last frame's view of the source and its descriptor are reused here.
bool present_pass_begin(VkCommandBuffer cmd, VkImage dst, VkImageView dst_view, VkExtent2D extent, VkRect2D shown,
                        const PresentSource* src, const float clear[4]);
// heliasdream-vk Phase 2, Show Motion Vectors: the scene's motion image (its
// region `x, y, w, h`) blended over `shown`, inside the open rendering,
// before the overlay. False when there is nothing to draw it with.
bool present_pass_motion(VkCommandBuffer cmd, VkRect2D shown, const PresentSource& motion);
// Helia's Dream: the frosted glass under the overlay's panes - the picture
// `src` (the one present_pass_begin drew), blurred inside each rounded
// rectangle (overlay coordinates, mapped onto `area` as the overlay's are).
// Inside the open rendering, after the picture and before the overlay. False
// when there is nothing to draw it with.
struct OverlayGlass;
bool present_pass_glass(VkCommandBuffer cmd, VkRect2D shown, const PresentSource& src, const OverlayGlass* rects, std::size_t count,
                        VkRect2D area, float display_w, float display_h);
// Helia's Dream, set each frame by the presenter: the picture's grade
// (brightness, contrast: 1 the game's own; hue in degrees), and HDR output
// (the swapchain is HDR10: compose in an SDR layer and encode it at the end).
void present_pass_set_grade(float brightness, float contrast, float hue_degrees);
void present_pass_set_hdr(bool on, float paper_white, float peak, float boost);
// Helia's Dream, HDR with the UI kept apart (host/hdr_ui.h), set each frame
// before present_pass_begin: the UI's brightness (times paper white; 0 off),
// and the display buffer's copy from before its UI (null: none this frame -
// then only our overlay is told apart).
void present_pass_set_hdr_ui(float ui_brightness, const PresentSource* before_ui);
// Ends the rendering and moves `dst` to PRESENT_SRC_KHR.
void present_pass_end(VkCommandBuffer cmd, VkImage dst);
