#pragma once

// heliasdream-vk's upscaler (docs/vk/PLAN.md). Phase 0 is scaffolding: the
// settings (host/settings.h) are saved, shown in F10, the System menu's PC
// Upscaling sections and the setup window, and logged - but what reaches the
// screen is heliasdream's own path, FSR 1 at the Resolution setting
// (host/fsr.cpp), whatever they say. The temporal upscalers come in Phase 3
// (FSR 3.1, FireBurn/FSR-Vulkan, linked from Phase 0 on) and Phase 4 (FSR
// 4.1.1, built from the player's own DLL).

#include "host/upscaler_presets.h"

#include <cstdint>
#include <string>

// Once at start, after the options are loaded: the chosen upscaler, preset
// and sharpness, and what this build has linked of FSR-Vulkan.
void upscaler_log_startup();

// FireBurn/FSR-Vulkan, when this build links it (cmake/fsr_vulkan.cmake):
// how many of the FSR 3.1.5 upscaler's embedded SPIR-V modules are present
// and start with the SPIR-V magic (11 when the shader tables are whole), 0
// when it is not linked. Pure: no Vulkan call.
int upscaler_fsr3_embedded_modules();
// Whether the FSR 3.1.4 / 3.1.5 upscalers' entry points are in the binary.
bool upscaler_fsr3_linked();

// Whenever a setting changes (host_opt_serial moved): logs the new upscaler
// choices once per change, so a test session's log says what was picked.
void upscaler_note_settings();

// Who does the game's scene-to-output pass - the one pass that scales the
// HDR scene up to the output size before post-processing (pixel shader
// ccbf44a6, IFreemz's "decoupled UI" finding; engine/scene_split.h). The
// renderer asks per draw of that pass; each backend is one value here, so
// FSR 3.1 (Phase 3), FSR 4.1.1 (Phase 4) and FSR 4 INT8 / DLSS / XeSS
// (Phase 7) plug in at the same point.
enum class ScenePass : int {
    Game = 0,      // the game's own pass as it is: the scene is not split (stock)
    Bilinear = 1,  // Phase 1: the same pass with a bilinear, clamped sampler
    Fsr31 = 2,     // Phase 3: FSR 3.1 writes the pass's target (host/fsr3_pass.h); its
                   // fallback, when FSR 3.1 cannot run a frame, is Bilinear's draw
};
// Any thread; cheap (atomic loads). The upscaler choice is the one the
// presenter last saw (upscaler_note_settings).
ScenePass upscaler_scene_pass();
const char* upscaler_scene_pass_name(ScenePass p);

// The motion-vector view (settings debug_motion_vectors). The presenter
// calls it each frame the view is on, saying whether it drew the motion
// image (host/temporal_inputs.h, host/present_pass.h); logs each change.
void upscaler_debug_motion_vectors_hook(bool drawn);
