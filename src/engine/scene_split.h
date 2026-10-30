#pragma once

// heliasdream-vk Phase 1: the scene at the render size, the UI at the output's.
//
// The output size is the Resolution setting - the res words, the GX output
// entry, Scaleform's viewports, the display picture - as in stock heliasdream. The
// render size is that divided by the upscaler preset's ratio (Quality 1.5,
// Balanced 1.7, Performance 2, Ultra Performance 3), when an upscaler is
// chosen and the preset is not Native AA. Then the scene contexts (G-buffer,
// depth, the HDR lighting and their half and quarter targets) are built at
// the render size; YEBIS's post chain, the UI and the display buffers stay at
// the output size; and the game's own pass that scales the HDR scene up to
// the output size before post-processing (shader ccbf44a6) does it, in this
// phase, as a plain bilinear copy (host/render.cpp).
//
// The writes are Kyo's Decoupled UI patches as a table
// (engine/scene_split_sites.h). They are made, or the game's bytes put back,
// by live_resolution's change at its quiet point (engine/live_resolution.cpp)
// - before the graphics manager exists at the start, so it is built at the
// render size. With the render size equal to the output (Upscaler Off, or
// Native AA) every site holds the bytes it had at load: stock.
//
// HELIAS_SCENE_SPLIT=0 never splits.

#include <cstdint>

struct ElfImage;

// At load, from live_resolution_install (after its own patches): checks the
// sites, keeps their bytes and makes the two stubs. False when the split
// cannot be used in this run (then the render size is always the output's).
bool scene_split_install(ElfImage* image);

// Before scene_split_install, for live resolution's display-buffer size: whether
// the settings will split the scene at an output of ow x oh at the start
// (HELIAS_SCENE_SPLIT and the eboot permitting).
bool scene_split_planned(const ElfImage* image, std::uint32_t ow, std::uint32_t oh);

// Whether the split can be used in this run.
bool scene_split_available();

// The render size the settings ask for at an output of ow x oh: the output
// itself when the split is unavailable, the upscaler is Off or the preset is
// Native AA - and, test3, when the display buffers are not the output's size
// (scene_split::display_fits_split; logged once per size).
void scene_split_wanted(std::uint32_t ow, std::uint32_t oh, std::uint32_t* rw, std::uint32_t* rh);

// The sites for a render size of rw x rh at an output of ow x oh: the split's
// bytes, or the game's own when they are equal. Main thread, at a point
// where none of the game's code at the sites can be running (the quiet
// point, or before the GX init). False when a write failed.
bool scene_split_apply(std::uint32_t rw, std::uint32_t rh, std::uint32_t ow, std::uint32_t oh);

// The render size the sites hold now (the output's when not split), and
// whether they hold a split. Any thread.
bool scene_split_active();
// Whether the settings ask for the split at the output's own size (Native
// TAA), as scene_split_wanted last found it. Any thread.
bool scene_split_forced();
// Whether a split asked for now (an upscaler below Native AA, or Native TAA)
// takes effect in this run: the split is available and the display buffers
// are the output's size (they are only when the run started split). Any thread.
bool scene_split_live();
void scene_split_render_size(std::uint32_t* w, std::uint32_t* h);
