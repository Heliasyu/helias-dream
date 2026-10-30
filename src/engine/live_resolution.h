#pragma once

// Changing the render resolution while the game runs, the way Dark Souls III
// PC does on the same engine: the GX device's
// resize broadcast rebuilds the scene context's targets, the post-effect
// chain and the UI scale at the new size, at a point where no frame is being
// recorded or played. The display buffers are allocated once, at the largest
// size, and the window shows the top-left of them at the render size.
//
// Off with HELIAS_LIVE_RESOLUTION=0 (the resolution then applies on the next
// run, as before), or when a hook does not go in.

#include <cstdint>

struct ElfImage;

// At load, before the GX init runs. True when a live change can run; the
// loader then pins the UI-stage readers of the res words whatever the
// starting size, because the words will move.
bool live_resolution_install(ElfImage* image);

// True once installed and the game's GX device exists - a request made
// before that is kept and runs at the first frame end.
bool live_resolution_available();

// True when a change to w x h can happen in this run: installed, inside the
// display buffers, and no more pixels than the render-target heap was grown
// for at the start (3840x2160's, or the starting size's when larger). A
// larger size applies when the game next starts.
bool live_resolution_can(unsigned w, unsigned h);

// Ask for a new render size; any thread. It takes effect at the end of the
// next presented frame. A size live_resolution_can refuses is refused.
// With `confirm`, the game's own yes/no dialog then asks to keep it, the way
// Dark Souls III does (ten seconds, then it counts as no); the answer is
// taken with live_resolution_take_answer.
bool live_resolution_request(unsigned w, unsigned h, bool confirm = false);
// 0 while nothing has been answered; then once 1 (keep) or 2 (go back).
int live_resolution_take_answer();
// heliasdream-vk: on around the game's own System menu setting the resolution or
// the upscaler (then off): a request made meanwhile waits (bounded) for that
// menu's pick list to close before resizing. F10 and the setup window never
// set it, so their changes go as upstream's.
void live_resolution_from_game_menu(bool on);
// Back to the size shown before the last change that asked to be kept, for
// a "go back" whose old entry waits for the next start.
void live_resolution_go_back();

// heliasdream-vk: the upscaler or its preset changed - the scene's size
// (engine/scene_split.h) follows at the next quiet point, the output staying.
void live_resolution_scale_changed();
// The scene's size now: the render size when split, else the output's.
void live_resolution_scene(unsigned* w, unsigned* h);

// The display buffers' size (0 x 0 before live resolution sized them, or
// without it - then they are the starting size). The scene split needs them
// at the output's size (engine/scene_split_sites.h, display_fits_split).
void live_resolution_display(unsigned* w, unsigned* h);

// The render size now, and a counter that moves each time a change has been
// applied (0 before any).
std::uint64_t live_resolution_current(unsigned* w, unsigned* h);
