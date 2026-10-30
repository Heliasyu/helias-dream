# Helia's Dream

**Helia's Dream** runs Bloodborne (CUSA03173, game version 1.09) natively on Linux and Windows, with
modern upscaling, high frame rates, online play, and DualSense HD haptics and adaptive
triggers.

## What it adds
- **Upscaling:** FSR 4.1.1, FSR 3.1 and FSR 1, with Native AA, Quality, Balanced,
  Performance and Ultra Performance presets and a sharpness slider. Presets change live, and
  the HUD and menus always stay at full resolution.
- **Frame rate caps:** 30, 60, 90, 120 or uncapped, with even frame pacing.
- **Online play:** co-op, invasions and messages through the community server
  (thehuntersdream.com).
- **DualSense and DualSense Edge haptics:** HD haptics over USB that you feel on hits,
  gunshots, footsteps and menu moves, with music and ambience filtered out. Adaptive
  triggers that follow your character: a charged heavy attack builds resistance and kicks on
  release, and the gun's trigger break goes slack when you're out of Quicksilver Bullets.
- **Launcher:** a setup window for display, upscaling and game options, plus an in-game
  settings overlay (F10).
- **Performance tools:** F11 saves a snapshot of the last 30 seconds (frame times, GPU and
  CPU time), and `bbtest` records full test sessions.

## Status
| | |
|---|---|
| Current build | [`v0.2.18-vk.3`](https://github.com/Heliasyu/helias-dream/releases/tag/v0.2.18-vk.3) (2026-10-30): Linux and Windows |
| Tested on | RX 9070 XT, Ryzen 9 5950X, KDE Plasma (Wayland) |
| Performance | 4K output, FSR 4.1.1 Performance, 120 cap: ~120-122 fps average, 1% lows ~110 in Old Yharnam |
| Known issues | Heavy smoke can dip below 120; some textures at Vicar Amelia. See [KNOWN_ISSUES](docs/vk/KNOWN_ISSUES.md). |

## In progress
- A steady 120 fps in the heaviest areas.
- A new launcher with a Bloodborne look.

## Install
You need your own Bloodborne game files (CUSA03173 with the 1.09 update, and its decrypted
eboot). None are included here.

**Windows 10/11:** download `helias-dream-windows-<version>.zip` from
[Releases](https://github.com/Heliasyu/helias-dream/releases), unzip it to a folder of its
own and run `HeliasDream.exe`. Pick the game folder and eboot, then Play. A bbhost install's
settings and saves are copied over the first time.

**Linux:** download the `helias-dream-linux-<version>.tar.gz`, unpack it and run
`./HeliasDream` (the launcher; `./run-heliasdream.sh` starts the engine alone). To update an existing install, copy the new `heliasdream` engine over
`HeliasDream.bin` (keep a backup).

FSR 3.1 works out of the box. FSR 4.1.1 needs its assets built once from your own copy of
AMD's DLL (`./build-fsr4.sh` in the Linux release, see `tools/fsr4cap`). They go in
`~/.local/share/HeliasDream/fsr4_411` on Linux and `%LOCALAPPDATA%\HeliasDream\fsr4_411` on Windows.
For HD haptics, connect the DualSense by USB with Bluetooth off. On Windows, its speaker must
be set to 4 channels in Sound settings.

## Building
- Linux: `tools/ci_build.sh` (Clang, CMake, Ninja; on Ubuntu 24.04 it installs the packages).
- Windows, cross-built from Linux: `tools/ci_windows.sh` (Clang, lld, mingw-w64, and dotnet
  for the launcher). The result is `build/win/helias-dream-windows-<rev>.zip`.

## Documentation
[CHANGELOG](CHANGELOG.md) · [Known issues](docs/vk/KNOWN_ISSUES.md) ·
[Plan](docs/vk/PLAN.md) · [Testing](docs/vk/TESTING.md) · [Dev log](docs/vk/DEVLOG.md)

## Credits
- [droogie/bbhost](https://github.com/droogie/bbhost): the native Bloodborne host this
  project grew from; its networking backend is used as it is. Its original README: [README.upstream.md](README.upstream.md).
- [bbport](https://github.com/deadinside28/bloodborne_pc) (GPL-2.0-or-later): the FSR 4.1.1
  replay and asset build, and findings behind the motion vectors.
- [FireBurn/FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) (MIT): FSR 3.1 on Vulkan,
  with the AMD FidelityFX SDK.
- [HansKristian-Work/dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv) (MIT):
  used when building the FSR 4.1.1 assets.
- Kyo's Decoupled UI patches (via
  [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats)),
  [IFreemz](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR),
  [Supermedo](https://github.com/Supermedo/bloodborne_pc) and
  [Awakenchan](https://github.com/Awakenchan/BloodBorneButPc): findings behind the
  scene/UI split.

Details: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License
GPL-3.0. See [LICENSE](LICENSE).
