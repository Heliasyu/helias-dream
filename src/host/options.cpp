#include "host/options.h"
#include "host/updater.h"
#include "host/settings.h"
#include "host/upscaler_presets.h"
#include "host/menu_i18n.h"

#include "host/bindings.h"
#include "host/overlay.h"
#include "host/window.h"
#include "core/config.h"
#include "core/host_clock.h"
#include "engine/live_resolution.h"
#include "engine/menu_pointer.h"
#include "engine/scene_split.h"
#include "host/dlss_pass.h"
#include "hle/modules.h"
#include "hle/np.h"
#include "net/account.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <mutex>
#include <thread>
#include <utility>
#include <deque>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(HELIAS_HAVE_SDL3)
#include <SDL3/SDL.h>
#endif

namespace {

struct Setting;
bool reads_on(const Setting& s, int index);  // below
int fps_from_label(const char* label);

// One setting. Everything here is a choice between a few named values: a
// toggle is a choice of two, which keeps drawing and input to one case.
struct Setting {
    const char* key;                 // what it is saved as
    const char* label;
    std::vector<const char*> values;
    int index = 0;
    const char* note = nullptr;      // shown under the row when it is selected
    bool restart = false;            // takes effect on the next run
};

// "lo", "lo+1" .. "hi": a setting in steps of one (HDR's nits). The strings
// live as long as the program.
std::vector<const char*> number_labels(int lo, int hi) {
    static std::deque<std::string> kept;
    std::vector<const char*> out;
    for (int v = lo; v <= hi; ++v) out.push_back(kept.emplace_back(std::to_string(v)).c_str());
    return out;
}

enum SettingId {
    kWindowMode,
    kVsync,
    kFrameCap,
    kFpsCounter,
    kFov,
    kCameraDistance,
    kCameraHeight,
    kMouseMenu,
    kMouseCamera,
    kMouseSens,
    kMouseInvertX,
    kMouseInvertY,
    kMouseAutoRotation,
    kDrawCursor,
    kButtonPrompts,
    kSsao,
    kMotionBlur,
    kAntiAlias,
    kDepthOfField,
    kChromaticAberration,
    kBloom,
    kVignette,
    kShadowDistance,
    kAoStrength,
    kSaturation,
    kFog,
    kModelDetail,
    kResolution,
    kSkipLogos,
    kDebugMenu,
    kDebugCamera,
    kChangeAppearance,
    kRebirth,
    kFivePlayers,
    // heliasdream-vk (docs/vk/PLAN.md): the upscaler and its debug views. Phase 0
    // saves and shows them; nothing renders differently yet (host/upscaler.h).
    kUpscaler,
    kUpscaleQuality,
    kUpscaleSharpness,
    kUpscaleReactive,
    kUpscaleJitter,
    kDebugMotionVectors,
    kDebugUpscalerTime,
    // The controller (host/haptics.h).
    kHaptics,
    kHapticsStrength,
    kAdaptiveTriggers,
    kTriggerStrength,
    kMenuHaptics,
    // engine/live_resolution.h: read once at the start.
    kLiveResolution,
    // Helia's Dream: the FPS counter's look, the picture's grade, HDR, the menu's key.
    kFpsPosition,
    kFpsSize,
    kFpsColour,
    kFpsOpacity,
    kFpsBackground,
    kFpsBackgroundOpacity,
    kFpsBorder,
    kFpsShowRate,
    kFpsShowFrameTime,
    kFpsShowUpscaler,
    kFpsShowCpu,
    kFpsShowGpu,
    kBrightness,
    kContrast,
    kHue,
    kHdr,
    kHdrPaperWhite,
    kHdrPeakAuto,
    kHdrPeak,
    kHdrBoost,
    kMenuKey,
    kTrophyPopups,
    kMipBias,
    kPadIcons,
    kCompileNote,
    kGameLanguage,
    kMenuLanguage,
    kHdrUi,
    // More > Performance (experimental): HELIAS_SUBMIT_SPLIT as a setting.
    kSubmitSplit,
    // More > Performance (experimental): Lantern Engine's "Graphics threads
    // first" and "Sharper motion (FSR 4)", as ours (core/host_clock.h,
    // host/fsr4v07_pass.h).
    kThreadPriority,
    kFsr4SharpMotion,
    // rc2: vk.1's present_pace and post_at_render switches are gone: those
    // are permanent (env vars for debugging only); a saved key is ignored.
    kSettingCount,
};

// A row on screen: a section header, a setting, or the close button.
struct Row {
    // Text: a field the player types (field 0 the account name, 1 a recovery
    // code). Action: a button (field 0 link with Discord, 1 create an account
    // on this PC, 2 recover, 3 website code, 4 sign out). Info: a line the
    // host writes (field 0 who is signed in, 1 the code in play, if any).
    // Fields 10 and up are the UPDATES rows (host/updater.h): Info 10 the
    // version and what the check found, 11 why it failed or the newer
    // release's page; Action 10 check, 11 open that page in the browser.
    enum Kind { Header, Option, Close, Text, Action, Info } kind;
    const char* text;
    int setting = -1;
    int field = -1;
};

Setting g_set[kSettingCount] = {
    {"window_mode", "Window mode", {"Windowed", "Fullscreen"}, 0,
     "Fullscreen is a borderless window over the whole display: the desktop resolution, no mode switch, and overlays and pop-ups show over it.", false},
    {"vsync", "V-Sync", {"On", "Off"}, 0,
     "Off presents as soon as a frame is ready, and can tear.", false},
    // The game's own pace (engine/frame_rate.h), the same five the game's Frame
    // Rate pick list offers (engine/option_menu.cpp). The key is the old Frame
    // cap's, so a file keeps its 30 or 60; an older file's 90, 120, 144 and
    // Off, which ran the game at 60, read as 60 (host_options_load).
    {"frame_cap", "Frame rate", {"30", "60", "90", "120", "Uncapped"}, 0,
     "30 is the game's own pace; 60 and 90 run its logic at that rate; 120 and Uncapped step it by the frame time "
     "(120: at most 120 fps; Uncapped: the display's refresh rate with V-Sync on, up to 240 off).",
     true},
    // What reaches the screen, counted by the presenter (host/window.cpp).
    {"fps_counter", "FPS counter", {"Off", "On"}, 0,
     "Frames per second actually presented, in the top right corner.", false},
    // The follow camera's vertical field of view, LockCamParam's camFovY
    // (43 degrees in most areas) widened 5% a step (engine/camera.h).
    {"fov", "Field of view", {"+0%", "+5%", "+10%", "+15%", "+20%", "+25%", "+30%", "+35%", "+40%", "+45%", "+50%"}, 0,
     "How wide the camera sees. +0% is the game's own; +50% is about 97 degrees across.", false},
    // The same row's camDistTarget and chrOrgOffset_Y: 5 the game's own, 10%
    // a step either way.
    {"camera_distance", "Camera distance", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 5,
     "How far behind the character the camera sits. 5 is the game's own.", false},
    {"camera_height", "Camera height", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 5,
     "How high above the character the camera looks. 5 is the game's own.", false},
    // On, as in DS3: a click only reaches the menus while one is open
    // (engine/menu_pointer.h), so it cannot swing a weapon in the world.
    {"mouse_menu", "Mouse in menus", {"Off", "On"}, 1,
     "Click confirms, right-click goes back, the wheel moves the cursor.", false},
    // On by default, and it only *disables* the automatic behaviour: the
    // mouse turns the camera whenever a menu is not open, which is what DS3
    // does and what a PC player expects. Off hands the camera back to the pad.
    {"mouse_camera", "Mouse camera", {"On", "Off"}, 0,
     "The mouse turns the camera outside menus. Off leaves it to the pad.", false},
    // 0..10, the range of the game's own Camera Sensitivity slider, which is
    // the widget the PC Controls screen draws it with, and DS3's: the same
    // angle a count at each step (engine/mouse_camera.h), 5 its default.
    {"mouse_sens", "Mouse sensitivity", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 5,
     "How far a given mouse movement turns the camera.", false},
    {"mouse_invert_x", "Invert mouse X", {"Off", "On"}, 0,
     "Reverses the mouse's horizontal camera movement.", false},
    {"mouse_invert_y", "Invert mouse Y", {"Off", "On"}, 0,
     "Reverses the mouse's vertical camera movement.", false},
    // Off by default: with the mouse the camera stays where it is put, as
    // the community's "Disable Camera Auto Rotation" patch has it - but only
    // while the keyboard and mouse were used last; a controller gets the
    // game's own camera back (engine/mouse_camera.h).
    {"mouse_auto_rotation", "Mouse auto-rotation", {"Off", "On"}, 0,
     "Off: while the mouse turns the camera, it stays where you put it instead of swinging behind you as you move. A controller always has the game's own camera.", false},
    {"draw_cursor", "Draw the pointer", {"On", "Off"}, 0,
     "The host draws it; the game has no cursor of its own.", false},
    // The game's key guide and its tutorial notes name a pad button as an
    // image tag; the port rewrites the tag into the key bound to it
    // (engine/key_prompts.h). Auto follows the device last used (host_input_device).
    {"button_prompts", "Button prompts", {"Auto", "Keyboard", "Controller"}, 0,
     "What the game's own prompts show. Auto follows the device you last used, keys or controller.", false},
    // The post-processing switches. They follow their settings live
    // (engine/graphics_patch.cpp, host/shader_patch.h).
    {"ssao", "Ambient occlusion", {"On", "Off"}, 0,
     "Screen-space ambient occlusion. Off is faster.", false},
    // Not a byte patch: the blur shader passes every pixel through
    // (host/shader_patch.h).
    {"motion_blur", "Motion blur", {"On", "Off"}, 0,
     "The blur on fast camera and character movement.", false},
    // test7: Auto (the default) is Off while FSR 4.1.1 upscales - it
    // smooths edges itself, and the game's AA pass costs ~0.35 ms at 4K -
    // and On otherwise (FSR 3.1, FSR 1, native, or FSR 4.1.1 not running).
    {"anti_alias", "Game anti-aliasing", {"On", "Off", "Auto"}, 2,
     "The game's own AA pass. Auto: off while FSR 4.1.1, FSR 4 or DLSS upscales (a quality below Native AA: they smooth edges themselves) and with Native TAA, on otherwise.", false},
    {"depth_of_field", "Depth of field", {"On", "Off"}, 0,
     "Blurs distant scenery; indoors there is rarely anything far enough to blur.", false},
    {"chromatic_aberration", "Chromatic aberration", {"On", "Off"}, 0,
     "The colour fringing at the edges of the frame.", false},
    // Two of the area's draw parameters (param/drawparam), scaled or cleared
    // as the game hands them to YEBIS each frame - engine/graphics_patch.cpp.
    // 10 is the area's own glare; 0 is none.
    {"bloom", "Bloom", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 10,
     "The glow around bright light. 10 is the game's own; lower dims it.", false},
    {"vignette", "Vignette", {"On", "Off"}, 0,
     "The darkened frame edges an area may use. Most areas have none, so Off often changes nothing; where there is one it is usually faint.", false},
    // The area's shadow cascades, stretched past the first split: 1 + 0.2 a
    // step, so the game's slider row (0..10) is the same setting.
    {"shadow_distance", "Shadow distance",
     {"x1.0", "x1.2", "x1.4", "x1.6", "x1.8", "x2.0", "x2.2", "x2.4", "x2.6", "x2.8", "x3.0"}, 0,
     "How far the sun and moon cast shadows. Farther spreads each shadow map wider.", false},
    // Two more of the area's numbers, scaled: 5 is the area's own, 0 none, 10
    // twice it.
    {"ao_strength", "AO strength", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 5,
     "How dark ambient occlusion makes corners and contact. 5 is the game's own.", false},
    {"saturation", "Saturation", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 5,
     "Colour. 5 is the game's own; 0 is black and white.", false},
    // The fog table's opacities (engine/graphics_patch.cpp): 10 the area's own.
    {"fog", "Fog", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 10,
     "How thick distance and height fog are. 10 is the game's own; 0 clears it.", false},
    // The game's level-of-detail bias (engine/graphics_patch.cpp): Full keeps
    // every model at its finest mesh, Low and Lowest move each to a coarser
    // one sooner. Lowest is the community "model LOD 2" (-8% GPU, models
    // visibly simpler up close).
    {"model_detail", "Model detail", {"Full", "Normal", "Low", "Lowest"}, 1,
     "Normal is the game's own. Low and Lowest swap in simpler models sooner; Full never does.", false},
    // The render resolution. res_width and res_height are two int32s the eboot
    // never writes, so this is a data patch and every pass scales together -
    // see engine/graphics_patch.cpp. The game's pick list shows six entries
    // and scrolls through more; the ultrawide ones and
    // the Steam Deck's 16:10 go after the 16:9 ones so a saved entry keeps its
    // place, and the other shapes are engine/live_resolution.cpp's. 960x600
    // and 1024x640 are the Deck's screen at 75% and 80%: it holds 60 fps there
    // (pixel-bound at 1280x800, ~45), and the presenter upscales by FSR 1.
    {"resolution", "Resolution", {"1280x720", "1600x900", "1920x1080", "2560x1440", "3200x1800",
                                  "3840x2160", "2560x1080", "3440x1440", "5120x2160", "3840x1080",
                                  "5120x1440", "1280x800", "960x600", "1024x640"}, 2,
     "What the game renders at. 1920x1080 is what it shipped with; wide and 16:10 screens are filled, the HUD kept 16:9. "
     "The largest sizes can need a restart: the memory for them is set aside when the game starts.",
     true},
    {"skip_logos", "Skip company logos", {"Off", "On"}, 0,
     "The three logos before the title screen, and the warning that the last session did not end with Exit Game.", true},
    // The old switch for the developers' debug menu, which the Debug Menu
    // plugin replaced (engine/debug_menu.h): no row shows it, and an "On" an
    // older heliasdream saved turns the plugin on once (main.cpp).
    {"debug_menu", "Debug menu", {"Off", "On"}, 0, "Replaced by the Debug Menu plugin.", true},
    // Lance McDonald's "Restore Debug Camera" (patches/debug-camera.toml). Its
    // code takes the place of a debug-only load-test step, so the debug menu's
    // LOAD TEST and DUNGEON MOVEMAP TEST crash while it is on.
    {"debug_camera", "Debug camera", {"Off", "On"}, 0,
     "Hold Interact (E) and press L3 (Left Ctrl) to cycle the free camera's modes.", true},
    // The PC enhancements: additions to the game itself, each read once at
    // start (main.cpp hands them to config_set_enhancements), all on unless
    // HeliasDream.toml's [world] says otherwise. Off, all three, is the game as it
    // shipped. (The Old Hunters, a fourth in v0.2.9, is the patch old-hunters.)
    {"change_appearance", "Hunter's Dream mirror", {"On", "Off"}, 0,
     "The Dream's mirror opens the appearance editor (the unused \"Put on Disguise\").", true},
    {"rebirth", "Rebirth at the Altar", {"On", "Off"}, 0,
     "With the Yharnam Stone, the Altar of Despair resets level and attributes.", true},
    {"five_players", "Five players", {"On", "Off"}, 0,
     "Two invaders in Mensis, Mergo's Loft, the Frontier and the DLC; all players need it.", true},
    // heliasdream-vk's upscaler (host/upscaler.h). FSR 1 is what heliasdream always did:
    // the presenter's EASU + RCAS when the game renders smaller than the
    // window (host/fsr.cpp). Phase 1: with an upscaler on, the quality renders the
    // scene smaller than the output (engine/scene_split.h).
    {"upscaler", "Upscaler", {"Off", "FSR 1", "FSR 3.1", "FSR 4.1.1", "Native TAA", "FSR 4", "DLSS"}, 1,
     "FSR 1 is simple and light. FSR 3.1 is temporal, from Upscaling quality's size. FSR 4 and FSR 4.1.1 are AMD's machine-learning upscalers, sharper; they need their assets (the launcher sets them up), else FSR 3.1 runs. Native TAA is anti-aliasing at the full resolution. DLSS needs an NVIDIA RTX GPU and applies from the next start; elsewhere FSR 3.1 runs.",
     false},
    // Output size / render size: 1.0, 1.5, 1.7, 2.0, 3.0 (AMD's presets).
    {"upscale_quality", "Upscaling quality",
     {"Native AA", "Quality", "Balanced", "Performance", "Ultra Performance"}, 0,
     "The 3D scene's share of the Resolution: Native AA 100%, Quality 67%, Balanced 58%, Performance 50%, Ultra Performance 33%. "
     "Menus and text stay at the Resolution. Changes live, a moment after the menu closes.",
     false},
    // 0..1 like FSR 3's sharpness; 0.8 is RCAS at 0.2 stops, what FSR 1 uses.
    {"upscale_sharpness", "Upscaling sharpness", {"0.0", "0.1", "0.2", "0.3", "0.4", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0"}, 8,
     "How much the temporal upscalers sharpen (0 turns it off). 0.8 matches FSR 1's own.", false},
    // heliasdream-vk Phase 3: the reactive mask (host/temporal_inputs.h).
    {"upscale_reactive", "Upscaler reactive mask", {"Off", "On"}, 1,
     "FSR 3.1: marks particles, fog, Blood Echoes, the lantern's glow and other transparent effects so they do not smear or ghost. Off for comparison.",
     false},
    // heliasdream-vk: the scene's sub-pixel jitter (host/temporal_inputs.h), for testers.
    {"upscale_jitter", "Upscaler jitter", {"Off", "On"}, 1,
     "The 3D scene shifts by a fraction of a pixel each frame so a temporal upscaler can rebuild detail. Off for comparison: a softer, more aliased picture.",
     false},
    {"debug_motion_vectors", "Show motion vectors", {"Off", "On"}, 0,
     "Debug: draws the scene's motion vectors over the picture (red and green: sideways and up-down movement; blue: moving objects).", false},
    {"debug_upscaler_time", "Show upscaler GPU time", {"Off", "On"}, 0,
     "Debug: the upscaler's and the presenter's GPU time under the FPS counter, and in the log.",
     false},
    // The controller (host/haptics.h).
    {"haptics", "Haptics", {"Off", "Rumble", "HD"}, 2,
     "HD: a DualSense or DualSense Edge plugged in by USB feels the game's rumble and its sound effects (hits, shots, footsteps, doors) "
     "through its haptic motors. Over Bluetooth, or with another controller, HD is the classic Rumble.",
     false},
    {"haptics_strength", "Haptics strength", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 5,
     "How strong HD haptics are. 5 is the default, 0 nothing, 10 twice as strong.", false},
    {"adaptive_triggers", "Adaptive triggers", {"On", "Off"}, 0,
     "DualSense and DualSense Edge: L2 (your gun) has a trigger break; R2 resists lightly and hums as a heavy attack charges. "
     "Relaxed in loading screens and menus. With an Edge's trigger stops set short, the break may be past the stop.",
     false},
    {"trigger_strength", "Trigger strength", {"Light", "Medium", "Strong"}, 1,
     "How hard the adaptive triggers push back.", false},
    {"menu_haptics", "Menu haptics", {"On", "Off"}, 0,
     "HD: a light tap as the cursor moves through the title menu, the pause menu, the inventory and the System menu, "
     "and a firmer one for a decide or back.",
     false},
    // On: the Resolution setting applies in the game (engine/live_resolution.h). What it costs
    // is memory set aside at the start for the largest display's size - display buffers and
    // the render-target heap - and, when the game starts below that size, buffers larger than
    // the picture that are cleared every frame. Off sizes everything for the starting
    // resolution; a change applies at the next start.
    {"live_resolution", "Live resolution change", {"On", "Off"}, 0,
     "On: a new Resolution applies in the game. Off: it applies at the next start, and the memory and buffers kept "
     "ready for larger sizes are not set aside (a little faster when the game runs below your display's size).",
     true},
    // The FPS counter (host/window.cpp): where, how large, its colours, and what it shows.
    {"fps_position", "Position", {"Top right", "Top left", "Bottom right", "Bottom left", "Top centre"}, 0,
     "Where the FPS counter sits on the screen.", false},
    {"fps_size", "Size", {"75%", "100%", "125%", "150%", "175%", "200%"}, 1, "How large the FPS counter's text is.", false},
    {"fps_colour", "Text colour", {"White", "Yellow", "Green", "Cyan", "Red", "Orange", "Pink", "Black"}, 0,
     "The FPS counter's text colour.", false},
    {"fps_opacity", "Text opacity", {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 9, "How solid the FPS counter's text is.", false},
    {"fps_background", "Background", {"Dark grey", "Black", "Dark red", "Dark blue", "White"}, 0,
     "The colour of the box behind the FPS counter.", false},
    {"fps_background_opacity", "Background opacity", {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 6, "How solid the box behind the FPS counter is; 0% hides it.", false},
    {"fps_border", "Border", {"Off", "On"}, 0, "A thin outline round the FPS counter's box.", false},
    {"fps_show_rate", "Show frame rate", {"On", "Off"}, 0, "Frames per second actually shown.", false},
    {"fps_show_frame_time", "Show frame time", {"Off", "On"}, 0, "Milliseconds a frame, averaged like the rate.", false},
    {"fps_show_upscaler", "Show upscaler", {"Off", "On"}, 0, "The upscaler and quality preset running.", false},
    {"fps_show_cpu", "Show CPU usage", {"Off", "On"}, 0, "How much of the CPU the game uses (100% is every core).", false},
    {"fps_show_gpu", "Show GPU usage", {"On", "Off"}, 0, "How busy the game keeps the GPU.", false},
    // The picture's grade, at the presenter (host/present_pass.cpp): the game's
    // picture only, the menus and counter as they are.
    {"brightness", "Brightness", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 5,
     "The picture's brightness. 5 is the game's own; each step is about 10% lighter or darker.", false},
    {"contrast", "Contrast", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 5,
     "The picture's contrast around mid grey. 5 is the game's own.", false},
    {"hue", "Hue shift", {"-180", "-165", "-150", "-135", "-120", "-105", "-90", "-75", "-60", "-45", "-30", "-15", "0", "15", "30", "45", "60", "75", "90", "105", "120", "135", "150", "165", "180"}, 12, "Turns every colour round the colour wheel, in degrees. 0 is the game's own.", false},
    // HDR output (host/window.cpp's swapchain, host/present_pass.cpp's encode):
    // the game's SDR frame on an HDR10 display, its highlights expanded.
    {"hdr", "HDR output", {"Off", "On"}, 0,
     "The picture on an HDR screen in HDR10, with brighter highlights. Needs HDR turned on for the display in your "
     "desktop's settings (KDE: Display Configuration).",
     false},
    {"hdr_paper_white", "Paper white", number_labels(10, 500), 190, "How bright the game's white is on an HDR screen, in nits.", false},
    {"hdr_peak_auto", "Peak from the display", {"On", "Off"}, 0,
     "On: the peak brightness the display reports (its EDID). Off: Peak brightness below.", false},
    {"hdr_peak", "Peak brightness", number_labels(10, 4000), 990,
     "Your display's peak brightness, in nits, when it is not taken from the display.", false},
    {"hdr_boost", "Highlight boost", {"Off", "Low", "Medium", "High", "Max"}, 1,
     "How far the brightest parts of the picture (lamps, fire, the moon) rise above paper white. Off keeps the SDR look.", false},
    // The key that opens this menu (host/window.cpp reads it).
    {"menu_key", "Menu key", {"F10", "Insert", "Home", "F1"}, 0, "The key that opens this menu. F10 always works too.", false},
    {"trophy_popups", "Trophy pop-ups", {"On", "Off"}, 0,
     "A card in the top right corner when you earn a trophy, with its grade and your progress.", false},
    // The samplers' LOD bias (host/textures.cpp): with the scene rendered below
    // the output, textures fall to smaller mips sooner than they would at the
    // output's size; Auto puts back what the render scale took (log2 of it).
    {"mip_bias", "Texture detail", {"Auto", "-2.0", "-1.5", "-1.0", "-0.5", "0.0", "+0.5", "+1.0"}, 5,
     "The texture sharpness bias. Auto matches the upscaler's render scale (sharper textures when upscaling; can shimmer). "
     "0.0 is the game's own; below 0 sharper, above 0 softer.",
     false},
    // engine/key_prompts.h: with a controller's icons showing, the game's own
    // (PlayStation) or an Xbox pad's buttons by name. Auto follows the pad.
    {"pad_icons", "Controller icons", {"Auto", "PlayStation", "Xbox"}, 0,
     "The button icons a controller's prompts show. Auto: PlayStation's with a PlayStation controller, the Xbox buttons by name with any other.", false},
    // host/window.cpp's pump: a small note under the FPS counter's corner while
    // a frame waits on a pipeline compile (perf::compiling_recently).
    {"compile_note", "Building shaders note", {"On", "Off"}, 0,
     "\"Building shaders\" in a corner while the game builds shaders for something it shows for the first time (a brief stutter is normal then).", false},
    // hle/system.cpp's SCE_SYSTEM_SERVICE_PARAM_ID_LANG: the game's text and
    // voices in that language (the disc carries their message bundles).
    {"game_language", "Game language", {"English", "French", "Spanish", "German", "Italian", "Russian", "Japanese"}, 0,
     "The game's own text and voices. Takes effect at the next start.", true},
    // host/menu_i18n.h: this menu's own language; each language by its own name.
    {"menu_language", "Menu language", {"English", "Русский", "Español", "Português (Brasil)", "Français", "Deutsch", "Italiano", "Polski", "Türkçe"}, 0,
     "The language of this menu. The game's own text follows Game language.", false},
    // host/hdr_ui.h: HDR with the UI told apart from the picture, at this
    // brightness (times paper white); the first value leaves it expanded.
    {"hdr_ui_brightness", "UI brightness",
     {"Like the picture", "50%", "60%", "70%", "80%", "90%", "100%", "110%", "120%", "130%", "140%", "150%", "160%", "170%", "180%", "190%", "200%"}, 0,
     "HDR: the HUD, menus and subtitles at this share of paper white, kept out of the highlight boost. Like the picture: they are boosted as the rest.", false},
    // test7's HELIAS_SUBMIT_SPLIT (host/gpu.cpp submit_split_point): every n-th
    // draw, if the GPU has run dry, the recording so far is submitted mid-job.
    // Live; the env var wins. Launcher only (More > Performance), no F10 row.
    {"submit_split", "Submit split", {"Off", "32", "64", "128", "256"}, 0,
     "Experimental: every N draws, if the GPU has nothing left to do, hand it the work recorded so far instead of waiting for the job's end.", false},
    // core/host_clock.h host_threads_set_priority. Live. Launcher only.
    {"thread_priority", "Graphics threads first", {"Off", "On"}, 0,
     "Experimental: the engine's graphics threads and the game's main and render threads run at a higher CPU priority than its other threads.", false},
    // host/fsr4v07_pass.h: Performance and Ultra Performance use FSR 4's
    // balanced model. Live (the context is remade). Launcher only.
    {"fsr4_sharp_motion", "Sharper motion (FSR 4)", {"Off", "On"}, 0,
     "Experimental: FSR 4 uses its balanced model at Performance and Ultra Performance - fewer trails behind moving characters, a little more GPU time.", false},
};

// rc2: fewer sections, by what a setting is about (vk1-dream's F10 report:
// UPSCALING and the resolution were apart, and the menu ran to eight
// sections). Optimizations have no rows: they are permanent, and the
// capture (F11 / bbtest snap) shows how they do.
const Row g_rows[] = {
    {Row::Header, "DISPLAY"},
    {Row::Option, nullptr, kResolution},
    {Row::Option, nullptr, kWindowMode},
    {Row::Option, nullptr, kVsync},
    {Row::Option, nullptr, kFrameCap},
    {Row::Option, nullptr, kLiveResolution},
    {Row::Option, nullptr, kMenuKey},
    {Row::Header, "FPS COUNTER"},
    {Row::Option, nullptr, kFpsCounter},
    {Row::Option, nullptr, kFpsPosition},
    {Row::Option, nullptr, kFpsSize},
    {Row::Option, nullptr, kFpsColour},
    {Row::Option, nullptr, kFpsOpacity},
    {Row::Option, nullptr, kFpsBackground},
    {Row::Option, nullptr, kFpsBackgroundOpacity},
    {Row::Option, nullptr, kFpsBorder},
    {Row::Option, nullptr, kFpsShowRate},
    {Row::Option, nullptr, kFpsShowFrameTime},
    {Row::Option, nullptr, kFpsShowUpscaler},
    {Row::Option, nullptr, kFpsShowCpu},
    {Row::Option, nullptr, kFpsShowGpu},
    {Row::Option, nullptr, kCompileNote},
    {Row::Header, "HDR"},
    {Row::Option, nullptr, kHdr},
    {Row::Option, nullptr, kHdrPaperWhite},
    {Row::Option, nullptr, kHdrPeakAuto},
    {Row::Option, nullptr, kHdrPeak},
    {Row::Option, nullptr, kHdrBoost},
    {Row::Option, nullptr, kHdrUi},
    {Row::Header, "UPSCALING"},
    {Row::Option, nullptr, kUpscaler},
    {Row::Option, nullptr, kUpscaleQuality},
    {Row::Option, nullptr, kUpscaleSharpness},
    {Row::Option, nullptr, kUpscaleReactive},
    {Row::Option, nullptr, kUpscaleJitter},
    {Row::Option, nullptr, kAntiAlias},
    {Row::Option, nullptr, kMipBias},
    {Row::Header, "GRAPHICS"},
    {Row::Option, nullptr, kSsao},
    {Row::Option, nullptr, kMotionBlur},
    {Row::Option, nullptr, kDepthOfField},
    {Row::Option, nullptr, kChromaticAberration},
    {Row::Option, nullptr, kVignette},
    {Row::Option, nullptr, kBloom},
    {Row::Option, nullptr, kFog},
    {Row::Option, nullptr, kAoStrength},
    {Row::Option, nullptr, kSaturation},
    {Row::Option, nullptr, kBrightness},
    {Row::Option, nullptr, kContrast},
    {Row::Option, nullptr, kHue},
    {Row::Option, nullptr, kShadowDistance},
    {Row::Option, nullptr, kModelDetail},
    {Row::Header, "CAMERA"},
    {Row::Option, nullptr, kFov},
    {Row::Option, nullptr, kCameraDistance},
    {Row::Option, nullptr, kCameraHeight},
    {Row::Header, "CONTROLS"},
    {Row::Option, nullptr, kMouseMenu},
    {Row::Option, nullptr, kMouseCamera},
    {Row::Option, nullptr, kMouseSens},
    {Row::Option, nullptr, kMouseInvertX},
    {Row::Option, nullptr, kMouseInvertY},
    {Row::Option, nullptr, kMouseAutoRotation},
    {Row::Option, nullptr, kDrawCursor},
    {Row::Option, nullptr, kButtonPrompts},
    {Row::Option, nullptr, kPadIcons},
    {Row::Header, "CONTROLLER"},
    {Row::Option, nullptr, kHaptics},
    {Row::Option, nullptr, kHapticsStrength},
    {Row::Option, nullptr, kAdaptiveTriggers},
    {Row::Option, nullptr, kTriggerStrength},
    {Row::Option, nullptr, kMenuHaptics},
    {Row::Header, "GAME"},
    {Row::Option, nullptr, kChangeAppearance},
    {Row::Option, nullptr, kRebirth},
    {Row::Option, nullptr, kFivePlayers},
    {Row::Option, nullptr, kSkipLogos},
    {Row::Option, nullptr, kTrophyPopups},
    {Row::Option, nullptr, kGameLanguage},
    {Row::Option, nullptr, kMenuLanguage},
    // The private server's account. No passwords: link this PC to
    // the account signed into on the website with Discord, or make one held
    // by this PC with a recovery code. The token is kept in the options file.
    {Row::Header, "ONLINE"},
    {Row::Info, nullptr, -1, 0},
    {Row::Info, nullptr, -1, 1},
    {Row::Action, "Link with Discord", -1, 0},
    {Row::Text, "Name", -1, 0},
    {Row::Action, "Create account on this PC", -1, 1},
    {Row::Text, "Recovery code", -1, 1},
    {Row::Action, "Recover account", -1, 2},
    {Row::Action, "Website sign-in code", -1, 3},
    {Row::Action, "Sign out", -1, 4},
    {Row::Info, nullptr, -1, 10},
    {Row::Info, nullptr, -1, 11},
    {Row::Action, "Check for updates", -1, 10},
    {Row::Action, "Open the release page", -1, 11},
    {Row::Header, "DEBUG"},
    {Row::Option, nullptr, kDebugMotionVectors},
    {Row::Option, nullptr, kDebugUpscalerTime},
    {Row::Option, nullptr, kDebugCamera},
    {Row::Close, "Close"},
};
constexpr int kRowCount = static_cast<int>(sizeof(g_rows) / sizeof(g_rows[0]));

std::atomic<bool> g_open{false};
int g_sel = 1;  // the first selectable row
// Where each row was drawn last frame, so the pointer hit-tests what it sees
// rather than what the layout would be at some other display size.
float g_row_y[kRowCount] = {};
float g_row_h = 0.0f;
float g_row_xs[kRowCount] = {}, g_row_ws[kRowCount] = {};  // each row's left and width (two columns)
// And where an option row's value was: from just left of its "<" (g_row_vl)
// to its middle (g_row_vm) is the side a click steps back on.
float g_row_vl[kRowCount] = {}, g_row_vm[kRowCount] = {};
bool g_laid_out = false;
bool g_dirty = false;  // something changed since the last save

// What every setting starts with: the table's own indices, taken before
// anything is loaded over them, then whatever HeliasDream.toml sets for it - the
// frame cap, the logos, and a package's resolution and window mode (the
// Steam Deck kit's) - so the menus' Defaults go back to those.
// The table is indexed by SettingId: its entries must be in the enum's order.
// A few checked here, so an entry put in the wrong place fails at once.
const bool g_table_order_ok = [] {
    const auto at = [](int id, const char* key) {
        if (std::strcmp(g_set[id].key, key) == 0) return true;
        std::fprintf(stderr, "options: g_set[%d] is \"%s\", not \"%s\" - the table and SettingId disagree\n", id, g_set[id].key, key);
        std::abort();
    };
    return at(kUpscaler, "upscaler") && at(kHaptics, "haptics") && at(kHdr, "hdr") && at(kMipBias, "mip_bias") && at(kPadIcons, "pad_icons") &&
           at(kCompileNote, "compile_note") && at(kGameLanguage, "game_language") && at(kMenuLanguage, "menu_language") &&
           at(kHdrUi, "hdr_ui_brightness") && at(kSubmitSplit, "submit_split") &&
           at(kThreadPriority, "thread_priority") && at(kFsr4SharpMotion, "fsr4_sharp_motion");
}();

std::array<int, kSettingCount> g_default_index = [] {
    std::array<int, kSettingCount> a{};
    for (int id = 0; id < kSettingCount; ++id) a[static_cast<std::size_t>(id)] = g_set[id].index;
    return a;
}();

// A resolution change made while the game runs (engine/live_resolution.h)
// asks to be kept, the way Dark Souls III's ApplyScreenSettingJob does: the
// new size at once, then the game's own yes/no dialog, ten seconds, or it
// goes back. While it waits, g_res_prev is the entry it goes back to and the
// file keeps.
int g_res_prev = -1;
bool g_res_reverting = false;  // the change back itself asks nothing

bool selectable(int r) {
    return r >= 0 && r < kRowCount && g_rows[r].kind != Row::Header && g_rows[r].kind != Row::Info;
}

// The sections: each header row starts one, shown as a tab.
int g_sections[16];
int g_section_count = [] {
    int n = 0;
    for (int r = 0; r < kRowCount && n < 16; ++r)
        if (g_rows[r].kind == Row::Header) g_sections[n++] = r;
    return n;
}();
int g_section_sel[16] = {};  // the row last selected in each section

int section_of(int r) {
    int s = 0;
    for (int i = 0; i < g_section_count; ++i)
        if (g_sections[i] <= r) s = i;
    return s;
}

// The notes of the action rows (by field: 0..4 the account's, 10.. the
// updates'), and the key guide: file scope, so --menu-strings lists them.
const char* const kNotes[] = {
    "Shows a code and opens the website: sign in there with Discord and approve it.",
    "Makes an account held by this PC under the name above; write down its recovery code.",
    "The name and recovery code above give this PC the account back (other PCs are signed out).",
    "A one-time code to sign in on the website's account page with this PC's account.",
    "Forgets the token; the game is signed out if the server requires an account.",
};
const char* const kUpdateNotes[] = {
    "Asks GitHub for the newest release.",
    "Opens the newer release's page in your browser, to download it from there.",
};
const char* const kKeys[][2] = {
    {"Up / Down", "move"}, {"Left / Right", "change"}, {"Q / E", "section"}, {"Enter", "toggles"}, {"Esc / F10", "closes"}};
// The menu's other words.
const char* const kOtherStrings[] = {"Settings apply live in the game", "(type here)", "RESTART", "Saves on close.",
                                     "Enter or click to type; Enter keeps it, Escape drops it.", "Fullscreen (borderless)"};

// A tab's name: the header's words, capitalised as a title.
const char* section_name(int s) {
    static std::string names[16];
    std::string& n = names[s];
    if (n.empty()) {
        const char* h = g_rows[g_sections[s]].text;
        bool start = true;
        for (const char* c = h; *c; ++c) {
            const char ch = *c;
            n += start ? ch : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            start = ch == ' ';
        }
        if (n == "Fps counter") n = "FPS Counter";
        if (n == "Hdr") n = "HDR";
    }
    return menu_tr(n.c_str());
}

// Up and Down: the next selectable row inside the section, round its end.
void move_in_section(int step) {
    const int s = section_of(g_sel);
    const int first = g_sections[s] + 1, end = s + 1 < g_section_count ? g_sections[s + 1] : kRowCount;
    const int n = end - first;
    if (n <= 0) return;
    int r = g_sel;
    for (int i = 0; i < n; ++i) {
        r = first + ((r - first + step) % n + n) % n;
        if (selectable(r)) { g_sel = r; break; }
    }
    g_section_sel[s] = g_sel;
}

// A tab: the row last chosen there, else its first selectable one.
void go_section(int s) {
    s = ((s % g_section_count) + g_section_count) % g_section_count;
    g_section_sel[section_of(g_sel)] = g_sel;
    const int first = g_sections[s] + 1, end = s + 1 < g_section_count ? g_sections[s + 1] : kRowCount;
    int r = g_section_sel[s];
    if (r < first || r >= end || !selectable(r)) {
        r = first;
        while (r < end && !selectable(r)) ++r;
    }
    if (r < end) g_sel = r;
}

// The controller's selected setting (host_options_pad): the row whose value
// takes Left and Right, -1 for none. Only while it is still the row selected
// (the pointer may have moved the selection), and cleared by moving off it, a
// tab, back, a key, or closing.
int g_pad_edit_row = -1;
bool pad_editing() { return g_pad_edit_row >= 0 && g_pad_edit_row == g_sel; }

// Left / Right while browsing: the row level with this one in the column on
// that side, as the last frame laid them out.
void move_across(int dir) {
    const int s = section_of(g_sel);
    const int first = g_sections[s] + 1, end = s + 1 < g_section_count ? g_sections[s + 1] : kRowCount;
    const float cx = g_row_xs[g_sel], cy = g_row_y[g_sel];
    int best = -1;
    float best_d = 1e9f;
    for (int r = first; r < end; ++r) {
        if (r == g_sel || !selectable(r) || g_row_y[r] < -9000.0f) continue;
        const float dx = g_row_xs[r] - cx;
        if (dir > 0 ? dx < 1.0f : dx > -1.0f) continue;
        const float d = std::fabs(g_row_y[r] - cy) + std::fabs(dx) * 0.01f;
        if (d < best_d) {
            best_d = d;
            best = r;
        }
    }
    if (best >= 0) {
        g_sel = best;
        g_section_sel[s] = g_sel;
    }
}

// Whether the resolution entry `index` changes while the game runs. One past
// what this run's render-target heap was grown for at its start (5120x2160
// after a smaller start) applies on the next run like any restart setting.
bool res_live(int index) {
    unsigned w = 0, h = 0;
    return live_resolution_available() && std::sscanf(g_set[kResolution].values[static_cast<std::size_t>(index)], "%ux%u", &w, &h) == 2 &&
           live_resolution_can(w, h);
}

// Whether the value `index` of setting `id` waits for the next start - the
// row's RESTART badge. Resolution: past what this run's heap was grown for.
// Upscaler and Upscaling quality change live, except into a split scene when
// the run started unsplit (its display buffers are not the output's size)
// and into DLSS when the device was made without NGX's extensions.
bool waits_for_restart(int id, int index) {
    if (id == kResolution) return g_set[id].restart && !res_live(index);
    if (id == kUpscaler || id == kUpscaleQuality) {
        const int up = id == kUpscaler ? index : g_set[kUpscaler].index;
        const int quality = id == kUpscaleQuality ? index : g_set[kUpscaleQuality].index;
        if (up == upscaler::kDlss && !gpu::dlss_device_ready()) return true;
        const bool splits = up != 0 && (quality > 0 || up == upscaler::kTaa);
        return splits && !scene_split_live();
    }
    return g_set[id].restart;
}

// The account rows. The fields are typed through the window's text entry
// (host_text_entry_*), one at a time; the calls run on their own thread so
// the screen keeps drawing, and their outcome is the two status lines.
std::string g_field[2];       // name, recovery code (never saved)
int g_edit_field = -1;        // which field the text entry is on, -1 none
std::mutex g_acct_mu;
std::string g_acct_status;    // the last outcome, for the first status line
std::string g_acct_detail;    // a code the player needs (link, recovery, website)
std::atomic<bool> g_acct_busy{false};
std::atomic<bool> g_acct_save{false};    // signed in or out: the token wants saving
std::atomic<bool> g_link_cancel{false};  // stops a link that is waiting for approval

void set_account_lines(const std::string& status, const std::string& detail) {
    std::lock_guard<std::mutex> lk(g_acct_mu);
    g_acct_status = status;
    g_acct_detail = detail;
}

std::string signed_in_line() {
    // The game read its NpId when it went online, so a sign-in after that
    // names the account to the server but not to the game until the next run.
    if (net::account_refused())
        return "Signed in as " + net::account_name() + " - the server refused it (expired or replaced): link this PC again";
    return "Signed in as " + net::account_name() +
           (hle_np_context_started() ? " (in full on the next launch)" : "");
}

// Link with Discord: a code the player approves on the website's /link page,
// signed in there with Discord; this thread polls until it is approved,
// denied or expired.
void link_thread() {
    net::DeviceLink link;
    std::string err;
    if (!net::account_link_start(link, err)) {
        set_account_lines("Link failed: " + err, "");
        g_acct_busy.store(false);
        return;
    }
    const std::string uri = link.verification_uri + "?code=" + link.user_code;
    set_account_lines("Waiting for approval on the website...",
                      "Code " + link.user_code + "  -  approve it at " + link.verification_uri);
    SDL_OpenURL(uri.c_str());
    int interval = link.interval;
    for (int waited = 0; waited < link.expires_in && !g_link_cancel.load();) {
        SDL_Delay(static_cast<Uint32>(interval) * 1000u);
        waited += interval;
        const std::string status = net::account_link_poll(link, err);
        if (status == "pending") continue;
        if (status == "slow_down") {
            ++interval;
            continue;
        }
        if (status == "approved") {
            set_account_lines(signed_in_line(), "");
            g_acct_save.store(true);
        } else {
            set_account_lines(status == "denied" ? "The link was denied on the website"
                              : status == "expired" ? "The code expired; link again"
                                                    : "Link failed: " + err, "");
        }
        g_acct_busy.store(false);
        return;
    }
    set_account_lines(g_link_cancel.load() ? "Link cancelled" : "The code expired; link again", "");
    g_acct_busy.store(false);
}

// The UPDATES buttons, and whether each can do anything right now.
void update_action(int which) {
    if (which == 10) updater::check_now();
    if (which == 11) updater::open_release_page();
}

bool action_idle(int which) {
    if (which < 10) return !g_acct_busy.load();
    if (updater::busy()) return false;
    if (which == 11) return updater::update_available();
    return true;
}

void account_action(int which, const std::string& name, const std::string& code) {
    if (which >= 10) {
        update_action(which);
        return;
    }
    if (which == 4) {  // sign out, also cancelling a link in progress
        g_link_cancel.store(true);
        std::thread([] {
            net::account_logout();
            set_account_lines("Signed out", "");
            g_acct_save.store(true);
        }).detach();
        return;
    }
    if (g_acct_busy.load()) return;
    if ((which == 1 || which == 2) && name.empty()) {
        set_account_lines("Type a name first", "");
        return;
    }
    if (which == 2 && code.empty()) {
        set_account_lines("Type the recovery code first", "");
        return;
    }
    if (which == 3 && !net::account_logged_in()) {
        set_account_lines("Sign in first", "");
        return;
    }
    g_acct_busy.store(true);
    g_link_cancel.store(false);
    if (which == 0) {
        set_account_lines("Asking the server for a code...", "");
        std::thread(link_thread).detach();
        return;
    }
    set_account_lines(which == 1 ? "Creating the account..." : which == 2 ? "Recovering..." : "Asking for a code...", "");
    std::thread([which, name, code] {
        std::string err, out;
        bool ok = false;
        if (which == 1) {
            ok = net::account_create(name, out, err);
        } else if (which == 2) {
            ok = net::account_recover(name, code, out, err);
        } else {
            ok = net::account_web_code(out, err);
        }
        if (!ok) {
            // A server without accounts says so itself; anything else is the
            // server's refusal (a name taken, a daily limit) in its words.
            set_account_lines(err.find("no account service") != std::string::npos ? err : "Refused: " + err, "");
        } else if (which == 3) {
            set_account_lines(signed_in_line(), "Website code " + out + "  -  use it on the account page within 10 minutes");
        } else {
            // The recovery code is shown here once and never written anywhere.
            set_account_lines(signed_in_line(), "Recovery code " + out + "  -  write it down now; it is shown once");
            g_field[1].clear();
            g_acct_save.store(true);
        }
        g_acct_busy.store(false);
    }).detach();
}

std::string account_status() {
    std::lock_guard<std::mutex> lk(g_acct_mu);
    if (!g_acct_status.empty()) return g_acct_status;
    return net::account_logged_in() ? "Signed in as " + net::account_name() : "Not signed in";
}

std::string account_detail() {
    std::lock_guard<std::mutex> lk(g_acct_mu);
    return g_acct_detail;
}

void edit_begin(int field) {
    // The window has one text entry. Taken from the game's own box while that
    // is open, it ended the box with this field's text - or, closed with the
    // screen, left the game waiting on a box nobody could type into again.
    if (g_edit_field < 0 && host_text_entry_open()) {
        set_account_lines("Finish the game's text box first (Enter or Escape)", "");
        return;
    }
    g_edit_field = field;
    host_text_entry_begin(g_field[field].c_str(), field == 0 ? 16 : 48, false, field == 0 ? "Account name" : "Recovery code");
}

// Called every frame the screen draws: takes the typed text, ends the entry
// when the player did.
void edit_poll() {
    if (g_edit_field < 0) return;
    std::string text;
    const int r = host_text_entry_poll(text);
    if (r == 0) {
        g_field[g_edit_field] = text;
        return;
    }
    if (r == 1) g_field[g_edit_field] = text;
    host_text_entry_end();
    g_edit_field = -1;
}

// The frame-rate values are the labels themselves: "Uncapped" is 0, anything
// else is the number.
int fps_from_label(const char* v) { return std::atoi(v); }

int fps_index(int fps) {
    for (std::size_t v = 0; v < g_set[kFrameCap].values.size(); ++v) {
        if (fps_from_label(g_set[kFrameCap].values[v]) == fps) {
            return static_cast<int>(v);
        }
    }
    return -1;
}

std::string options_path() {
    // HELIAS_OPTIONS_PATH: a file of its own, so a scripted test that changes
    // settings (tools/menu_drive.sh) never writes the player's.
    if (const char* e = std::getenv("HELIAS_OPTIONS_PATH"); e && e[0]) {
        return e;
    }
    // The per-user folder (config_user_dir), beside the per-user config: the
    // F10 settings and the account outlive any copy of heliasdream. A --config is a
    // profile with a file of its own (HeliasDream-options-<name>.toml), so a dev
    // server's account never mixes with the live one's. The file from before
    // (beside that config, or the config a package or tree used, or in the
    // working directory) is copied over the first time: nobody is signed out.
    static const std::string path = [] {
        const HostConfig& c = config();
        const std::string mine = config_user_dir() + "/" +
                                 (c.profile.empty() ? std::string("HeliasDream-options.toml") : "HeliasDream-options-" + c.profile + ".toml");
        std::ifstream have(mine);
        if (have) return mine;
        const auto beside = [](const std::string& f) {
            const std::size_t slash = f.find_last_of("/\\");
            return (slash == std::string::npos ? std::string() : f.substr(0, slash + 1)) + "bbhost-options.toml";
        };
        std::vector<std::string> old;
        if (!c.profile.empty()) {
            old.push_back(beside(c.profile_config));
        } else {
            for (const char* f : {"bbhost.toml"}) old.push_back(beside(f));
            old.push_back(beside(config_exe_dir() + "/bbhost.toml"));
        }
        // A folder of its own (HELIAS_CONFIG_DIR: every test harness) takes the
        // settings but not the [account]: one old file beside several test
        // configs signed each of their profiles in as that player, with a
        // token long replaced, and the server refused them all day.
        const char* own_dir = std::getenv("HELIAS_CONFIG_DIR");
        const bool keep_account = !(own_dir && own_dir[0]);
        for (const std::string& f : old) {
            if (f == mine) continue;
            std::ifstream in(f, std::ios::binary);
            if (!in) continue;
            std::ofstream out(mine, std::ios::binary);
            bool in_account = false, dropped = false;
            for (std::string line; std::getline(in, line);) {
                if (!line.empty() && line[0] == '[') in_account = line.rfind("[account]", 0) == 0;
                if (in_account && !keep_account) {
                    dropped = true;
                    continue;
                }
                out << line << '\n';
            }
            if (out) host_log("options: copied %s to %s (the per-user settings every copy of heliasdream reads)%s", f.c_str(),
                              mine.c_str(), dropped ? ", not its account (a folder of its own)" : "");
            break;
        }
        return mine;
    }();
    return path;
}

// Applied every time one changes, and once at load, so there is one path to
// the live state instead of a copy in each caller.
void apply(int id) {
    const Setting& s = g_set[id];
    switch (id) {
        case kWindowMode: host_window_set_fullscreen(s.index == 1); break;
        case kVsync: host_window_set_vsync(s.index == 0); break;
        case kHdr: host_window_set_hdr(s.index == 1); break;
        case kFrameCap:
            hle_video_set_fps_cap(fps_from_label(s.values[static_cast<std::size_t>(s.index)]));
            break;
        case kMouseMenu:
        case kMouseCamera:
        case kMouseSens:
        case kMouseInvertX:
        case kMouseInvertY:
        case kMouseAutoRotation:
        case kDrawCursor:
        case kButtonPrompts:
        case kPadIcons:
        case kCompileNote:
        case kGameLanguage:
        case kMenuLanguage:
        case kHdrUi:
        case kSubmitSplit:
        case kThreadPriority:
        case kFsr4SharpMotion:
            break;  // the settings object carries them (host/settings.h)
        case kSsao:
        case kMotionBlur:
        case kAntiAlias:
        case kDepthOfField:
        case kChromaticAberration:
        case kResolution: {
            unsigned w = 0, h = 0;
            // A change the player made asks to be kept (g_res_prev set by
            // set_index); going back, and the start, ask nothing. HELIAS_RES
            // wins here too, or applying the settings at the start undid it.
            const char* forced = std::getenv("HELIAS_RES");
            if ((forced && forced[0] && std::sscanf(forced, "%ux%u", &w, &h) == 2) ||
                std::sscanf(s.values[static_cast<std::size_t>(s.index)], "%ux%u", &w, &h) == 2)
                live_resolution_request(w, h, g_res_prev >= 0 && !g_res_reverting);
            break;
        }
        case kSkipLogos:
        case kDebugMenu:
        case kDebugCamera:
        case kChangeAppearance:
        case kRebirth:
        case kFivePlayers:
            break;  // read by the loader before any of this exists
        case kUpscaler:
        case kUpscaleQuality:
            // heliasdream-vk Phase 1: the scene's size follows (engine/scene_split.h).
            live_resolution_scale_changed();
            break;
        case kUpscaleSharpness:
        case kUpscaleReactive:
        case kUpscaleJitter:
        case kDebugMotionVectors:
        case kDebugUpscalerTime:
            break;  // the settings object carries them (host/upscaler.h reads it)
        default: break;
    }
}

// The file's format. 2: Mouse in menus defaults to On. A file without a
// version was written by a build where it defaulted to Off, and every save
// writes every setting - so its "Off" is that old default far more often
// than a choice, and reads as On (once: the next save writes version 2).
// 3: frame_cap is the game's frame rate (30, 60, 90, 120, Uncapped); an older
// file's 90, 120, 144 and Off were caps on presentation that ran the game at
// 60, and read as 60.
constexpr int kOptionsVersion = 3;

void save() {
    if (!g_dirty) {
        return;
    }
    const std::string path = options_path();
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        host_log("options: cannot write %s", path.c_str());
        return;
    }
    std::fprintf(f, "# heliasdream PC port options. Written by the options screen (F10).\n");
    std::fprintf(f, "[options]\n");
    std::fprintf(f, "version = \"%d\"\n", kOptionsVersion);
    for (const Setting& s : g_set) {
        // A resolution still waiting for its keep-or-revert answer is not
        // saved: the file keeps the one it would go back to.
        const int index = &s == &g_set[kResolution] && g_res_prev >= 0 ? g_res_prev : s.index;
        std::fprintf(f, "%s = \"%s\"\n", s.key, s.values[static_cast<std::size_t>(index)]);
    }
    host_bindings_save(f);
    // The account: its name and token.
    if (net::account_logged_in()) {
        std::fprintf(f, "[account]\nname = \"%s\"\ntoken = \"%s\"\n", net::account_name().c_str(),
                     net::account_token().c_str());
    }
    std::fclose(f);
    g_dirty = false;
    host_log("options: saved %s", path.c_str());
}

// Bumped on every change, for readers that act on a setting live and would
// rather not look it up by name on every frame (host_opt_serial).
std::atomic<std::uint64_t> g_serial{1};
// FSR 4.1.1 cannot run this session (host_opt_fsr4_unavailable): Auto AA stays on.
std::atomic<bool> g_fsr4_unavailable{false};

// The settings object (host/settings.h): every field from the table, scaled
// the way its consumer wants it, rebuilt whenever a setting changes.
std::mutex g_settings_mu;
HostSettings g_settings;

bool on_of(int id) { return reads_on(g_set[id], g_set[id].index); }
int index_of(int id) { return g_set[id].index; }

std::atomic<float> g_mip_bias{0.0f};  // host_opt_mip_bias(): the samplers read it on every lookup
std::atomic<unsigned> g_submit_split{0};  // host_opt_submit_split(): read at every draw
std::atomic<bool> g_fsr4_sharp_motion{false};  // host_opt_fsr4_sharp_motion(): read at each FSR 4 frame

void rebuild_settings() {
    HostSettings h;
    h.serial = g_serial.load(std::memory_order_acquire);
    h.fullscreen = index_of(kWindowMode) == 1;
    h.vsync = index_of(kVsync) == 0;
    h.frame_cap = fps_from_label(g_set[kFrameCap].values[static_cast<std::size_t>(index_of(kFrameCap))]);
    h.fps_counter = on_of(kFpsCounter);
    h.fps_position = index_of(kFpsPosition);
    h.fps_scale = 0.75f + 0.25f * static_cast<float>(index_of(kFpsSize));
    {
        static constexpr std::uint32_t kColours[] = {0xffffffu, 0xffd54au, 0x7cfc8au, 0x6fe3ffu, 0xff5a5au, 0xffa040u, 0xff8ad8u, 0x000000u};
        static constexpr std::uint32_t kBacks[] = {0x0f0f0fu, 0x000000u, 0x3a0a0au, 0x0a1a3au, 0xffffffu};
        h.fps_colour = kColours[std::clamp(index_of(kFpsColour), 0, 7)];
        h.fps_background = kBacks[std::clamp(index_of(kFpsBackground), 0, 4)];
    }
    h.fps_opacity = 0.1f * static_cast<float>(index_of(kFpsOpacity) + 1);
    h.fps_background_opacity = 0.1f * static_cast<float>(index_of(kFpsBackgroundOpacity));
    h.fps_border = on_of(kFpsBorder);
    h.fps_show_rate = on_of(kFpsShowRate);
    h.fps_show_frame_time = on_of(kFpsShowFrameTime);
    h.fps_show_upscaler = on_of(kFpsShowUpscaler);
    h.fps_show_cpu = on_of(kFpsShowCpu);
    h.fps_show_gpu = on_of(kFpsShowGpu);
    h.brightness = 1.0f + 0.1f * static_cast<float>(index_of(kBrightness) - 5);
    h.contrast = 1.0f + 0.08f * static_cast<float>(index_of(kContrast) - 5);
    h.hue_degrees = static_cast<float>(-180 + 15 * index_of(kHue));
    h.hdr = on_of(kHdr);
    h.hdr_paper_white = std::strtof(g_set[kHdrPaperWhite].values[static_cast<std::size_t>(index_of(kHdrPaperWhite))], nullptr);
    h.hdr_peak_auto = on_of(kHdrPeakAuto);
    h.hdr_peak = std::strtof(g_set[kHdrPeak].values[static_cast<std::size_t>(index_of(kHdrPeak))], nullptr);
    h.hdr_boost = 0.25f * static_cast<float>(index_of(kHdrBoost));
    h.menu_key = index_of(kMenuKey);
    h.trophy_popups = on_of(kTrophyPopups);
    {
        // HELIAS_RES=WxH still wins, so an A/B run started with one behaves
        // the way its command line says.
        unsigned w = 0, hh = 0;
        const char* e = std::getenv("HELIAS_RES");
        if (!(e && e[0] && std::sscanf(e, "%ux%u", &w, &hh) == 2)) {
            const Setting& r = g_set[kResolution];
            if (std::sscanf(r.values[static_cast<std::size_t>(r.index)], "%ux%u", &w, &hh) != 2) w = hh = 0;
        }
        if (w && hh) {
            h.res_width = static_cast<int>(w);
            h.res_height = static_cast<int>(hh);
        }
    }
    // Distance and height: 5 is the game's own, 10% a step either way. Field
    // of view: 0 is the game's own, 5% wider a step (HELIAS_FOV overrides).
    const auto around = [](int id) { return 0.5f + 0.1f * static_cast<float>(index_of(id)); };
    h.camera_distance_scale = around(kCameraDistance);
    h.camera_height_scale = around(kCameraHeight);
    {
        const char* e = std::getenv("HELIAS_FOV");
        const float env = e && *e ? std::strtof(e, nullptr) : 0.0f;
        const int fov = index_of(kFov);
        h.fov_scale = env > 0.0f ? env : fov <= 0 ? 1.0f : 1.0f + 0.05f * static_cast<float>(fov);
    }
    h.mouse_menu = on_of(kMouseMenu);
    h.mouse_camera = on_of(kMouseCamera);
    // DS3's mouse sensitivity (engine/mouse_camera.h): the same 0..10 and the
    // same default, and the same angle a count at each step.
    h.mouse_sens = index_of(kMouseSens) < 0 || index_of(kMouseSens) > 10 ? 5 : index_of(kMouseSens);
    h.mouse_invert_x = on_of(kMouseInvertX);
    h.mouse_invert_y = on_of(kMouseInvertY);
    h.mouse_auto_rotation = on_of(kMouseAutoRotation);
    h.draw_cursor = on_of(kDrawCursor);
    h.button_prompts = index_of(kButtonPrompts) < 0 ? 0 : index_of(kButtonPrompts);
    h.pad_icons = std::clamp(index_of(kPadIcons), 0, 2);
    h.compile_note = on_of(kCompileNote);
    menu_i18n_set_language(std::clamp(index_of(kMenuLanguage), 0, kMenuLanguageCount - 1));
    {
        const int i = index_of(kHdrUi);
        h.hdr_ui_brightness = i <= 0 ? 0.0f : 0.4f + 0.1f * static_cast<float>(i);  // 1: 50%
    }
    {
        const int i = index_of(kSubmitSplit);
        h.submit_split = i > 0 ? static_cast<unsigned>(std::strtoul(g_set[kSubmitSplit].values[static_cast<std::size_t>(i)], nullptr, 10)) : 0u;
        g_submit_split.store(h.submit_split, std::memory_order_relaxed);
    }
    h.thread_priority = on_of(kThreadPriority);
    host_threads_set_priority(h.thread_priority);
    h.fsr4_sharp_motion = on_of(kFsr4SharpMotion);
    g_fsr4_sharp_motion.store(h.fsr4_sharp_motion, std::memory_order_relaxed);
    {
        // The PS4's language codes, in the setting's order.
        static constexpr int kLang[] = {1, 2, 3, 4, 5, 8, 0};
        h.game_language = kLang[std::clamp(index_of(kGameLanguage), 0, 6)];
    }
    h.haptics = std::clamp(index_of(kHaptics), 0, 2);
    h.haptics_strength = static_cast<float>(std::clamp(index_of(kHapticsStrength), 0, 10)) / 5.0f;
    h.adaptive_triggers = on_of(kAdaptiveTriggers);
    h.trigger_strength = std::clamp(index_of(kTriggerStrength), 0, 2);
    h.menu_haptics = on_of(kMenuHaptics);
    h.ssao = on_of(kSsao);
    h.motion_blur = on_of(kMotionBlur);
    h.depth_of_field = on_of(kDepthOfField);
    h.chromatic_aberration = on_of(kChromaticAberration);
    h.vignette = on_of(kVignette);
    h.bloom = static_cast<float>(index_of(kBloom)) / 10.0f;
    h.saturation = static_cast<float>(index_of(kSaturation)) / 5.0f;  // 5 the area's own
    h.fog = static_cast<float>(index_of(kFog)) / 10.0f;
    h.ao_strength = static_cast<float>(index_of(kAoStrength)) / 5.0f;
    h.shadow_scale = 1.0f + 0.2f * static_cast<float>(index_of(kShadowDistance));
    {
        static constexpr int kBias[] = {-2, 0, 1, 2};
        const int i = index_of(kModelDetail);
        h.lod_bias = i >= 0 && i < 4 ? kBias[i] : 0;
    }
    h.skip_logos = on_of(kSkipLogos);
    h.debug_menu = on_of(kDebugMenu);
    h.debug_camera = on_of(kDebugCamera);
    h.change_appearance = on_of(kChangeAppearance);
    h.rebirth = on_of(kRebirth);
    h.five_players = on_of(kFivePlayers);
    h.upscaler = std::clamp(index_of(kUpscaler), 0, upscaler::kKindCount - 1);
    h.upscale_quality = std::clamp(index_of(kUpscaleQuality), 0, 4);
    {
        const int i = index_of(kMipBias);
        if (i == 0) {
            static constexpr float kScale[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
            const bool temporal = upscaler::is_temporal(h.upscaler) && h.upscaler != upscaler::kTaa;  // TAA renders at the output's size
            h.mip_bias = temporal ? -std::log2(kScale[std::clamp(h.upscale_quality, 0, 4)]) : 0.0f;
        } else {
            h.mip_bias = std::strtof(g_set[kMipBias].values[static_cast<std::size_t>(i)], nullptr);
        }
        g_mip_bias.store(h.mip_bias, std::memory_order_relaxed);
    }
    // Auto: off while FSR 4.1.1 is the upscaler and runs (host_opt_fsr4_unavailable).
    // It runs only below Native AA: Native AA is stock rendering, the scene not
    // split and no FSR pass (DEVLOG Phase 1, decision 2), so Auto keeps the
    // game's AA there - it turned it off before, and Native AA had none at all.
    h.anti_alias_auto = index_of(kAntiAlias) == 2;
    // Native TAA is FSR 3.1's own anti-aliasing: Auto leaves the game's pass off under it too.
    h.anti_alias = h.anti_alias_auto ? !((h.upscaler == 3 && h.upscale_quality > 0 && !g_fsr4_unavailable.load()) || h.upscaler == upscaler::kTaa ||
                                         ((h.upscaler == upscaler::kFsr4 || h.upscaler == upscaler::kDlss) && h.upscale_quality > 0))
                                     : on_of(kAntiAlias);
    h.upscale_sharpness = static_cast<float>(std::clamp(index_of(kUpscaleSharpness), 0, 10)) / 10.0f;
    h.upscale_reactive = on_of(kUpscaleReactive);
    h.upscale_jitter = on_of(kUpscaleJitter);
    h.debug_motion_vectors = on_of(kDebugMotionVectors);
    h.debug_upscaler_time = on_of(kDebugUpscalerTime);
    std::lock_guard<std::mutex> lk(g_settings_mu);
    g_settings = h;
}

// The index one step on from a setting's: a long one (HDR's nits, in ones)
// goes 10 at a time and stops at its ends; Shift takes single steps.
int stepped(int id, int dir, bool fine) {
    const int n = static_cast<int>(g_set[id].values.size());
    if (n <= 100) return g_set[id].index + dir;
    return std::clamp(g_set[id].index + dir * (fine ? 1 : 10), 0, n - 1);
}

void set_index(int id, int index, bool from_file) {
    Setting& s = g_set[id];
    const int n = static_cast<int>(s.values.size());
    index = ((index % n) + n) % n;
    if (index == s.index && from_file) {
        return;
    }
    if (id == kResolution && !from_file && !g_res_reverting && index != s.index && res_live(index)) {
        if (g_res_prev < 0) g_res_prev = s.index;  // a second change while one waits goes back to the first's start
        if (index == g_res_prev) g_res_prev = -1;  // back where it started: nothing to ask
    }
    s.index = index;
    g_serial.fetch_add(1, std::memory_order_release);
    rebuild_settings();
    if (!from_file) {
        g_dirty = true;
    }
    apply(id);
}

void res_keep() {
    if (g_res_prev < 0) return;
    host_log("options: resolution %s kept", g_set[kResolution].values[static_cast<std::size_t>(g_set[kResolution].index)]);
    g_res_prev = -1;
    g_dirty = true;
    save();
}

void res_revert() {
    if (g_res_prev < 0) return;
    const int prev = g_res_prev;
    g_res_prev = -1;
    host_log("options: resolution back to %s", g_set[kResolution].values[static_cast<std::size_t>(prev)]);
    g_res_reverting = true;
    set_index(kResolution, prev, false);
    g_res_reverting = false;
    // An entry waiting for the next start (res_live) cannot be gone back to
    // now: the picture goes back to the size it had before the change.
    if (!res_live(prev)) live_resolution_go_back();
    save();
}

}  // namespace



void host_options_apply() {
    for (int id = 0; id < kSettingCount; ++id) {
        apply(id);
    }
}

void host_options_load() {
    // Once: the setup window (host/launcher.h) loads them before it shows them.
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    // HeliasDream.toml still sets the defaults; the options file is what the player
    // changed, and the environment switches still win over both.
    host_bindings_load_defaults();
    g_set[kSkipLogos].index = config().skip_intro ? 1 : 0;
    // The PC enhancements seed from HeliasDream.toml's [world] (all on unless it
    // says otherwise); the options file then holds the player's choice, and a
    // HELIAS_* switch still wins over both (config_set_enhancements).
    g_set[kChangeAppearance].index = config().change_appearance ? 0 : 1;
    g_set[kRebirth].index = config().rebirth ? 0 : 1;
    g_set[kFivePlayers].index = config().five_players ? 0 : 1;
    // video.fps_cap is the frame rate's default: 30, 60, 90, 120 or 0
    // (uncapped); another number takes the nearest rate below it.
    {
        const int cap = config().fps_cap;
        const int mode = cap <= 0 ? 0 : cap >= 120 ? 120 : cap >= 90 ? 90 : cap >= 60 ? 60 : 30;
        if (mode != cap) host_log("options: video.fps_cap = %d is not a frame rate; %d", cap, mode);
        g_set[kFrameCap].index = std::max(0, fps_index(mode));
    }
    // A package's own (video.resolution, video.window_mode: the Steam Deck
    // kit's 1280x800, fullscreen; video.model_detail), by the entry's name.
    const auto seed = [](int id, const std::string& value) {
        for (std::size_t k = 0; !value.empty() && k < g_set[id].values.size(); ++k)
            if (value == g_set[id].values[k]) g_set[id].index = static_cast<int>(k);
    };
    seed(kResolution, config().resolution);
    seed(kWindowMode, config().window_mode);
    seed(kModelDetail, config().model_detail);
    for (const int id : {static_cast<int>(kSkipLogos), static_cast<int>(kFrameCap), static_cast<int>(kResolution), static_cast<int>(kWindowMode),
                         static_cast<int>(kModelDetail), static_cast<int>(kChangeAppearance),
                         static_cast<int>(kRebirth), static_cast<int>(kFivePlayers)})
        g_default_index[static_cast<std::size_t>(id)] = g_set[id].index;
    const std::string path = options_path();
    FILE* f = std::fopen(path.c_str(), "r");
    if (f) {
        char line[512];
        bool keys = false;  // in [keys]: bindings, not settings
        bool account = false;  // in [account]: the name and token
        std::string acct_name, acct_token;
        int file_version = 1;
        bool old_hunters_off = false;  // v0.2.9's PC enhancement, turned off
        std::string frame_cap_val;
        while (std::fgets(line, sizeof(line), f)) {
            if (line[0] == '[') {
                keys = std::strncmp(line, "[keys]", 6) == 0;
                account = std::strncmp(line, "[account]", 9) == 0;
                continue;
            }
            char* eq = std::strchr(line, '=');
            if (!eq || line[0] == '#') {
                continue;
            }
            *eq = 0;
            std::string key(line), val(eq + 1);
            auto trim = [](std::string& v) {
                while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ' ||
                                      v.back() == '\t' || v.back() == '"')) {
                    v.pop_back();
                }
                std::size_t i = 0;
                while (i < v.size() && (v[i] == ' ' || v[i] == '\t' || v[i] == '"')) {
                    ++i;
                }
                v.erase(0, i);
            };
            trim(key);
            trim(val);
            if (keys) {
                host_bindings_parse(key.c_str(), val.c_str());
                continue;
            }
            if (account) {
                if (key == "name") acct_name = val;
                if (key == "token") acct_token = val;
                continue;
            }
            if (key == "version") {
                file_version = std::atoi(val.c_str());
                continue;
            }
            if (key == "old_hunters") {
                old_hunters_off = val == "Off";
                continue;
            }
            // Looked at again once the file's version is known (below).
            if (key == "frame_cap") frame_cap_val = val;
            // The four named steps mouse_sens used to have, on the new scale.
            if (key == "mouse_sens") {
                const char* old[] = {"Low", "Medium", "High", "Very high"};
                const int to[] = {2, 5, 7, 9};
                for (int k = 0; k < 4; ++k) {
                    if (val == old[k]) val = std::to_string(to[k]);
                }
            }
            for (int id = 0; id < kSettingCount; ++id) {
                if (key != g_set[id].key) {
                    continue;
                }
                for (std::size_t v = 0; v < g_set[id].values.size(); ++v) {
                    if (val == g_set[id].values[v]) {
                        g_set[id].index = static_cast<int>(v);
                    }
                }
            }
        }
        std::fclose(f);
        // Before version 3 the frame_cap was a cap on presentation, and its 90,
        // 120, 144 and Off ran the game at 60: the frame rate that ran is 60
        // (before any save below writes version 3).
        if (file_version < 3 &&
            (frame_cap_val == "90" || frame_cap_val == "120" || frame_cap_val == "144" || frame_cap_val == "Off")) {
            g_set[kFrameCap].index = std::max(0, fps_index(60));
            g_dirty = true;
            host_log("options: frame_cap \"%s\" (a cap that ran the game at 60) reads as the frame rate 60",
                     frame_cap_val.c_str());
        }
        if (!acct_name.empty() && !acct_token.empty()) {
            net::account_set(acct_name, acct_token);
            g_field[0] = acct_name;
            host_log("options: account %s from %s", acct_name.c_str(), path.c_str());
        }
        // After the account: save() writes everything it holds, and saving
        // before the account was set dropped the [account] section - a
        // dev-server tester was signed out (2026-10-02).
        if (file_version < 2 && g_set[kMouseMenu].index == 0) {
            g_set[kMouseMenu].index = 1;
            g_dirty = true;
            host_log("options: Mouse in menus read as On - this file predates it defaulting to On");
            save();  // as version 2, so a later Off is the player's and stays
        }
        // The Old Hunters was a PC enhancement in v0.2.9 and is the patch
        // old-hunters since. A player who turned it off keeps it off: the
        // per-user HeliasDream.toml's [patches] says so from now on, and the next
        // save drops the old key (unless [patches] already says something).
        if (old_hunters_off) {
            bool carried = !config_value("patches.old-hunters").empty();
            if (!carried) {
                const std::string user = config_user_file();
                carried = config_set_values(user, {{"patches", "old-hunters", "false"}});
                if (carried) {
                    config_value_set("patches.old-hunters", "false");
                    host_log("options: The Old Hunters was off here; it is the patch old-hunters now, off under [patches] in %s",
                             user.c_str());
                }
            }
            if (carried) {
                g_dirty = true;
                save();
            }
        }
        host_log("options: read %s", path.c_str());
    }
    // HELIAS_ACCOUNT=name:token, for a scripted run that must not touch the file.
    if (const char* e = std::getenv("HELIAS_ACCOUNT"); e && e[0]) {
        const std::string v = e;
        if (const std::size_t c = v.find(':'); c != std::string::npos) {
            net::account_set(v.substr(0, c), v.substr(c + 1));
            g_field[0] = v.substr(0, c);
        }
    }
    // The environment switches the track was brought up with still win, so a
    // run started with one behaves the way it says on the command line.
    if (const char* e = std::getenv("HELIAS_MOUSE_MENU"); e && e[0]) {
        g_set[kMouseMenu].index = e[0] == '0' ? 0 : 1;
    }
    // The post-processing switches, for A/B runs: HELIAS_SSAO=0 and friends.
    for (const auto& [env, id] : {std::pair<const char*, int>{"HELIAS_SSAO", kSsao},
                                  {"HELIAS_MOTION_BLUR", kMotionBlur},
                                  {"HELIAS_ANTI_ALIAS", kAntiAlias},
                                  {"HELIAS_DEPTH_OF_FIELD", kDepthOfField},
                                  {"HELIAS_CHROMATIC_ABERRATION", kChromaticAberration}}) {
        if (const char* e = std::getenv(env); e && e[0]) {
            // {"On","Off"} first (anti_alias adds "Auto"): 0 off, a or A Auto, else on.
            g_set[id].index = e[0] == '0' ? 1 : (id == kAntiAlias && (e[0] == 'a' || e[0] == 'A')) ? 2 : 0;
        }
    }
    if (const char* e = std::getenv("HELIAS_MOUSE_CAMERA"); e && e[0]) {
        g_set[kMouseCamera].index = e[0] == '0' ? 0 : 1;
    }
    if (const char* e = std::getenv("HELIAS_MOUSE_SENS"); e && e[0]) {
        const int v = std::atoi(e);
        g_set[kMouseSens].index = v < 0 ? 0 : v > 10 ? 10 : v;
    }
    if (const char* e = std::getenv("HELIAS_MOUSE_CURSOR"); e && e[0]) {
        g_set[kDrawCursor].index = e[0] == '0' ? 1 : 0;
    }
    host_options_apply();
    rebuild_settings();
}

bool host_options_open() { return g_open.load(std::memory_order_relaxed); }
bool host_options_editing() { return g_edit_field >= 0; }

void host_options_set_open(bool open) {
    if (open == g_open.load(std::memory_order_relaxed)) {
        return;
    }
    g_open.store(open, std::memory_order_relaxed);
    g_pad_edit_row = -1;
    if (open) {
        // Nothing has consumed the pointer while the screen was closed, so a
        // click from minutes ago is still latched. Drop it: the first click
        // the screen sees must be one aimed at it.
        host_mouse_state();
        g_laid_out = false;
    } else {
        if (g_edit_field >= 0) {
            host_text_entry_end();
            g_edit_field = -1;
        }
        save();
    }
    host_log("options: %s", open ? "open" : "closed");
}

bool host_options_key(int scancode, bool repeat) {
#if defined(HELIAS_HAVE_SDL3)
    // F10 always; the Menu key setting adds Insert, Home or F1.
    static constexpr int kMenuKeys[] = {SDL_SCANCODE_F10, SDL_SCANCODE_INSERT, SDL_SCANCODE_HOME, SDL_SCANCODE_F1};
    const int extra = kMenuKeys[std::clamp(g_set[kMenuKey].index, 0, 3)];
    if (scancode == SDL_SCANCODE_F10 || (scancode == extra && g_edit_field < 0)) {
        if (!repeat) {
            host_options_set_open(!host_options_open());
        }
        return true;
    }
    if (!host_options_open()) {
        return false;
    }
    g_pad_edit_row = -1;  // the keyboard changes values with its arrows
    // A field being typed: the window's text entry takes the keys (Enter
    // accepts, Escape cancels); the poll in the frame picks the result up.
    if (g_edit_field >= 0) {
        return false;
    }
    const int step = scancode == SDL_SCANCODE_UP ? -1 : scancode == SDL_SCANCODE_DOWN ? 1 : 0;
    if (step) {
        move_in_section(step);
        return true;
    }
    // The section tabs: Q / E, Page Up / Down, Tab (Shift+Tab back), 1-7.
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    int tab = 0;
    if (scancode == SDL_SCANCODE_Q || scancode == SDL_SCANCODE_PAGEUP || (scancode == SDL_SCANCODE_TAB && shift)) tab = -1;
    if (scancode == SDL_SCANCODE_E || scancode == SDL_SCANCODE_PAGEDOWN || (scancode == SDL_SCANCODE_TAB && !shift)) tab = 1;
    if (tab) {
        go_section(section_of(g_sel) + tab);
        return true;
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9 && scancode - SDL_SCANCODE_1 < g_section_count) {
        go_section(scancode - SDL_SCANCODE_1);
        return true;
    }
    const int id = g_rows[g_sel].setting;
    if ((scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_RIGHT) && id >= 0) {
        // A held key steps to the end and stops there: wrapping round would
        // flip a two-value setting back and forth for as long as it is held,
        // and HDR or the upscaler rebuilds with every flip.
        const int to = stepped(id, scancode == SDL_SCANCODE_LEFT ? -1 : 1, shift);
        if (repeat && (to < 0 || to >= static_cast<int>(g_set[id].values.size()))) return true;
        set_index(id, to, false);
        return true;
    }
    if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER ||
        scancode == SDL_SCANCODE_SPACE) {
        if (g_rows[g_sel].kind == Row::Close) {
            host_options_set_open(false);
        } else if (g_rows[g_sel].kind == Row::Text) {
            edit_begin(g_rows[g_sel].field);
        } else if (g_rows[g_sel].kind == Row::Action) {
            account_action(g_rows[g_sel].field, g_field[0], g_field[1]);
        } else if (id >= 0) {
            set_index(id, g_set[id].index + 1, false);
        }
        return true;
    }
    if (scancode == SDL_SCANCODE_ESCAPE) {
        host_options_set_open(false);
        return true;
    }
    // Modal: while it is open it eats every key, so nothing leaks to the pad.
    return true;
#else
    (void)scancode;
    (void)repeat;
    return false;
#endif
}

