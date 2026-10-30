# heliasdream-vk v0.2.18-vk.2

The second heliasdream-vk release. Built on droogie's Helia's Dream v0.2.18 (GPLv3), for Bloodborne
CUSA03173 1.09 on Linux. On top of vk.1 it has the rc2-rc7 performance work, a round of
fixes, and DualSense HD haptics and adaptive triggers.

## What's new

### DualSense and DualSense Edge: HD haptics and adaptive triggers
- **HD haptics (USB only).** The controller's haptic motors play:
  - the game's own rumble, converted into haptic waveforms;
  - its sound effects: hits, gunshots, impacts, footsteps, doors. Music and steady ambience
    (wind, rain, area music) are filtered out. Left and right follow where a sound comes from.
  - light taps for moving through menus, and firmer ones for confirm and back.
- **Adaptive triggers**, driven by what the character is actually doing (its animation):
  - **R2:** a light wall where the attack registers, so it is felt before the attack goes.
    During a charged heavy attack: resistance on the wind-up, a rumble that grows while it
    charges, a strong kick as the charged swing goes. Free once an attack has played while
    R2 is still held, until it is let go.
  - **L2:** a trigger break, with a recoil kick on each real shot. Slack the moment no
    Quicksilver Bullets (normal or Blood Bullets) are carried; the break comes back as soon
    as there are some.
  - Both relax in menus and loading screens.
- **Settings** (F10 > CONTROLLER, all live): Haptics (Off / Rumble / HD), Haptics strength,
  Adaptive triggers, Trigger strength, Menu haptics.
- Helia's Dream raises the controller's own playback volume (Linux leaves it at -20 dB), and says on
  screen when a DualSense is on Bluetooth, where HD haptics cannot work.

### Performance
- At 4K output with FSR 4.1.1 Performance and the 120 cap on an RX 9070 XT + Ryzen 9 5950X:
  about 121-122 fps average with 1% lows around 110 in Old Yharnam and the Hunter's Dream
  (test sessions of 2026-10-21/22). Heavy smoke can still dip below 120: the
  command-processor thread (`bb-cp0`) is the limit.
- From rc2-rc7: a frame-latency limit and a steadier pacer, hot threads on the preferred
  cores, asynchronous logging, cheaper GPU submissions (dma-buf imports), the periodic
  ~12 ms frame gone, a cached key path in the command processor, and a PGO-ready build.

### Fixes
- **Upscaling quality changes live** in the game; no restart.
- **Starting at Native AA no longer blocks the upscaling presets:** the display buffers are
  sized for the largest connected display on Linux too.
- **Anti-aliasing Auto** keeps the game's AA at Native AA, where no upscaler runs.
- A hang in the loading screen and on exit with lazy pass barriers, a layout change inside a
  render pass, and an invalid occlusion shader, all found with the validation layer.
- The in-game PC Upscaling help says what the settings do now.

## Install
1. Keep a copy of your `heliasdream.bin`, `~/.config/heliasdream/heliasdream-options.toml` and
   `~/.config/heliasdream/heliasdream.toml`.
2. Check the download: `sha256sum -c SHA256SUMS`.
3. `tar xzf heliasdream-vk-linux-v0.2.18-vk.2.tar.gz`, then copy
   `heliasdream-vk-linux-v0.2.18-vk.2/heliasdream` over your `heliasdream.bin`.

## For HD haptics
- Plug the DualSense in by USB **with its Bluetooth link off.** A controller already
  connected over Bluetooth stays on Bluetooth when the cable goes in (it only charges):
  disconnect it in your Bluetooth settings, then replug the cable.
- Its sound card has to be in the **Direct** profile (KDE: System Settings > Sound), not
  "Pro Audio": there the motors cannot be reached. The log says so when it is.
- Over Bluetooth, or with another controller, Haptics: HD is the classic rumble.

## Unchanged
- Online play and sign-in (thehuntersdream.com) are untouched.
- Lazy pass barriers stay off by default (they flashed white blocks at load-in).

## Known
See `docs/KNOWN_ISSUES.md`: Vicar Amelia's rainbow textures (not yet narrowed down); the
adaptive triggers were tuned on one weapon and the pistol, so other weapons' charged attacks
and firearms may need their own animations added.
