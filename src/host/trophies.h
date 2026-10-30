#pragma once

// Helia's Dream: Bloodborne's trophies as the PS4 lists them (CUSA03173's 40,
// their names and grades), and the in-game pop-up when one is earned - a
// frosted glass card in a corner of the screen, like a console's, drawn by the
// host overlay (host/overlay.h) so it shows in any window mode and in
// recordings. The unlocks themselves are hle/system.cpp's (data/trophies.txt).

#include <cstdint>

struct TrophyInfo {
    int id;
    const char* name;
    const char* description;
    int grade;  // the PS4's: 1 platinum, 2 gold, 3 silver, 4 bronze
    bool dlc;   // The Old Hunters (not needed for the platinum)
};

// The trophy with this id, or null.
const TrophyInfo* trophy_info(int id);
int trophy_count();
// How many of each grade (1..4).
int trophy_count_of_grade(int grade);

// The trophies' own icons, read from the game's trophy pack
// (/app0/sce_sys/trophy/trophy00.trp, TROP<id>.PNG) once, on the first call:
// RGBA rows of `cols` x `rows` cells of `cell` pixels, trophy id i at cell i
// (left to right, then down), each a 240-pixel icon halved, its corners
// rounded. False with no pack or no PNG loader; nothing is written to disk.
bool trophy_icon_sheet(const std::uint8_t** rgba, int* w, int* h, int* cell, int* cols);

// A trophy was just earned: queue its pop-up. `earned` / `total` are the
// progress after it. Any thread.
void trophy_popup(int id, int earned, int total);

// The window's pump, every frame: the pop-up being shown, if any, into the
// overlay being built. `display_w`, `display_h`: the overlay's size.
void trophy_popup_frame(float display_w, float display_h);