void host_options_pad(OptionsPad press, bool repeat) {
    if (!host_options_open() || g_edit_field >= 0) return;
    const Row& row = g_rows[g_sel];
    const int id = row.kind == Row::Option ? row.setting : -1;
    switch (press) {
        case OptionsPad::Up:
        case OptionsPad::Down:
            g_pad_edit_row = -1;
            move_in_section(press == OptionsPad::Up ? -1 : 1);
            return;
        case OptionsPad::PrevTab:
        case OptionsPad::NextTab:
            g_pad_edit_row = -1;
            go_section(section_of(g_sel) + (press == OptionsPad::PrevTab ? -1 : 1));
            return;
        case OptionsPad::Left:
        case OptionsPad::Right: {
            const int dir = press == OptionsPad::Left ? -1 : 1;
            if (!pad_editing() || id < 0) {
                if (!repeat) move_across(dir);
                return;
            }
            // As a held key: steps to the end and stops there.
            const int to = stepped(id, dir, false);
            if (repeat && (to < 0 || to >= static_cast<int>(g_set[id].values.size()))) return;
            set_index(id, to, false);
            return;
        }
        case OptionsPad::Confirm:
            if (repeat) return;
            if (row.kind == Row::Close) {
                host_options_set_open(false);
            } else if (row.kind == Row::Text) {
                edit_begin(row.field);
            } else if (row.kind == Row::Action) {
                account_action(row.field, g_field[0], g_field[1]);
            } else if (id >= 0) {
                g_pad_edit_row = pad_editing() ? -1 : g_sel;
            }
            return;
        case OptionsPad::Back:
            if (repeat) return;
            if (pad_editing()) {
                g_pad_edit_row = -1;
            } else {
                host_options_set_open(false);
            }
            return;
    }
}

