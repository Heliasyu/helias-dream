// The host's settings as one object: what the options
// screen (F10), the port's rows in the game's System menu and
// HeliasDream-options.toml edit by name (host/options.h), read back by every
// consumer as typed, already-scaled fields. The table in options.cpp is the
// single source; it rebuilds this snapshot on every change, and a reader
// takes a copy (host_settings()) whenever host_opt_serial() has moved, or
// on the frame it needs it - the copy is small and the lock is short.
//
// The graphics half is shaped after what DS3 keeps in its CSGraphicsConfig
// (the released PC binary's RTTI names the class): per-effect switches and
// strengths the renderer applies each frame, held in one place instead of
// a copy in each consumer.
#pragma once

#include <cstdint>

struct HostSettings {
    std::uint64_t serial = 0;  // host_opt_serial() when this was built

    // Display
    bool fullscreen = false;
    bool vsync = true;
    int frame_cap = 30;   // the game's frame rate (engine/frame_rate.h): 30, 60, 90, 120; 0 is uncapped
    bool fps_counter = false;
    int fps_position = 0;            // 0 top right, 1 top left, 2 bottom right, 3 bottom left, 4 top centre
    float fps_scale = 1.0f;          // 0.75 .. 2.0
    std::uint32_t fps_colour = 0xffffffu, fps_background = 0x0f0f0fu;  // 0xRRGGBB
    float fps_opacity = 1.0f, fps_background_opacity = 0.6f;
    bool fps_border = false;
    bool fps_show_rate = true, fps_show_frame_time = false, fps_show_upscaler = false, fps_show_cpu = false, fps_show_gpu = true;
    // The picture's grade at the presenter: 1, 1, 0 are the game's own.
    float brightness = 1.0f, contrast = 1.0f, hue_degrees = 0.0f;
    // HDR output (read at the start; the levels live).
    bool hdr = false;
    float hdr_paper_white = 200.0f;  // nits
    bool hdr_peak_auto = true;
    float hdr_peak = 1000.0f;        // nits, when not from the display
    float hdr_boost = 0.25f;         // 0..1: the share of the room above paper white the highlights use
    float hdr_ui_brightness = 0.0f;  // the UI kept apart at this x paper white (host/hdr_ui.h); 0: boosted with the rest
    bool trophy_popups = true;
    bool compile_note = true;        // "Building shaders" while pipelines are built in play
    int game_language = 1;           // the PS4's language code (1 English US); HELIAS_SYSTEM_LANG wins
    unsigned submit_split = 0;       // every n-th draw a mid-job submission if the GPU ran dry; 0 off (HELIAS_SUBMIT_SPLIT wins)
    bool thread_priority = false;    // Graphics threads first (core/host_clock.h)
    bool fsr4_sharp_motion = false;  // FSR 4's balanced model at Performance and Ultra Performance
    // Texture detail (the samplers' LOD bias, added to the game's own): -1 a mip
    // sharper. NAN is Auto: log2(render / output) under FSR 3.1 / 4.1.1.
    float mip_bias = 0.0f;       // the in-game pop-up when a trophy is earned (host/trophies.h)
    int menu_key = 0;                // 0 F10, 1 Insert, 2 Home, 3 F1 (F10 always works)
    int res_width = 1920, res_height = 1080;  // the render resolution (HELIAS_RES wins)

    // Camera: multipliers on LockCamParam's values (engine/camera.cpp)
    float fov_scale = 1.0f;              // 1.0 the game's own; +5% a step
    float camera_distance_scale = 1.0f;  // 1.0 the game's own; 10% a step either way
    float camera_height_scale = 1.0f;

    // Controls
    bool mouse_menu = true;     // the pointer drives the menus through the pad
    bool mouse_camera = true;   // the mouse turns the camera outside menus
    int mouse_sens = 5;         // 0..10, DS3's: 0.2 * (s * 0.15 + 0.5) degrees a count (engine/mouse_camera.h)
    bool mouse_invert_x = false, mouse_invert_y = false;
    // While the mouse turns the camera (the keyboard and mouse used last): off
    // holds the camera's own turns as the character moves (engine/mouse_camera.h).
    bool mouse_auto_rotation = false;
    bool draw_cursor = true;    // the host draws its own pointer
    // What the game's own button prompts show: 0 follows whether a pad is
    // plugged in, 1 always the bound key, 2 always the pad's glyph
    // (engine/key_prompts.h).
    int button_prompts = 0;
    int pad_icons = 0;          // 0 Auto (the pad in use), 1 PlayStation, 2 Xbox

    // The controller (host/haptics.h): DualSense and DualSense Edge.
    int haptics = 2;                // 0 Off, 1 Rumble (the classic two motors), 2 HD (wired; Rumble without the device)
    float haptics_strength = 1.0f;  // 0..2, 1 the default
    bool adaptive_triggers = true;
    int trigger_strength = 1;       // 0 Light, 1 Medium, 2 Strong
    bool menu_haptics = true;       // a light tap for each move, decide and back in the game's menus

    // Graphics (the CSGraphicsConfig-shaped part; engine/graphics_patch.cpp,
    // host/shader_patch.cpp apply them)
    bool ssao = true;
    bool motion_blur = true;
    bool anti_alias = true;       // in effect (Auto resolved)
    bool anti_alias_auto = true;  // the setting is Auto (test7)
    bool depth_of_field = true;
    bool chromatic_aberration = true;
    bool vignette = true;
    float bloom = 1.0f;         // 1.0 the area's own glare, 0 none
    float saturation = 1.0f;    // 1.0 the area's own, 0 black and white, 2 twice
    float fog = 1.0f;           // 1.0 the area's own opacities, 0 clear
    float ao_strength = 1.0f;   // 1.0 the area's own, 0 none, 2 twice
    int lod_bias = 0;           // the game's model detail bias: -2 finest always, 0 its own, 2 coarsest
    float shadow_scale = 1.0f;  // the cascades stretched: 1.0 to 3.0

    // Startup (read once by the loader)
    bool skip_logos = false;
    bool debug_menu = false;
    bool debug_camera = false;  // patches/debug-camera.toml (Lance McDonald's free camera)
    // The PC enhancements (also read once, through config_set_enhancements)
    bool change_appearance = true;
    bool rebirth = true;
    bool five_players = true;

    // heliasdream-vk's upscaler (host/upscaler.h; docs/vk/PLAN.md). Phase 0 only
    // carries them: what reaches the screen is still FSR 1 at the Resolution
    // setting whatever they say.
    int upscaler = 1;                // 0 Off, 1 FSR 1 (heliasdream's own), 2 FSR 3.1, 3 FSR 4.1.1
    int upscale_quality = 0;         // 0 Native AA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance
    float upscale_sharpness = 0.8f;  // 0..1; 0.8 is RCAS at 0.2 stops, FSR 1's own
    bool upscale_jitter = true;      // the scene's sub-pixel jitter for FSR 3.1 / 4.1.1 (testers can turn it off)
    bool upscale_reactive = true;    // Phase 3: FSR 3.1's reactive mask from the opaque snapshot
    bool debug_motion_vectors = false;  // the motion-vector view (a no-op until Phase 2)
    bool debug_upscaler_time = false;   // the presenter's GPU timestamps, under the FPS counter
};

// A copy of the current settings.
HostSettings host_settings();
