// heliasdream-vk: HD haptics and adaptive triggers for the DualSense and the
// DualSense Edge (docs/vk/PLAN.md, Phase X).
//
// HD haptics: plugged in by USB, either controller is also a 4-channel sound
// card - channels 1 and 2 its speaker, 3 and 4 the left and right voice-coil
// motors - and what plays on 3 and 4 is what the hands feel. The game is a
// PS4 title and knows only the DualShock 4's two rumble motors, so the
// waveform is made here from two things: the game's rumble (each motor a tone
// the actuators carry well) and the game's sound effects - the main audio
// port's 7.1 mix, its low end, with a transient detector so impacts, shots
// and footsteps come through and steady rain, wind and fire do not. The game
// opens a BGM port but plays its music through the main one too, in the front
// pair: those two pass only far sharper hits than the other channels. Over
// Bluetooth, or with any other controller, there is no such device and the
// classic rumble runs (Haptics: Rumble).
//
// Adaptive triggers: SDL's DualSense effect report (SDL_SendGamepadEffect),
// USB or Bluetooth. L2, the gun, has a trigger break; R2, the heavy attack,
// a light resistance that turns into a rising hum while it is held to charge.
// Both relax in loading screens and heliasdream's menus.
#pragma once

#include <cstddef>
#include <cstdint>

// The game's audio thread, every main-port buffer: 8-channel float frames
// (FL FR C LFE SL SR BL BR) at 48 kHz, already at the port's volume.
void haptics_game_audio(const float* frames, unsigned count, float gain);

// The game's pad thread (scePadSetVibration). True when HD haptics took it -
// the caller then leaves the controller's own rumble alone, which would turn
// the audio haptics off until the next report.
bool haptics_rumble(std::uint8_t small, std::uint8_t large);

// The Haptics setting as it stands: 0 Off, 1 Rumble, 2 HD.
int haptics_mode();

// A controller came or went (the window's pump thread): whether a DualSense
// is now connected by USB - only then is HD tried, since one on Bluetooth
// ignores its USB sound card even with the cable in - and look again.
void haptics_devices_changed(bool dualsense_on_usb);

// Whether a USB vendor:product is a DualSense or DualSense Edge.
bool haptics_is_dualsense(std::uint16_t vendor, std::uint16_t product);

// The pump thread, every pad update, for one DualSense: what its triggers
// should do now, as the 11-byte effect blocks of the controller's output
// report. `l2` and `r2` are 0..1; `relaxed` is a loading screen or a menu.
// `id` keeps each controller's charge timing apart.
struct TriggerEffects {
    std::uint8_t left[11]{};
    std::uint8_t right[11]{};
};
TriggerEffects haptics_triggers(std::uint32_t id, float l2, float r2, bool relaxed);

// How far L2 has to travel before the game sees it pressed, 0..1, while its
// trigger break is on (the shot fires at the snap, not before it); 0 when
// it is not, and the usual threshold applies. HELIAS_L2_FIRE=0.7 moves it.
float haptics_l2_fire_point();
// The same for R2: just into the light wall it has while the character
// stands, so the wall is felt before the attack goes. HELIAS_R2_FIRE moves it.
float haptics_r2_fire_point();

// A tap for the game's menus (the title menu, the pause menu, the inventory,
// the System list - every list the game draws): 0 the cursor moved, 1 a
// decide, 2 a back. Light and short; felt only with HD haptics live.
void haptics_menu_tick(int kind);

// Counts the HD outputs opened (at the start, after a replug, after the
// setting comes back to HD); 0 while none is open. The pump thread sends each
// DualSense the controller's reset when it changes (host/window.cpp): the
// report pair a rumble's start and stop make, which leaves the motors playing
// sound again - a controller a crash or another program left on its rumble
// emulation would otherwise stay silent.
std::uint32_t haptics_hd_generation();

// Before the audio subsystem shuts down: closes the haptics output.
void haptics_shutdown();

// Tests: the trigger effect blocks, as the controller's firmware reads them.
void haptics_trigger_off(std::uint8_t out[11]);
void haptics_trigger_feedback(std::uint8_t out[11], int start, int strength);
void haptics_trigger_weapon(std::uint8_t out[11], int start, int end, int strength);
void haptics_trigger_vibration(std::uint8_t out[11], int position, int amplitude, int frequency);
