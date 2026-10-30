# Helia's Dream v0.2.18-vk.3

The first release under the Helia's Dream name, and the first with a **Windows** build.
Bloodborne CUSA03173 1.09, on Linux and Windows 10/11. You need your own game files (the
1.09 update and its decrypted eboot); none are included.

## Downloads
- **Windows:** `helias-dream-windows-v0.2.18-vk.3.zip`. Unzip it to a folder of its own and
  run `HeliasDream.exe`.
- **Linux:** `helias-dream-linux-v0.2.18-vk.3.tar.gz`. Unpack it and run `./HeliasDream`.
- `SHA256SUMS` lists both.

Settings and saves from a bbhost or earlier Helia's Dream install are copied over on the
first start; the old folders are kept.

## What's new since vk.2
- **Windows build:** the launcher and the engine, with FSR 3.1 and the in-game F10 menu.
- **Launcher:** display, graphics, controller and keyboard bindings, game setup, player
  profiles, save backups, Add to Steam, a Console page and update notices.
- **Upscaling:** FSR 4 (INT8), DLSS (NVIDIA's NGX, at run time), Native AA, and Texture
  detail. FSR 4 is now built on a worker thread, so it no longer freezes the game.
- **HDR output** that switches live, with UI brightness and picture grade controls.
- **F10 menu:** a frosted-glass look in nine languages, plus full controller support. The
  PS button or L3 + R3 opens it, L1 / R1 switch tabs, and Cross selects a setting before
  Left / Right change it.
- **Trophies:** in-game pop-ups with each trophy's own icon, sized for 4K.
- **Haptics:** DualSense HD haptics recover by themselves after a crash, a replug or a
  settings change, and the motor channels are kept at full strength.
- **Other additions:** live resolution change, presentation modes, monitor choice, and game
  language.
- **Name change:** the engine is now `heliasdream`. Old `BBHOST_*` environment variables
  still work, and online play is unchanged.

## Known issues
- The Windows build is new and has had little testing on real Windows PCs. Please report
  problems with the log from the `logs` folder.
- FSR 4.1.1 needs assets built once from AMD's DLL (`tools/fsr4cap`). Without them it
  falls back to FSR 3.1.
- Heavy smoke can dip below 120 fps; some textures at Vicar Amelia are wrong.

Built on [droogie/bbhost](https://github.com/droogie/bbhost) v0.2.18 (GPL-3.0).