namespace {
void options_panel_frame(float display_w, float display_h);
}

void host_options_frame(float display_w, float display_h) {
    if (g_acct_save.exchange(false)) {
        g_dirty = true;
        save();
    }
    // HELIAS_RES_PROMPT_TEST=<flip>:<entry>: the Resolution setting changed
    // to that entry at that flip, the way the menus change it (tests).
    static long test_flip = [] {
        const char* e = std::getenv("HELIAS_RES_PROMPT_TEST");
        return e && *e ? std::atol(e) : -1L;
    }();
    if (test_flip >= 0 && static_cast<long>(hle_video_flip_count()) >= test_flip) {
        test_flip = -1;
        const char* e = std::strchr(std::getenv("HELIAS_RES_PROMPT_TEST"), ':');
        if (e) host_opt_set_resolution_index(std::atoi(e + 1));
    }
    // The game's keep-or-revert dialog answered (engine/live_resolution.h).
    if (const int a = live_resolution_take_answer()) {
        if (a == 1) res_keep(); else res_revert();
    }
    if (host_options_open()) options_panel_frame(display_w, display_h);
}

namespace {
// `text` broken at spaces into lines no wider than `width` at `scale`; at
// most `max_lines`, the last ending in "..." when there was more.
std::vector<std::string> wrap_note(const char* text, float width, float scale, std::size_t max_lines) {
    std::vector<std::string> out;
    std::string line, word;
    const std::string all(text);
    bool cut = false;
    // Measured in the face and size the note is drawn in (17 px body), so a
    // translation's letters count as what they are, not as bytes.
    (void)scale;
    auto fits = [&](const std::string& t) { return host_overlay_ui_text_width(kOverlayFontBody, 17.0f, t.c_str()) <= width; };
    std::size_t i = 0;
    while (i <= all.size()) {
        const std::size_t j = std::min(all.find(' ', i), all.size());
        word = all.substr(i, j - i);
        i = j + 1;
        if (word.empty()) continue;
        const std::string tried = line.empty() ? word : line + " " + word;
        if (fits(tried) || line.empty()) {
            line = tried;
            continue;
        }
        out.push_back(line);
        line = word;
        if (out.size() == max_lines) {
            cut = true;
            break;
        }
    }
    if (!cut && !line.empty()) out.push_back(line);
    if (out.size() > max_lines) out.resize(max_lines), cut = true;
    if (cut && !out.empty()) {
        std::string& last = out.back();
        while (!last.empty() && !fits(last + "...")) {
            last.pop_back();
            while (!last.empty() && (static_cast<unsigned char>(last.back()) & 0xc0) == 0x80) last.pop_back();  // a whole UTF-8 letter
        }
        last += "...";
    }
    return out;
}

void options_panel_frame(float display_w, float display_h) {
    edit_poll();
    // Helia's Dream's layout (the launcher's): a wide glass pane, the sections
    // as pill tabs along its top, one section at a time in two columns, with
    // room between the rows. Up and Down move inside the section; Q / E, Tab
    // or Page Up / Down (and a click on a tab) change it.
    const float pw = std::fmin(1360.0f, display_w - 80.0f);
    const float ph = std::fmin(760.0f, display_h - 60.0f);
    const float px = std::floor((display_w - pw) * 0.5f), py = std::floor((display_h - ph) * 0.5f);
    constexpr float kHead = 150.0f;  // title, online state, tabs
    constexpr float kFoot = 150.0f;  // the note and the key guide
    constexpr float kRowH = 46.0f, kPitch = 56.0f, kColGap = 24.0f;
    const int sec = section_of(g_sel);
    const int sec_first = g_sections[sec], sec_end = sec + 1 < g_section_count ? g_sections[sec + 1] : kRowCount;

    // The section's rows, split into two columns when they do not fit one.
    int rows[kRowCount];
    int n = 0;
    for (int r = sec_first + 1; r < sec_end; ++r) rows[n++] = r;
    const float body_h = ph - kHead - kFoot;
    const int per_col_fit = std::max(1, static_cast<int>(body_h / kPitch));
    const bool two = n > 6 || n > per_col_fit;
    const int col_rows = two ? (n + 1) / 2 : n;
    const float col_w = two ? (pw - 56.0f - kColGap) * 0.5f : pw - 56.0f;
    const float pitch = std::fmin(kPitch, col_rows > 0 ? body_h / static_cast<float>(col_rows) : kPitch);
    const float row_h = std::fmin(kRowH, pitch - 6.0f);
    for (int r = 0; r < kRowCount; ++r) g_row_y[r] = -10000.0f;  // other sections: the pointer cannot land on them
    for (int i = 0; i < n; ++i) {
        const int col = two && i >= col_rows ? 1 : 0;
        const int k = col ? i - col_rows : i;
        g_row_xs[rows[i]] = px + 28.0f + static_cast<float>(col) * (col_w + kColGap);
        g_row_ws[rows[i]] = col_w;
        g_row_y[rows[i]] = py + kHead + static_cast<float>(k) * pitch;
    }
    g_row_h = row_h;

    // The tabs, measured before the pointer is handled.
    // 17 px, smaller when a language's longer names would not fit the panel.
    float kTabFont = 17.0f;
    float tab_x[16], tab_w[16];
    const float tab_y = py + 92.0f, tab_h = 38.0f;
    for (int pass = 0; pass < 2; ++pass) {
        float tx = px + 28.0f + 5.0f;
        for (int t = 0; t < g_section_count; ++t) {
            tab_w[t] = host_overlay_ui_text_width(kOverlayFontBold, kTabFont, section_name(t)) + 34.0f;
            tab_x[t] = tx;
            tx += tab_w[t] + 4.0f;
        }
        const float room = pw - 56.0f - 10.0f, used = tx - (px + 28.0f + 5.0f);
        if (used <= room || pass == 1) break;
        kTabFont = std::max(12.0f, kTabFont * (room - 38.0f * static_cast<float>(g_section_count)) / (used - 38.0f * static_cast<float>(g_section_count)));
    }

    // The pointer, against this frame's layout. Hover selects only when the
    // pointer moves (or clicks), so a pointer resting on the panel does not
    // hold the selection against the arrow keys.
    const MouseState m = host_mouse_state();
    static float last_x = -1.0f, last_y = -1.0f;
    const bool moved = m.x != last_x || m.y != last_y;
    last_x = m.x;
    last_y = m.y;
    if (m.in_window && (m.pressed & 1u) && m.y >= tab_y && m.y < tab_y + tab_h) {
        for (int t = 0; t < g_section_count; ++t)
            if (m.x >= tab_x[t] && m.x < tab_x[t] + tab_w[t]) go_section(t);
    }
    if (g_laid_out && m.in_window && (moved || (m.pressed & 3u))) {
        for (int r = 0; r < kRowCount; ++r) {
            if (!selectable(r) || m.x < g_row_xs[r] || m.x > g_row_xs[r] + g_row_ws[r]) {
                continue;
            }
            if (m.y >= g_row_y[r] && m.y < g_row_y[r] + g_row_h) {
                g_sel = r;
                if (m.pressed & 1u) {
                    if (g_rows[r].kind == Row::Close) {
                        host_options_set_open(false);
                        return;
                    }
                    if (g_rows[r].kind == Row::Text) {
                        if (g_edit_field != g_rows[r].field) edit_begin(g_rows[r].field);
                    } else if (g_rows[r].kind == Row::Action) {
                        account_action(g_rows[r].field, g_field[0], g_field[1]);
                    } else if (g_rows[r].setting >= 0) {
                        // On the value pill's left half it steps back, on its
                        // right half forward; the label steps forward, as Enter does.
                        const bool back = m.x >= g_row_vl[r] && m.x < g_row_vm[r];
                        set_index(g_rows[r].setting, stepped(g_rows[r].setting, back ? -1 : 1, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0), false);
                    }
                } else if (m.pressed & 2u) {
                    if (g_rows[r].kind == Row::Option) {
                        set_index(g_rows[r].setting, g_set[g_rows[r].setting].index - 1, false);
                    }
                }
                break;
            }
        }
    }
    if (m.wheel != 0.0f) move_in_section(m.wheel > 0.0f ? -1 : 1);

    // Frosted glass: no pane colour, the game behind the panel blurred (the
    // presenter draws it, host/present_pass.h), with only a bright edge and a
    // breath of tint - the tint alone is what shows where the blur cannot be
    // drawn (an FSR 1 picture, which takes the blit's path).
    constexpr std::uint32_t kText = 0xf2f5faffu, kSoft = 0xdde2eaffu, kMuted = 0xb9c1cfffu, kAccent = 0xc2d3efffu;
    host_overlay_glass(px, py, pw, ph, 28.0f);
    host_overlay_round_rect(px, py, pw, ph, 28.0f, 0x0c0f1626u, 0x0c0f1633u);
    host_overlay_round_frame(px, py, pw, ph, 28.0f, 1.0f, 0xffffff66u, 0xffffff17u);
    host_overlay_round_rect(px + pw - 86.0f, py + 26.0f, 42.0f, 42.0f, 21.0f, 0xe9eefad0u, 0xc8d6f0a8u);  // the moon
    host_overlay_ui_text(kOverlayFontTitle, px + 30.0f, py + 18.0f, 46.0f, kText, "Helia's Dream");
    host_overlay_ui_text(kOverlayFontBody, px + 32.0f, py + 64.0f, 16.0f, kMuted, menu_tr("Settings apply live in the game"));
    {
        const std::string online = hle_np_online_status();
        const float ow = host_overlay_ui_text_width(kOverlayFontBody, 16.0f, online.c_str());
        host_overlay_ui_text(kOverlayFontBody, px + pw - 110.0f - ow, py + 38.0f, 16.0f, kSoft, online.c_str());
    }
    // Tabs: a dark track, the chosen section a bright glass bead.
    host_overlay_round_rect(px + 28.0f, tab_y - 5.0f, tab_x[g_section_count - 1] + tab_w[g_section_count - 1] + 5.0f - (px + 28.0f),
                            tab_h + 10.0f, (tab_h + 10.0f) * 0.5f, 0x0000005cu, 0x00000047u);
    for (int t = 0; t < g_section_count; ++t) {
        const bool on = t == sec;
        if (on) host_overlay_round_rect(tab_x[t], tab_y, tab_w[t], tab_h, tab_h * 0.5f, 0xffffffebu, 0xd6e0f2d9u);
        host_overlay_ui_text(kOverlayFontBold, tab_x[t] + 17.0f, tab_y + (tab_h - kTabFont * 1.2f) * 0.5f, kTabFont,
                             on ? 0x0b0d12ffu : 0xd5dbe6ffu, section_name(t));
    }

    const float text_dy = (row_h - 21.0f) * 0.5f;
    for (int i = 0; i < n; ++i) {
        const int r = rows[i];
        const Row& row = g_rows[r];
        const float x = g_row_xs[r], w = g_row_ws[r], y = g_row_y[r];
        const bool sel = r == g_sel;
        if (row.kind != Row::Info) {
            if (sel) {
                host_overlay_round_rect(x, y, w, row_h, 14.0f, 0xa9bfe352u, 0xa9bfe32eu);
                host_overlay_round_frame(x, y, w, row_h, 14.0f, 1.0f, 0xc9d6ee8cu, 0xa9bfe338u);
            } else {
                host_overlay_round_rect(x, y, w, row_h, 14.0f, 0x0000003du, 0x00000029u);
                host_overlay_round_frame(x, y, w, row_h, 14.0f, 1.0f, 0xffffff17u, 0xffffff08u);
            }
        }
        const std::uint32_t label_c = sel ? kText : kSoft;
        const float lx = x + 18.0f, right = x + w - 16.0f;
        if (row.kind == Row::Close || row.kind == Row::Action) {
            const bool busy = row.kind == Row::Action && !action_idle(row.field);
            host_overlay_ui_text(kOverlayFontBold, lx, y + text_dy, 19.0f, busy ? 0x6a7080ffu : label_c, menu_tr(row.text));
            continue;
        }
        if (row.kind == Row::Info) {
            const std::string line = row.field == 11  ? updater::detail()
                                     : row.field == 10 ? updater::status()
                                     : row.field == 1  ? account_detail()
                                                       : account_status();
            host_overlay_ui_text(kOverlayFontBody, lx, y + text_dy, 17.0f, kMuted, line.c_str());
            continue;
        }
        if (row.kind == Row::Text) {
            host_overlay_ui_text(kOverlayFontBody, lx, y + text_dy, 19.0f, label_c, menu_tr(row.text));
            std::string shown = g_field[row.field];
            if (g_edit_field == row.field) shown += "_";
            if (shown.empty()) shown = menu_tr("(type here)");
            const float vw = host_overlay_ui_text_width(kOverlayFontBold, 18.0f, shown.c_str());
            const float vx = right - 16.0f - vw;
            host_overlay_round_rect(vx - 16.0f, y + 6.0f, vw + 32.0f, row_h - 12.0f, (row_h - 12.0f) * 0.5f, 0x0000004du, 0x00000040u);
            host_overlay_ui_text(kOverlayFontBold, vx, y + text_dy, 18.0f, g_edit_field == row.field ? kAccent : sel ? kText : kSoft,
                                 shown.c_str());
            continue;
        }
        const Setting& s = g_set[row.setting];
        const float lw = host_overlay_ui_text(kOverlayFontBody, lx, y + text_dy, 19.0f, label_c, menu_tr(s.label));
        const bool toggle = s.values.size() == 2 &&
                            ((std::strcmp(s.values[0], "On") == 0 && std::strcmp(s.values[1], "Off") == 0) ||
                             (std::strcmp(s.values[0], "Off") == 0 && std::strcmp(s.values[1], "On") == 0));
        if (toggle) {
            const bool on = reads_on(s, s.index);
            const float tw = 48.0f, th = 26.0f, tx = right - tw, ty = y + (row_h - th) * 0.5f;
            host_overlay_round_rect(tx, ty, tw, th, th * 0.5f, on ? 0xbccdeaffu : 0xffffff26u, on ? 0xa9bfe3ffu : 0xffffff1au);
            if (!on) host_overlay_round_frame(tx, ty, tw, th, th * 0.5f, 1.0f, 0xffffff40u, 0xffffff26u);
            const float kx = on ? tx + tw - th + 3.0f : tx + 3.0f;
            host_overlay_round_rect(kx, ty + 3.0f, th - 6.0f, th - 6.0f, (th - 6.0f) * 0.5f, on ? 0x0b0d12ffu : 0xe9edf5ffu,
                                    on ? 0x0b0d12ffu : 0xc3cad6ffu);
            g_row_vl[r] = g_row_vm[r] = 0.0f;  // a click anywhere flips it
            if (sel && pad_editing()) host_overlay_round_frame(tx - 4.0f, ty - 4.0f, tw + 8.0f, th + 8.0f, th * 0.5f + 4.0f, 2.0f, kAccent, kAccent);
        } else {
            const char* v = s.values[static_cast<std::size_t>(s.index)];
            if (std::strcmp(s.key, "window_mode") == 0 && std::strcmp(v, "Fullscreen") == 0) v = "Fullscreen (borderless)";
            if (row.setting != kMenuLanguage) v = menu_tr(v);  // each language keeps its own name
            const float vw = host_overlay_ui_text_width(kOverlayFontBold, 18.0f, v);
            const float pr = right + 4.0f, pl = pr - vw - 66.0f;
            const float ty = y + 6.0f, th = row_h - 12.0f;
            host_overlay_round_rect(pl, ty, pr - pl, th, th * 0.5f, sel ? 0x00000066u : 0x00000047u, sel ? 0x00000052u : 0x00000038u);
            if (sel && pad_editing()) {
                // Selected on a controller: Left / Right change it.
                host_overlay_round_frame(pl, ty, pr - pl, th, th * 0.5f, 2.0f, kAccent, kAccent);
            } else {
                host_overlay_round_frame(pl, ty, pr - pl, th, th * 0.5f, 1.0f, sel ? 0xc9d6ee66u : 0xffffff1cu, 0xffffff0fu);
            }
            host_overlay_ui_text(kOverlayFontBold, pl + 33.0f, y + text_dy, 18.0f, sel ? kText : kSoft, v);
            const std::uint32_t chev_on = sel ? kAccent : 0x8b94a6ffu, chev_off = 0xffffff1fu;
            const float cy = y + row_h * 0.5f;
            const std::uint32_t back = s.index > 0 ? chev_on : chev_off;
            const std::uint32_t fwd = s.index + 1 < static_cast<int>(s.values.size()) ? chev_on : chev_off;
            host_overlay_tri(pl + 21.0f, cy - 6.0f, pl + 21.0f, cy + 6.0f, pl + 14.0f, cy, back);
            host_overlay_tri(pr - 21.0f, cy - 6.0f, pr - 21.0f, cy + 6.0f, pr - 14.0f, cy, fwd);
            g_row_vl[r] = pl - 10.0f;
            g_row_vm[r] = (pl + pr) * 0.5f;
        }
        if (waits_for_restart(row.setting, s.index)) {
            const char* restart = menu_tr("RESTART");
            const float rx = lx + lw + 10.0f, rw = host_overlay_ui_text_width(kOverlayFontBold, 12.0f, restart) + 14.0f;
            host_overlay_round_rect(rx, y + (row_h - 20.0f) * 0.5f, rw, 20.0f, 10.0f, 0xffffff1au, 0xffffff12u);
            host_overlay_ui_text(kOverlayFontBold, rx + 7.0f, y + (row_h - 20.0f) * 0.5f + 3.0f, 12.0f, kMuted, restart);
        }
    }
    g_laid_out = true;

    // The note for whatever is selected, in a fixed place, so the rows above
    // never move as the selection does.
    const int id = g_rows[g_sel].setting;
    const char* note = id >= 0 ? g_set[id].note : "Saves on close.";
    if (g_rows[g_sel].kind == Row::Text) note = "Enter or click to type; Enter keeps it, Escape drops it.";
    if (g_rows[g_sel].kind == Row::Action) {
        const int f = g_rows[g_sel].field;
        note = f >= 10 ? kUpdateNotes[f - 10] : kNotes[f];
    }
    // The note in a well of its own, like a launcher tooltip.
    const float ny = py + ph - kFoot + 18.0f;
    host_overlay_round_rect(px + 28.0f, ny, pw - 56.0f, 78.0f, 16.0f, 0x00000047u, 0x00000033u);
    host_overlay_round_frame(px + 28.0f, ny, pw - 56.0f, 78.0f, 16.0f, 1.0f, 0xffffff1au, 0xffffff0au);
    if (note) {
        // Wrapped by the monospace measure (about as wide as the proportional
        // face at this size), at most three lines, the last cut with "...".
        const auto lines = wrap_note(menu_tr(note), pw - 100.0f, 0.66f, 3);
        for (std::size_t i = 0; i < lines.size(); ++i)
            host_overlay_ui_text(kOverlayFontBody, px + 46.0f, ny + 12.0f + 21.0f * static_cast<float>(i), 17.0f, kSoft, lines[i].c_str());
    }
    // rc2: each selection change in the log with the overlay's size (the
    // F10 bug was a selection that took the vertices past the buffer).
    {
        static int last_sel = -1;
        if (g_sel != last_sel) {
            last_sel = g_sel;
            const Row& r = g_rows[g_sel];
            host_log("options: selected %s (overlay %zu vertices so far this frame, %zu last frame)",
                     r.setting >= 0 ? g_set[r.setting].key : r.text ? r.text : "(info)", host_overlay_built_vertices(),
                     host_overlay_shown_vertices());
        }
    }
    // The key guide as small pills.
    {
        float kx = px + 30.0f;
        const float ky = py + ph - 40.0f;
        std::vector<std::pair<std::string, const char*>> keys;
        if (host_input_device() == InputDevice::Pad) {
            // A controller's buttons: the icons' set (pad_icons, Auto following
            // the pad) and the region's confirm and back.
            const int icons = g_set[kPadIcons].index;
            const bool ps = icons == 1 || (icons == 0 && host_pad_playstation());
            const bool circle_confirms = menu_confirm_button() == 0x2000u;  // the pad's Circle bit (engine/menu_pointer.h)
            const char* confirm = ps ? (circle_confirms ? "Circle" : "Cross") : (circle_confirms ? "B" : "A");
            const char* back = ps ? (circle_confirms ? "Cross" : "Circle") : (circle_confirms ? "A" : "B");
            const char* tabs = ps ? "L1 / R1" : "LB / RB";
            if (pad_editing()) {
                keys = {{"Left / Right", "change"}, {std::string(confirm) + " / " + back, "done"}, {tabs, "section"}};
            } else {
                keys = {{"D-pad", "move"}, {tabs, "section"}, {confirm, "change"}, {back, "closes"}};
            }
        } else {
            for (const auto& k : kKeys) keys.emplace_back(k[0], k[1]);
        }
        for (const auto& k : keys) {
            const float kw = host_overlay_ui_text_width(kOverlayFontBold, 14.0f, k.first.c_str());
            host_overlay_round_rect(kx, ky - 4.0f, kw + 20.0f, 24.0f, 12.0f, 0xffffff24u, 0xffffff17u);
            host_overlay_ui_text(kOverlayFontBold, kx + 10.0f, ky, 14.0f, kText, k.first.c_str());
            kx += kw + 28.0f;
            kx += host_overlay_ui_text(kOverlayFontBody, kx, ky, 14.0f, kMuted, menu_tr(k.second)) + 26.0f;
        }
    }
}
}  // namespace

