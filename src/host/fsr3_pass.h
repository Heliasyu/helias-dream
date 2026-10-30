#pragma once

// heliasdream-vk Phase 3: FSR 3.1 at the game's scale pass (host/upscaler.h,
// ScenePass::Fsr31). The renderer calls it for the pass's draw, after the
// temporal inputs are made (host/temporal_inputs.h): FSR 3.1.5
// (host/fsr3_upscaler.h) upscales the frame's scene into an image of its
// own, which is copied into the pass's target, and the game's draw is
// skipped. When it cannot - no context (its creation failed, and is not
// retried until the sizes change), inputs missing this frame, a target it
// cannot copy into - it says why once and the game's draw runs as Phase 1's
// bilinear copy. FSR 4.1.1 selected uses this until Phase 4.

#include "host/temporal/fsr3_policy.h"
#include "host/upscaler_inputs.h"

#include <cstdint>
#include <string>

namespace gpu {

struct RtImage;

// Under g.mu, the pass's render target bound (and initialised). True: the
// target holds the upscaled scene; the caller skips the draw and marks the
// target written.
// `out`: where in the target the scene goes (fsr3_output_rect_locked): FSR's
// output size and the copy back, never the target's extent.
// test10 (HELIAS_POST_AT_RENDER): `color`, when given, is the upscaler's
// colour input instead of the scene's - the post chain's render-size result.
bool fsr3_scale_pass_locked(RtImage* target, const temporal::OutputRect& out, const UpscalerImage* color = nullptr);
// test4: whether a colour target being made should take STORAGE usage so FSR
// 3.1 writes it directly instead of its own image and a copy (0.23 ms a frame
// at 3840x2160 in test3): RGBA16F, one layer, exactly the display buffers'
// size with the scene split and FSR 3.1 chosen. HELIAS_FSR_DIRECT_TARGET=0: never.
bool fsr3_target_storage_wanted(int vk_format, std::uint32_t w, std::uint32_t h, std::uint32_t layers);
// test6: FSR 4.1.1 could not run this session (no assets or device support) and FSR 3.1 stands in.
bool fsr4_replay_unavailable();

// The scale pass's output rect in its target, from the draw's viewport
// (`vport`: PA_CL_VPORT_XSCALE, XOFFSET, YSCALE, YOFFSET; null without one),
// else the Resolution setting's size, else the target. Logged when it
// changes, with a warning when it is not the Resolution setting's size.
temporal::OutputRect fsr3_output_rect_locked(const RtImage* target, const float* vport);

// One line for the reports: dispatches, fallbacks and why, resets, memory.
std::string fsr3_report_locked();

}  // namespace gpu

// The presenter (host/window.cpp), for the GPU time line under the FPS
// counter (debug_upscaler_time): "FSR 3.1 1.23 ms", or empty.
std::string host_gpu_upscale_timing();
