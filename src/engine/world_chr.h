// Where the player and the camera are. WorldChrMan (singleton slot at Binary
// Ninja 0x593e878) keeps the
// main player's ChrIns at +0x60; a ChrIns's module container is at +0x3b0,
// and the module in its slot +0x68 holds the character's transform: the yaw
// in radians at +0x1d4, the position (x, y, z) at +0x1e0. The follow camera
// (ChrExFollowCam) is at +0x60 of the camera owner the pointer at +0x2830 of
// the object in slot 0x593e860 names: its position at +0x40, the point it
// looks at - the character raised by the camera's height - at +0xd0.
//
// The chain to the ChrIns, the module container and the camera fields
// (WorldChrMan, ChrIns, ChrExFollowCam) come from independent
// reverse-engineering of the eboot; the position field was found by a scan
// during a walking soak: the vector at +0x1e0 of module +0x68 (and its copies
// at +0x1f0 and +0x2b0) followed the camera's focus 1.42 below it in all 39
// samples, through a reload to another place.
#pragma once

#include <cstdint>

struct ElfImage;

void world_chr_install(ElfImage* image);
// False before the world has a player (the title, a load).
bool world_player_position(float xyz[3], float* yaw);
bool world_camera(float pos[3], float focus[3]);
// The animation the main player's SprjChrTimeActModule plays now (its +0x20),
// as the game numbers them: the motion category times 1,000,000 plus the
// animation (122032610: category 122, animation 032610 - a heavy attack's
// charge). Found by HELIAS_CHR_SCAN (round 10): R2 held runs 032600 (the
// wind-up), 032610 (the charge), then 032630 (the charged swing) or, let go
// early, 032620; R1 030000; the gun 117051000 / 117054500; standing
// 200000000..200004000. False before the world has a player.
bool world_player_anim(std::int32_t* anim);
// The Quicksilver Bullets the player carries, from the inventory in the
// local player's game data (GameDataMan +0x08): its entries are 16 bytes -
// a handle, the item's id tagged 0xB0000000, again tagged 0x40000000 (goods),
// and the count - and the bullets are goods 901 (HELIAS_FIND_BULLETS, rounds
// 14-16: an entry whose count hits zero is freed, and the next bullets take
// another one, so the entry is looked for, not remembered). Read on the main
// thread every 100 ms; -1 before the world has a player or when no read has
// been made. Any thread.
int world_player_bullets();
// The map block the main player is in, packed as the game keeps it at ChrIns
// +0x3f8 (m24_01_00_00 = 0x18010000). False before the world has a player.
bool world_player_block(std::uint32_t* block);

// Test-only (tools/mapval_plugin.c): moves the main player through the game's
// own warp (0x194b110 on the world manager at 0x593b148 - what the event
// system's warp and the debug PlayerWarp bot call), into `block` (0: the one
// it stands in; another block must be resident). Off unless
// HELIAS_TEST_WARP=1. Queued; world_chr_tick() runs it on the main thread at
// the start of the next frame. False when off or no player.
bool world_player_warp_queue(std::uint32_t block, const float xyz[3], float yaw);
void world_chr_tick();

// Test-only: the lamp travel the Hunter's Dream headstones do - the
// ReturnPointParam row `return_point` (a lamp's entity id + 1000, e.g.
// 2412951 for Central Yharnam's lamp 2411951) as the return point
// (0x196d110), the next load told to use it (0x196d0e0), then the game's
// WarpNextStage_Bonfire (0x172e050). Gated with the warp (HELIAS_TEST_WARP=1);
// queued for the main thread.
bool world_lamp_warp_queue(std::int32_t return_point);
// Turns on player_warp / lamp_warp outside tests: for an opt-in gameplay
// plugin (BB_PLUGIN_OPT_IN | BB_PLUGIN_GAMEPLAY) the player enabled.
void world_warp_allow();

// HELIAS_TEST_UNLOCK_LAMPS=1: every main-game lamp's lit flag (its event
// flag base + 10) set through the game's SetEventFlag at each world load, so
// the travel menu offers every lamp. The count set, and the lamps whose flag
// block the save does not have yet, go to the log.
