#!/bin/bash
# Packs the Windows cross build into the zip a Windows machine runs from
# (docs/building.md), the release's Windows bundle: the engine without its
# DWARF inside the launcher, HeliasDream.exe - the one program in the folder
# (launcher/HeliasDream/Core/Engine.cs writes it out and runs it; dotnet
# publishes it, LAUNCHER_EXE to give one already built), a config template with
# Windows paths, the player's README, and the empty data/, build/ and logs/
# directories the run writes into. Nothing in it picks a server: heliasdream's
# defaults are the live server's (https://thehuntersdream.com). The
# unstripped exe is kept beside the zip under the same revision name, for
# symbolizing a crash report from that machine.
#   tools/win_package.sh [EXE] [OUTDIR]     (cmake --build build-win --target package-win)
set -eu
cd "$(dirname "$0")/.."
exe=${1:-build-win/heliasdream.exe}
out=${2:-build/win}
rev=$(git -c core.excludesFile=/dev/null rev-parse --short HEAD 2>/dev/null || echo unknown)
# A release (HEAD at a v* tag) is named for its version, anything else for its revision.
if tag=$(git -c core.excludesFile=/dev/null describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null); then
    rev=$tag
fi
if [ -n "$(git -c core.excludesFile=/dev/null status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    rev="$rev+"
fi
name=helias-dream-windows-$rev
dir=$out/$name
rm -rf "$dir"
mkdir -p "$dir/data" "$dir/build"
cp -r patches "$dir/patches"
mkdir -p "$dir/plugins"
cp include/heliasdream_plugin.h "$dir/plugins/"
cp LICENSE "$dir/LICENSE.txt"
mkdir -p "$dir/licenses/FSR-Vulkan"
cp THIRD_PARTY_NOTICES.md "$dir/licenses/"
if [ -f third_party/FSR-Vulkan/LICENSE.txt ]; then
    cp third_party/FSR-Vulkan/LICENSE.txt third_party/FSR-Vulkan/NOTICE.md "$dir/licenses/FSR-Vulkan/"
    cp third_party/FSR-Vulkan/upstream/ffx-1.1.4/sdk/LICENSE.txt "$dir/licenses/FSR-Vulkan/FidelityFX-SDK-LICENSE.txt"
fi
tools/package_plugins.sh "$(dirname "$exe")/plugins" "$dir/plugins"
mkdir -p "$out/engine"
llvm-objcopy --strip-debug "$exe" "$out/engine/HeliasDream-engine.exe"
# The launcher, with the engine inside it.
launcher=${LAUNCHER_EXE:-}
if [ -z "$launcher" ]; then
    command -v dotnet > /dev/null || { echo "package: dotnet is needed for the launcher"; exit 1; }
    dotnet publish launcher/HeliasDream/HeliasDream.csproj -c Release -r win-x64 --self-contained true \
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true \
        -p:EngineBinary="$(realpath "$out/engine/HeliasDream-engine.exe")" -o "$out/launcher" > "$out/launcher.log" 2>&1 \
        || { echo "package: dotnet publish failed (log: $out/launcher.log)"; exit 1; }
    launcher=$out/launcher/HeliasDream.exe
fi
cp "$launcher" "$dir/HeliasDream.exe"
cp "$exe" "$out/$name.debug.exe"
sed -e 's#^app0 = .*#app0 = "C:/Games/CUSA00900"#' \
    -e 's#^eboot = .*#eboot = "./eboot-109-decrypted.bin"#' \
    -e 's#^skip_intro = false#skip_intro = true#' heliasdream.example.toml > "$dir/HeliasDream.example.toml"
cat > "$dir/README.txt" <<EOF
Helia's Dream for Windows, build $name
======================================

Helia's Dream runs Bloodborne on PC: the game's own 1.09 executable, its calls into
the PS4's system libraries answered by Helia's Dream and its graphics recompiled for
Vulkan. Free software under the GNU GPL, version 3 or later (LICENSE.txt); the
source is at https://github.com/Heliasyu/helias-dream (based on
https://github.com/droogie/bbhost).

You need
  - Your own copy of Bloodborne, dumped from your own PS4 - nothing from the
    game is in this package:
      - the game folder (CUSA00900) with the 1.09 update's files copied over
        it: the folder that contains dvdroot_ps4, whose sce_sys/param.sfo
        says APP_VER 01.09. Or the update kept beside it in a folder named
        after it with -UPDATE appended (CUSA00900-UPDATE), the way shadPS4
        keeps a game library: Helia's Dream reads the update's files from there;
      - the 1.09 update's eboot.bin, decrypted to an ELF. Its SHA-256 must be
        941f887a562aae054fac35af8cc8f27cf075f3d4cc2e029fb5ae2a663aaa5ae7;
        Helia's Dream stops at start and says why when it is another version (a
        dump without the update has the 1.00 eboot, which will not do).
  - Windows 10 or 11, 64-bit, and a graphics card with a Vulkan 1.3 driver
    (update the driver first).
  - A controller, or keyboard and mouse.

Install
  Unzip this folder somewhere of its own, for example C:\\Games\\HeliasDream, and
  double-click HeliasDream.exe, the launcher. Pick the game folder and the
  decrypted eboot, set the display, upscaling and game options, then Play.

  The settings are kept in %APPDATA%\\HeliasDream\\HeliasDream.toml, which every
  copy of Helia's Dream reads, so a new download or an update needs nothing set
  again. The F10 settings and your account sign-in are kept beside it, saves
  and caches in %LOCALAPPDATA%\\HeliasDream\\data. HeliasDream.example.toml lists
  every setting. An earlier install's settings and saves are copied over the
  first time.

