#pragma once

// heliasdream-vk Phase 2: the temporal upscaler's inputs, made as the game draws
// (host/temporal_inputs.cpp; what they are: host/upscaler_inputs.h).
//
// Active only while the scene is split (Phase 1) and either a temporal
// upscaler (FSR 3.1, FSR 4.1.1) is chosen or Show Motion Vectors is on;
// otherwise every hook returns at its first test and the frame is drawn as
// before - Upscaler Off, FSR 1 and Native AA stay stock. The jitter only
// with a temporal upscaler.
//
//   HELIAS_JITTER=0          never jitter
//   HELIAS_MOTION_VECTORS=0  no motion pass (no jitter either)
//   HELIAS_OBJECT_MOTION=0   camera motion only (the game's velocity unused)
//   HELIAS_OPAQUE_SNAPSHOT=0 no opaque-colour snapshot
//   HELIAS_MOTION_PROBE=1    log the buffers the G-buffer draws bind (=<n>: n lines)
//
// Renderer calls are made under the renderer's lock (gpu_internal.h).

#include "host/upscaler_inputs.h"

#include <cstdint>
#include <string>

namespace gpu {

struct RtImage;

// The start of each draw: re-reads the settings once a flip. Cheap.
void temporal_frame_locked(std::uint64_t flip);
// Whether this frame watches the draws at all (scene constants, depth,
// velocity). False: nothing else here does anything.
bool temporal_watching();

// Before a draw's buffers are resolved: how many colour targets it has and
// whether a depth (four or more with a depth: a G-buffer draw, where the
// scene constants are looked for hardest - bbport's gate).
void temporal_draw_targets_locked(int color_targets, bool depth);

// A constant buffer a draw binds (VS stage 0, PS stage 1), its V#'s base
// and size: the scene constants, bone palettes (survey), the velocity
// draws' packing scale. `orb_vs` / `orb_ps` are the shaders' hashes.
// `pointer`: bound by address, not a V# (`bytes` is the shader's extent): only
// looked at for the scene constants.
// `size_known`: false for a V# with no records (test3: most GX token draws'),
// when `bytes` is the shader's extent only - a scene block's head there is
// read on to its 864 bytes when the memory is mapped.
void temporal_observe_buffer_locked(int stage, std::uint32_t orb_vs, std::uint32_t orb_ps, int index, std::uint64_t base, std::uint64_t bytes,
                                    bool pointer = false, bool size_known = true);

// Per draw, with its viewport about to be set: the jitter to add to the
// viewport's x and y (render pixels), when the draw is one of the scene's
// geometry draws. Also learns the frame's scene depth and velocity target.
bool temporal_draw_locked(const RtImage* depth, const RtImage* color0, std::uint32_t orb_vs, std::uint32_t orb_ps, std::uint32_t index_count,
                          std::uint32_t instances, bool indirect, bool blended, float* jx, float* jy);

// The scale pass is about to draw (outside any render pass after this
// returns): the frame's scene is complete. Runs the motion pass, ends the
// frame (camera history, jitter phase) and fills the frame's inputs.
// `scene_color` is the pass's source image (its first texture).
void temporal_scale_pass_locked(RtImage* scene_color, std::uint32_t output_w, std::uint32_t output_h);

// Before a blended draw into the scene colour: takes the opaque snapshot
// once a frame (only with a temporal upscaler). True when it ended the pass.
bool temporal_before_blend_locked(RtImage* color0, const RtImage* depth);

// The last frame's inputs (for the upscaler backends, Phase 3 on).
const UpscalerInputs& temporal_inputs_locked();

// One line for the 300-flip report and the exit report.
std::string temporal_report_locked();

}  // namespace gpu

// The presenter (host/window.cpp), under the renderer lock: the motion image
// of a frame no older than two flips, for Show Motion Vectors.
bool host_gpu_motion_view(VkImage* image, VkFormat* format, std::uint32_t* w, std::uint32_t* h);
