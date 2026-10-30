#pragma once

// heliasdream-vk Phase 2: what a temporal upscaler is handed each frame, the
// same for every backend - FSR 3.1 (Phase 3), FSR 4.1.1 (Phase 4), and FSR 4
// INT8, DLSS and XeSS (Phase 7). Each takes what it needs and maps the
// conventions below to its own (jitter sign, motion scale, exposure).
//
// Made by host/temporal_inputs.cpp at the game's scale pass (the end of the
// scene, before post-processing), on the renderer's queue: every image is
// in VK_IMAGE_LAYOUT_GENERAL and written by commands recorded before the
// point the upscaler's would be.

#include "host/temporal/camera.h"

#include <vulkan/vulkan.h>

#include <cstdint>

struct UpscalerImage {
    VkImage image = VK_NULL_HANDLE;  // null: not available this frame
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0, height = 0;  // the image's size
    std::uint32_t used_w = 0, used_h = 0; // the region from (0, 0) that holds the frame (the render size)
};

struct UpscalerInputs {
    std::uint64_t frame = 0;  // the flip of the frame these are for
    bool valid = false;       // false: no scene this frame (menus, loads) - the backend passes the frame through

    std::uint32_t render_w = 0, render_h = 0;  // the scene's size
    std::uint32_t output_w = 0, output_h = 0;  // what the scale pass writes (post, UI, display)

    // The HDR scene colour before post-processing (RGBA16F in the game), the
    // scale pass's source.
    UpscalerImage color;
    // The scene depth (D32S8, or D16 with a performance patch). D3D 0..1,
    // not reversed: near 0, far 1.
    UpscalerImage depth;
    // Phase 3: the same depth as a colour image (R32F, written by the motion
    // pass), for backends that sample depth as colour (FSR-Vulkan).
    UpscalerImage depth_copy;
    // RGBA16F: rg = current-to-previous motion in render pixels, +x right, +y
    // down; b = 1 where it is an object's (the game's velocity), a = 1 where
    // the camera motion was valid. Jitter already removed.
    UpscalerImage motion;
    // The scene colour as it was before the first transparent draw (particles,
    // fog, glass): with `color` it is what FSR's reactive-mask generation and
    // DLSS/XeSS's auto-reactive take. Null when it was not taken.
    UpscalerImage opaque_color;
    // Phase 3: the reactive mask (R8, 0..1), made from opaque_color and color
    // (shaders/reactive.comp, AMD's generate-reactive rule). Null when there
    // was no snapshot this frame or the setting (upscale_reactive) is off.
    UpscalerImage reactive;
    // Exposure: none - every backend runs its own auto-exposure on the HDR
    // input (FSR 3.1 / 4 ENABLE_AUTO_EXPOSURE, DLSS AutoExposure, XeSS
    // auto-exposure), as IFreemz does.
    UpscalerImage exposure;

    // The jitter applied to the scene's draws this frame: a viewport shift
    // in render pixels, +x right, +y down; Halton(2,3) phase of phases.
    float jitter_x = 0.0f, jitter_y = 0.0f;
    std::uint32_t jitter_phase = 0, jitter_phases = 1;

    // Reset the history: the camera cut (or there was no camera to compare).
    bool reset = true;
    temporal::Cut cut = temporal::Cut::First;

    // The camera, for backends that want it (FSR's depth linearisation).
    float near_plane = 0.05f, far_plane = 3000.0f;
    float fov_y = 0.0f;  // radians
    float frame_ms = 0.0f;  // since the last scale pass (wall clock), 16.7 when unknown
};