HostSettings host_settings() {
    std::lock_guard<std::mutex> lk(g_settings_mu);
    return g_settings;
}

namespace {
// Which way round a two-value setting reads comes from the values themselves,
// not a list of ids kept in step by hand: a new {"On","Off"} setting was
// silently inverted by that list, which is the sort of bug that only shows up
// as "the option does the opposite".
bool reads_on(const Setting& s, int index) {
    const std::size_t i = static_cast<std::size_t>(index);
    if (i < s.values.size()) {
        if (std::strcmp(s.values[i], "On") == 0) return true;
        if (std::strcmp(s.values[i], "Off") == 0) return false;
    }
    return index != 0;  // anything else: the first value is the "no"
}

int setting_id(const char* key) {
    for (int id = 0; key && id < kSettingCount; ++id) {
        if (std::strcmp(key, g_set[id].key) == 0) return id;
    }
    return -1;
}

}  // namespace

std::uint64_t host_opt_serial() { return g_serial.load(std::memory_order_acquire); }

void host_opt_fsr4_unavailable() {
    if (g_fsr4_unavailable.exchange(true)) return;
    g_serial.fetch_add(1, std::memory_order_release);
    rebuild_settings();
}

bool host_opt_default_get(const char* key) {
    const int id = setting_id(key);
    return id >= 0 && reads_on(g_set[id], g_default_index[static_cast<std::size_t>(id)]);
}
int host_opt_default_index(const char* key) {
    const int id = setting_id(key);
    return id < 0 ? -1 : g_default_index[static_cast<std::size_t>(id)];
}
int host_opt_default_frame_cap() {
    return fps_from_label(g_set[kFrameCap].values[static_cast<std::size_t>(g_default_index[kFrameCap])]);
}
int host_opt_default_resolution_index() { return g_default_index[kResolution]; }