Start the game
  Double-click HeliasDream.exe and press Play. Its Console page shows the
  game's log, also written to logs\\HeliasDream-<date>-<time>.log. The first
  time, Windows may say "Windows protected your PC": click More info, then
  Run anyway. HeliasDream.exe is the only program here: the game itself is
  inside it, kept in %LOCALAPPDATA%\\HeliasDream\\engine while it runs.

  HD haptics: plug a DualSense or DualSense Edge in by USB and set Haptics to
  HD. Windows must show its speaker as a 4-channel device (Sound settings >
  the controller's speaker > Properties > Advanced: 4 channel); Bluetooth
  plays the classic rumble.

  When Windows Defender Firewall asks whether Helia's Dream may communicate on
  networks, tick Private and Public and click Allow access: other players
  reach you on UDP port 9307, and a blocked port is the most common reason
  summons fail. (If you clicked Cancel: Windows Security > Firewall & network
  protection > Allow an app through firewall, and allow HeliasDream-engine.exe,
  in %LOCALAPPDATA%\\HeliasDream\\engine.)

  The first visit to an area stutters for a moment while its graphics are
  prepared for your card; later visits do not.

  F10 opens the PC settings at any time: resolution, window mode, V-Sync,
  frame cap, mouse, key bindings and your account.

Online
  Helia's Dream plays online on the community server, https://thehuntersdream.com,
  with other players on PC (not with PS4s). The game stays offline until this
  PC is linked to an account, and the account's name is your name online.
  Link it once, in the setup window's Account section or with F10 > ACCOUNT
  at the title screen:
    - Log in with Discord (Link with Discord in F10): a code appears and your
      browser opens https://thehuntersdream.com/link. Sign in there with
      Discord, check that the page names this PC, and approve.
    - Create account on this PC: type a name (3 to 16 letters, digits, _ or
      -). A recovery code appears once. Write it down: it is the only way to
      get the account back on another PC or after reinstalling (Recover
      account takes the name and that code).
  Then choose Play Online at the title.

  Co-op: the host rings the Beckoning Bell and the helper the Small Resonant
  Bell, in the same area. Invasions: the Sinister Resonant Bell. Messages,
  bloodstains and phantoms work as on the console. The website has the live
  map of deaths and messages, the leaderboards and your account page (Public
  profile puts your name on them).

  If summons keep failing with one person, forwarding UDP port 9307 to this
  PC on your router often fixes it. Two PCs in one house: set p2p_addr under
  [online] in the per-user HeliasDream.toml to each PC's local address.

Plugins
  plugins/ holds the official ones. In the game, F9 opens the plugin menu
  (start the boss rush there). Turn them on in the per-user HeliasDream.toml:
      [plugins]
      randomizer = true    # every pickup and shop shuffled from a seed
      boss_rush = true     # the game's bosses back to back, timed
      mutators = true      # speed, bullet time, one-hit, chaos and more
      debug_menu = true    # the developers' debug menu: \` opens it
  Each one's settings go in a section of its own name ([randomizer],
  [boss_rush], [mutators]); docs/plugins.md in the repository lists them.
  Use the boss rush on a save of its own. Cheating against other players
  with the debug menu gets an account banned; trying it out online with
  friends is fine.

Reporting a problem
  Send the log of that session from the logs folder (privately: it contains
  your internet address), with what happened, when, your account name and
  your graphics card. If the game crashed, the log ends with a block starting
  "SIGSEGV pc=" with the registers, the guest address and the host
  module+offset; the release's $name.debug.exe symbolizes the host addresses:
      addr2line -f -C -e $name.debug.exe 0x<link address>
  If the log stops without that block, the process was ended from outside
  Helia's Dream (inside the GPU driver, or by Windows): Event Viewer, Windows Logs >
  Application, has an "Application Error" entry with the faulting module and
  exception code - send that with the log.

  A wrong picture (wrong colours, a green or black screen, missing parts):
  press F12 in the game while it shows. Helia's Dream writes that frame, the images
  the game drew it from and its list of draws into a folder of its own in
  logs (logs\\f12-<date>-<time>; the log names it). Zip that folder
  (right-click, Send to > Compressed (zipped) folder) and send it with the
  log.

Switches (environment variables)
  Set one in a Command Prompt opened in this folder, then start from there:
      set HELIAS_F12_PNG=1
      HeliasDream.exe
  HELIAS_GPU_DEVICE=N      the GPU to use, by the log's "gpu: device N"
                           index or part of its name; the default is a
                           discrete GPU over an on-board one
  HELIAS_SKIP_INTRO=1      skip the company logos (HeliasDream.toml has it)
  HELIAS_DUMP_FRAME=N      write the displayed frame N as build/frame-N.ppm
  HELIAS_F12_PNG=1         F12 writes PNG files (any image viewer opens
                           them, a fifth of the size) instead of PPM
  HELIAS_F12_TEXTURES=1    F12 also writes every texture the frame used
                           (the game pauses while it writes them)
  HELIAS_EXIT_FLIP=N       end the run at flip N with the exit reports
  HELIAS_SAMPLE=main       the sampler: build/samples.txt + .maps
  HELIAS_HEADLESS=1        no window
EOF
sed -i 's/\r*$/\r/' "$dir/README.txt"
mkdir -p "$dir/logs"
rm -f "$out/$name.zip"
if command -v zip > /dev/null; then (cd "$out" && zip -qr "$name.zip" "$name")
else (cd "$out" && 7z a -tzip -bd -bso0 "$name.zip" "$name"); fi
echo "package: $out/$name.zip ($(( $(stat -c %s "$out/$name.zip") / 1048576 )) MB); symbols: $out/$name.debug.exe"
