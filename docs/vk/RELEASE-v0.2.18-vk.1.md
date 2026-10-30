# heliasdream-vk v0.2.18-vk.1

The first heliasdream-vk release since `v0.2.18-vk.0`. It is built on droogie's Helia's Dream v0.2.18
(GPLv3), for Bloodborne CUSA03173 1.09 on Linux.

## What's new
- **Temporal upscaling:** AMD FSR 3.1, and FSR 4.1.1.
  - FSR 4.1.1 is built once on your own PC from your own
    `amd_fidelityfx_upscaler_dx12.dll` (`tools/fsr4cap`). No AMD DLL or assets are in this
    release.
  - Upscaling quality presets: Native AA, Quality, Balanced, Performance and Ultra
    Performance. Sharpness is adjustable.
- **Post-processing at render size:** with FSR below Native AA, the game's depth of field,
  fog/colour pass and motion blur run at the render size, and FSR upscales after them. This
  saves about 0.7 ms a frame at 4K. Chromatic aberration, bloom, tonemap, grain and the HUD
  stay at full resolution. It falls back on its own when a frame doesn't fit.
- **Even frame pacing:** at a fixed frame rate (30/60/90/120), frames are presented on an
  even grid. In testing at 4K/120 on an RX 9070 XT (FSR 4.1.1 Performance):

  | | pacing on | pacing off |
  |---|---|---|
  | 1% low | 119.4-120.0 | 88-100 |
  | Frames over 8.5 ms per 30 s | 15-34 | ~1000-1350 |

  This stops lights and fog flickering on uneven frame times. It adds a few ms of latency.
- **Motion blur off is cheaper:** with motion blur off, its two passes become one copy.
- **Anti-aliasing Auto:** the game's own AA is off while FSR 4.1.1 upscales, and on
  otherwise.
- **Launcher (`--setup`) additions:**
  - **Even frame pacing** and **Post-processing at render size** switches, both on.
  - Next to the existing Upscaler, Upscaling quality, Upscaler sharpness and Anti-aliasing
    rows.

## Defaults
- **Graphics:** a new install, or one with no options file, starts with the game's own
  graphics. All of these are on:
  - motion blur, depth of field, chromatic aberration, vignette, ambient occlusion;
  - bloom 10, fog 10, saturation 5, AO strength 5;
  - model detail Normal, AA Auto.
- **Existing options files are left as they are.**
- **Upscaling:** the upscaler defaults to FSR 1 at Native AA, which is stock rendering. Pick
  FSR 3.1 or 4.1.1 and a quality preset in the launcher or in F10 > UPSCALING.
- Even frame pacing, post-processing at render size and the motion blur skip are on. They
  only act where they apply.

## Fallbacks
- Launcher: **Even frame pacing** off, or **Post-processing at render size** off.
- Environment variables, which win over the options: `HELIAS_PRESENT_PACE=0`,
  `HELIAS_POST_AT_RENDER=0`, `HELIAS_MOTION_BLUR_SKIP=0`.

## Unchanged
- Online play and sign-in (thehuntersdream.com) are untouched.
- No HDR changes. MangoHud works as before.

## Install
1. Keep a copy of your `heliasdream.bin`, `heliasdream-options.toml` and `~/.config/heliasdream/heliasdream.toml`.
2. Run `tar xzf heliasdream-vk-linux-v0.2.18-vk.1.tar.gz`.
3. Copy `heliasdream-vk-linux-v0.2.18-vk.1/heliasdream` over your `heliasdream.bin`.
4. Check the download with `sha256sum -c SHA256SUMS`.

## Known
- The first seconds after a loading screen are unpaced, on purpose (2 s).
- Post-processing at render size is new. If depth of field or fog look shifted or doubled,
  turn it off and report it.