bool host_opt_get(const char* key) {
    const int id = setting_id(key);
    return id >= 0 && reads_on(g_set[id], g_set[id].index);
}

bool host_opt_resolution(int* w, int* h) {
    const HostSettings hs = host_settings();
    if (hs.res_width <= 0 || hs.res_height <= 0) return false;
    *w = hs.res_width;
    *h = hs.res_height;
    return true;
}

int host_opt_resolution_index() { return g_set[kResolution].index; }

void host_opt_set_resolution_index(int i) {
    if (i < 0 || static_cast<std::size_t>(i) >= g_set[kResolution].values.size()) {
        return;
    }
    if (i == g_set[kResolution].index) return;
    set_index(kResolution, i, false);
    save();
}

int host_opt_frame_cap() {
    return fps_from_label(g_set[kFrameCap].values[static_cast<std::size_t>(g_set[kFrameCap].index)]);
}

void host_opt_set_frame_cap(int fps) {
    const int i = fps_index(fps);
    if (i < 0) {
        return;
    }
    if (i == g_set[kFrameCap].index) return;
    set_index(kFrameCap, i, false);
    save();
}

void host_opt_set(const char* key, bool on) {
    if (!key) {
        return;
    }
    for (int id = 0; id < kSettingCount; ++id) {
        if (std::strcmp(key, g_set[id].key) != 0) {
            continue;
        }
        // Which index is "on" comes from the values, as in host_opt_get: a
        // hand-kept list of the {"On","Off"} settings missed Mouse camera and
        // the four graphics switches, so turning one of those on from the
        // game's menu saved it off. Two values that are not On/Off
        // (Windowed/Fullscreen) read the second as on.
        const Setting& s = g_set[id];
        int want = on ? 1 : 0;
        for (std::size_t v = 0; v < s.values.size(); ++v) {
            if (std::strcmp(s.values[v], on ? "On" : "Off") == 0) {
                want = static_cast<int>(v);
            }
        }
        if (want == s.index) return;  // the setup window sets every one; only a change is written
        set_index(id, want, false);
        // The F10 screen saves when it closes; a row in the game's menu has no
        // such moment, so a change made there is written now or not at all.
        save();
        return;
    }
}

