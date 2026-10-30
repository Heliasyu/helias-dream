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
- Testing the Windows build on more PCs.

## Install

### What you need
- **Your own copy of Bloodborne** (CUSA03173, or CUSA00900 in other regions) with the
  **1.09 update**, dumped from your own PS4. You need two things from it:
  - the game folder with the update applied: the folder that contains `dvdroot_ps4`;
  - the 1.09 `eboot.bin` **decrypted** to an ELF (SHA-256 `941f887a…aaa5ae7`).

  No game files are included here.
- **A PC** running Windows 10/11 or 64-bit Linux, with a Vulkan 1.3 graphics driver. 16 GB
  of RAM and 8 GB of video memory are recommended.

### Windows
1. Download `helias-dream-windows-<version>.zip` from
   [Releases](https://github.com/Heliasyu/helias-dream/releases/latest).
2. Unzip it into a folder of its own, for example `C:\Games\HeliasDream`.
3. Run `HeliasDream.exe`. If Windows says "Windows protected your PC", click
   **More info**, then **Run anyway**.
4. On the **Setup** page, pick the game folder and the decrypted eboot, then press **Play**.
5. The first time Windows Firewall asks, allow access on both networks. Other players reach
   you on UDP port 9307.

### Linux
1. Download `helias-dream-linux-<version>.tar.gz` from
   [Releases](https://github.com/Heliasyu/helias-dream/releases/latest). It needs glibc 2.38
   or newer: Ubuntu 24.04, Mint 22, Fedora 39, Arch or SteamOS.
2. Unpack it into a folder of its own:
   `tar xzf helias-dream-linux-<version>.tar.gz -C ~/Games`
3. Run `./HeliasDream` from that folder. On the **Setup** page, pick the game folder and the
   decrypted eboot, then press **Play**.
4. `./run-heliasdream.sh` starts the game without the launcher and writes its log to
   `logs/`.

### Online
Online play uses the community server, [thehuntersdream.com](https://thehuntersdream.com).
Link this PC once on the launcher's **Online** page (sign in with Discord, no password). After
that, co-op, invasions, messages and bloodstains work as on the console, with other PC
players.

### Optional extras
- **FSR 4.1.1:** build its assets once from your own copy of AMD's FSR 4 DLL with
  `./build-fsr4.sh` in the Linux release (see `tools/fsr4cap`). Until then it falls back
  to FSR 3.1, and FSR 3.1, FSR 4 (INT8) and Native AA work right away.
- **DualSense HD haptics:** plug the controller in by USB (not Bluetooth) and set Haptics to
  HD. On Windows, set the controller's speaker to 4 channels (Sound settings > the
  controller's speaker > Properties > Advanced).
- **Steam / Steam Deck:** the launcher's **Add to Steam** button adds the game as a
  non-Steam game.

### Updating and where things are kept
To update, unpack the new release over the old folder, or into a new one. Settings, saves
and the online sign-in live outside the game folder, so they carry over:

| | Windows | Linux |
|---|---|---|
| Settings | `%APPDATA%\HeliasDream` | `~/.config/HeliasDream` |
| Saves and caches | `%LOCALAPPDATA%\HeliasDream\data` | `~/.local/share/HeliasDream/data` |
| Logs | `logs\` in the game folder | `logs/` in the game folder |

If something goes wrong, open an issue with the newest log from `logs` (it contains your IP
address, so share it privately if you prefer). Include your graphics card and what happened.

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