int host_opt_index(const char* key) {
    for (int id = 0; key && id < kSettingCount; ++id) {
        if (std::strcmp(key, g_set[id].key) == 0) return g_set[id].index;
    }
    return -1;
}

void host_opt_set_index(const char* key, int index) {
    for (int id = 0; key && id < kSettingCount; ++id) {
        if (std::strcmp(key, g_set[id].key) != 0) continue;
        if (index < 0 || static_cast<std::size_t>(index) >= g_set[id].values.size()) return;
        if (index == g_set[id].index) return;
        set_index(id, index, false);
        save();
        return;
    }
}

std::string host_options_dump() {
    std::string out;
    for (int id = 0; id < kSettingCount; ++id) {
        const Setting& st = g_set[id];
        const std::size_t i = static_cast<std::size_t>(st.index);
        out += std::string(st.key) + " = " + (i < st.values.size() ? st.values[i] : "?") + "\n";
    }
    return out;
}

void host_options_save_now() {
    g_dirty = true;
    save();
}

bool host_steam_deck() {
    static const bool deck = [] {
        if (const char* e = std::getenv("HELIAS_STEAM_DECK"); e && e[0]) return e[0] == '1';
        // The firmware's vendor and product: Valve's Jupiter (the LCD Deck) or
        // Galileo (the OLED one). Not any Valve machine - another has another
        // GPU, and these two configurations are this one's.
        std::string vendor, product;
#if defined(_WIN32)
        const auto bios = [](const char* name) {
            char buf[128] = {};
            DWORD size = sizeof(buf);
            if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", name, RRF_RT_REG_SZ, nullptr, buf, &size) !=
                ERROR_SUCCESS)
                return std::string();
            return std::string(buf);
        };
        vendor = bios("SystemManufacturer");
        product = bios("SystemProductName");
#else
        const auto line = [](const char* path) {
            std::ifstream f(path);
            std::string s;
            std::getline(f, s);
            return s;
        };
        vendor = line("/sys/devices/virtual/dmi/id/sys_vendor");
        product = line("/sys/devices/virtual/dmi/id/product_name");
#endif
        return vendor == "Valve" && (product == "Jupiter" || product == "Galileo");
    }();
    return deck;
}

namespace {
// The Deck's configurations, by the frame rate: 60 or more (or uncapped) is 60 fps.
int deck_profile_of(int fps) { return fps == 0 || fps >= 60 ? 1 : 0; }
}  // namespace

int host_deck_profile() { return deck_profile_of(host_opt_frame_cap()); }
int host_deck_default_profile() { return deck_profile_of(host_opt_default_frame_cap()); }

void host_set_deck_profile(int profile) {
    const bool sixty = profile == 1;
    if (const int i = fps_index(sixty ? 60 : 30); i >= 0 && i != g_set[kFrameCap].index) set_index(kFrameCap, i, false);
    if (const int m = sixty ? 2 : 1; m != g_set[kModelDetail].index) set_index(kModelDetail, m, false);  // Low, Normal
    const char* const size = sixty ? "960x600" : "1280x800";
    for (std::size_t v = 0; v < g_set[kResolution].values.size(); ++v) {
        if (std::strcmp(g_set[kResolution].values[v], size) != 0 || static_cast<int>(v) == g_set[kResolution].index) continue;
        // Both sizes are known to fit the Deck: no keep-or-revert question,
        // and one still waiting is settled by this.
        g_res_prev = -1;
        g_res_reverting = true;
        set_index(kResolution, static_cast<int>(v), false);
        g_res_reverting = false;
    }
    host_log("options: Steam Deck at %s", sixty ? "60 fps (960x600 upscaled, Model detail Low)" : "30 fps (1280x800)");
    save();
}

// The ACCOUNT actions for the setup window (host/options.h).
void host_account_action(int which, const std::string& name, const std::string& code) {
    if (which >= 0 && which <= 4) account_action(which, name, code);
}
bool host_account_busy() { return g_acct_busy.load(); }
std::string host_account_signed_in() {
    return net::account_logged_in() ? "Signed in as " + net::account_name() : "Not signed in";
}
std::string host_account_outcome() {
    std::lock_guard<std::mutex> lk(g_acct_mu);
    return g_acct_status;
}
std::string host_account_detail() { return account_detail(); }
bool host_account_take_save() { return g_acct_save.exchange(false); }

float host_opt_mip_bias() { return g_mip_bias.load(std::memory_order_relaxed); }
unsigned host_opt_submit_split() { return g_submit_split.load(std::memory_order_relaxed); }
bool host_opt_fsr4_sharp_motion() { return g_fsr4_sharp_motion.load(std::memory_order_relaxed); }

std::vector<std::string> host_options_menu_strings() {
    std::vector<std::string> out;
    const auto add = [&](const char* t) {
        if (!t || !*t) return;
        // Numbers and sizes need no translation.
        bool word = false;
        for (const char* c = t; *c; ++c) word = word || std::isalpha(static_cast<unsigned char>(*c));
        if (!word) return;
        for (const std::string& o : out)
            if (o == t) return;
        out.emplace_back(t);
    };
    for (int sec = 0; sec < g_section_count; ++sec) {
        const int was = menu_i18n_language();
        menu_i18n_set_language(0);
        add(section_name(sec));
        menu_i18n_set_language(was);
    }
    for (int id = 0; id < kSettingCount; ++id) {
        add(g_set[id].label);
        add(g_set[id].note);
        if (id == kMenuLanguage || id == kResolution) continue;
        for (const char* v : g_set[id].values) add(v);
    }
    for (int r = 0; r < kRowCount; ++r)
        if (g_rows[r].kind != Row::Header && g_rows[r].kind != Row::Option) add(g_rows[r].text);
    for (const char* n : kNotes) add(n);
    for (const char* n : kUpdateNotes) add(n);
    for (const auto& k : kKeys) add(k[1]);
    add("done");  // the controller's guide while a setting is selected
    for (const char* n : kOtherStrings) add(n);
    return out;
}
