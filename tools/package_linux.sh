#!/bin/bash
# Packs a Linux build into the tarball another Linux machine runs from, the
# counterpart of tools/win_package.sh: the binary without its DWARF, the patch
# manifests it applies (patches/, looked for next to the binary), the plugin
# header, the example config, a README with what the machine must have, a
# launcher that keeps a log file, and the empty data/, logs/ and build/ (frame
# dumps) the run writes into: the release's Linux bundle. Nothing in it picks
# a server: heliasdream's defaults are the live server's (https://thehuntersdream.com).
# The unstripped binary is kept beside the tarball under the same revision
# name, for symbolizing a crash report from that machine.
#   tools/package_linux.sh [BIN] [OUTDIR]
# BIN defaults to build-linux-portable/build/heliasdream (tools/linux_portable_build.sh),
# the build that runs on other distributions; build/heliasdream only runs on a
# machine as new as this one. Never packs game files (eboot, dump, keys).
set -eu
cd "$(dirname "$0")/.."
bin=${1:-build-linux-portable/build/heliasdream}
out=${2:-build/linux}
[ -x "$bin" ] || { echo "no binary at $bin (tools/linux_portable_build.sh builds it)"; exit 1; }
# heliasdream-vk: named for `git describe` (v0.2.18-vk.0-55-gabc1234: the last
# version tag, the commits since, the revision), so a test build says exactly
# what it is; a bare revision when there is no tag to describe from.
rev=$(git -c core.excludesFile=/dev/null describe --tags --match 'v[0-9]*' --always 2>/dev/null ||
      git -c core.excludesFile=/dev/null rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ -n "$(git -c core.excludesFile=/dev/null status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    rev="$rev+"
fi
name=helias-dream-linux-$rev
dir=$out/$name
rm -rf "$dir"
mkdir -p "$dir/data" "$dir/logs" "$dir/plugins" "$dir/build"
cp -r patches "$dir/patches"
cp include/heliasdream_plugin.h "$dir/plugins/"
cp LICENSE "$dir/LICENSE"
# heliasdream-vk: the MIT notices of FSR-Vulkan and the FidelityFX SDK it carries,
# both linked into the binary (THIRD_PARTY_NOTICES.md).
if [ -f third_party/FSR-Vulkan/LICENSE.txt ]; then
    mkdir -p "$dir/licenses/FSR-Vulkan"
    cp THIRD_PARTY_NOTICES.md "$dir/licenses/"
    cp third_party/FSR-Vulkan/LICENSE.txt third_party/FSR-Vulkan/NOTICE.md "$dir/licenses/FSR-Vulkan/"
    cp third_party/FSR-Vulkan/upstream/ffx-1.1.4/sdk/LICENSE.txt "$dir/licenses/FSR-Vulkan/FidelityFX-SDK-LICENSE.txt"
fi
tools/package_plugins.sh "$(dirname "$bin")/plugins" "$dir/plugins"
# heliasdream-vk: the FSR 4.1.1 asset builder (GPL-2.0-or-later sources, from
# bbport) and the testers' documents. Never AMD's DLL or the assets built from
# it: those are each player's own (the check below refuses a package with any).
mkdir -p "$dir/tools" "$dir/docs"
cp -r tools/fsr4cap "$dir/tools/fsr4cap"
rm -rf "$dir/tools/fsr4cap/__pycache__"
# HELIAS_FSR4CAP_TOOLS=<folder with dxil-spirv and fsr4cap.exe>: built
# beforehand, so build_assets.sh needs no MinGW, cmake or network (it reads
# them through BB_FSR4CAP_TOOLS; run-fsr4cap.sh below sets it).
# The folder must carry the licence texts of what those binaries are built
# from (dxil-spirv and the libraries linked into it) in licenses/: they are
# copied to the package's licenses/fsr4cap-tools/. Without them the binaries
# stay out (v0.2.18-vk.1 shipped them with no notices and was repackaged), and
# build_assets.sh builds them on the player's PC instead.
if [ -n "${HELIAS_FSR4CAP_TOOLS:-}" ]; then
    if [ -d "$HELIAS_FSR4CAP_TOOLS/licenses" ] && [ -n "$(ls -A "$HELIAS_FSR4CAP_TOOLS/licenses")" ]; then
        mkdir -p "$dir/tools/fsr4cap/bin" "$dir/licenses/fsr4cap-tools"
        # dxil-spirv with its library (rpath $ORIGIN), fsr4cap.exe, SOURCES.txt.
        find "$HELIAS_FSR4CAP_TOOLS" -maxdepth 1 -type f -exec cp {} "$dir/tools/fsr4cap/bin/" \;
        cp -r "$HELIAS_FSR4CAP_TOOLS/licenses/." "$dir/licenses/fsr4cap-tools/"
    else
        echo "package: $HELIAS_FSR4CAP_TOOLS has no licenses/ folder: the prebuilt fsr4cap tools are left out" >&2
    fi
fi
cat > "$dir/build-fsr4.sh" <<'EOF2'
#!/bin/bash
# Builds FSR 4.1.1 once from your own amd_fidelityfx_upscaler_dx12.dll (4.1.x,
# e.g. from the latest OptiScaler release) into ~/.local/share/heliasdream/fsr4_411.
# Needs Proton (GE-Proton 10+, Proton-CachyOS or Proton Experimental), python3
# and spirv-dis/spirv-as; without tools/fsr4cap/bin also MinGW, cmake, ninja,
# git and network. See README-TESTING.txt.
#   ./build-fsr4.sh /path/to/amd_fidelityfx_upscaler_dx12.dll
cd "$(dirname "$0")" || exit 1
[ -n "${1:-}" ] || { echo "usage: ./build-fsr4.sh /path/to/amd_fidelityfx_upscaler_dx12.dll"; exit 2; }
if [ -x tools/fsr4cap/bin/dxil-spirv ] && [ -f tools/fsr4cap/bin/fsr4cap.exe ]; then
    export BB_FSR4CAP_TOOLS=${BB_FSR4CAP_TOOLS:-$(pwd)/tools/fsr4cap/bin}
fi
export BB_FSR4CAP_WORK=${BB_FSR4CAP_WORK:-${XDG_CACHE_HOME:-$HOME/.cache}/heliasdream/fsr4cap}
exec tools/fsr4cap/build_assets.sh "$@"
EOF2
chmod +x "$dir/build-fsr4.sh"
[ -f docs/vk/TESTING.md ] && cp docs/vk/TESTING.md "$dir/docs/TESTING.md"
[ -f docs/vk/KNOWN_ISSUES.md ] && cp docs/vk/KNOWN_ISSUES.md "$dir/docs/KNOWN_ISSUES.md"
[ -f CHANGELOG.md ] && cp CHANGELOG.md "$dir/docs/CHANGELOG.md"
for f in docs/vk/RELEASE-*.md; do [ -f "$f" ] && cp "$f" "$dir/docs/"; done
# The README's "Testing heliasdream-vk" section, as plain text beside the binary.
if [ -f README.md ]; then
    awk '/^## Testing heliasdream-vk/{on=1} on && /^## / && !/^## Testing heliasdream-vk/{exit} on' README.md > "$dir/README-TESTING.txt"
    [ -s "$dir/README-TESTING.txt" ] || rm -f "$dir/README-TESTING.txt"
fi
objcopy --strip-debug "$bin" "$dir/heliasdream" 2>/dev/null || llvm-objcopy --strip-debug "$bin" "$dir/heliasdream"
chmod +x "$dir/heliasdream"
cp "$bin" "$out/$name.debug"
cp heliasdream.example.toml "$dir/heliasdream.example.toml"
# The launcher (HeliasDream): the settings window that starts the engine,
# built with dotnet when there is one (LAUNCHER_BIN to give one).
launcher=${LAUNCHER_BIN:-}
if [ -z "$launcher" ] && command -v dotnet > /dev/null; then
    dotnet publish launcher/HeliasDream/HeliasDream.csproj -c Release -r linux-x64 --self-contained true \
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o "$out/launcher" > "$out/launcher.log" 2>&1 \
        && launcher=$out/launcher/HeliasDream || echo "launcher: dotnet publish failed (log: $out/launcher.log)"
fi
if [ -n "$launcher" ] && [ -f "$launcher" ]; then install -m 755 "$launcher" "$dir/HeliasDream"; else echo "launcher: none packed (no dotnet)"; fi

# What the binary still loads from the system, from its own dynamic section,
# so the README cannot drift from the build.
needed=$(readelf -d "$bin" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
glibc=$(objdump -T "$bin" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
glibcxx=$(objdump -T "$bin" | grep -o 'GLIBCXX_[0-9.]*' | sort -uV | tail -1)

cat > "$dir/run-heliasdream.sh" <<'EOF'
#!/bin/bash
# Starts heliasdream and keeps its log in logs/ (the newest ten are kept).
#   ./run-heliasdream.sh                       the per-user config (~/.config/HeliasDream/HeliasDream.toml)
#   HELIAS_CONFIG=other.toml ./run-heliasdream.sh   that file on top of it
cd "$(dirname "$0")" || exit 1
cfg=${HELIAS_CONFIG:-heliasdream.toml}
mkdir -p logs
log=logs/heliasdream-$(date +%Y%m%d-%H%M%S).log
ls -1t logs/heliasdream-*.log 2>/dev/null | tail -n +10 | xargs -r rm -f
args=()
[ -f "$cfg" ] && args=(--config "$cfg")
echo "heliasdream: config ${args[1]:-per-user}, log $(pwd)/$log"
# tee -i: Ctrl-C stops the game, not the log, so its last lines are kept.
./heliasdream "${args[@]}" "$@" 2>&1 | tee -i "$log"
exit "${PIPESTATUS[0]}"
EOF
chmod +x "$dir/run-heliasdream.sh"

cat > "$dir/README-linux.txt" <<EOF
Helia's Dream for Linux, build $name
====================================

Helia's Dream runs Bloodborne on PC: the game's own 1.09 executable, its calls into
the PS4's system libraries answered by Helia's Dream and its graphics recompiled for
Vulkan. Free software under the GNU GPL, version 3 or later (LICENSE); the
source is at https://github.com/Heliasyu/helias-dream (based on
https://github.com/droogie/bbhost).

It adds FSR 3.1, FSR 4 and FSR 4.1.1 upscaling, HDR, frame rate caps up to
120, DualSense HD haptics and adaptive triggers, and a launcher. Online play
uses the community server, https://thehuntersdream.com. docs/TESTING.md is a
full test session.

You need
  - x86-64 Linux with glibc ${glibc#GLIBC_} or newer (Ubuntu 24.04, Linux Mint 22, Debian 13,
    Fedora 39 or anything newer; Arch; a current SteamOS). Check with: ldd --version
  - A Vulkan 1.3 GPU driver and the Vulkan loader. Check with: vulkaninfo --summary
  - These system libraries (every desktop has most of them already):
        $needed
    Packages that provide them:
        Ubuntu / Debian / Mint : libvulkan1 mesa-vulkan-drivers libcurl4t64 zlib1g
        Fedora                 : vulkan-loader mesa-vulkan-drivers libcurl zlib
        Arch / SteamOS         : vulkan-icd-loader vulkan-radeon|vulkan-intel|nvidia-utils curl zlib
    NVIDIA: the proprietary driver brings its own Vulkan driver.
  - X11 or Wayland, and PulseAudio, PipeWire or ALSA for sound (SDL3 is built
    in and uses what it finds).
  - The eboot.bin of the game's 1.09 update, decrypted to an ELF (SHA-256
    941f887a562aae054fac35af8cc8f27cf075f3d4cc2e029fb5ae2a663aaa5ae7; heliasdream
    stops at start and says why when it is another version), and the game
    dump with the 1.09 update's files copied over it: the CUSA00900 folder
    that contains dvdroot_ps4, whose sce_sys/param.sfo says APP_VER 01.09
    (or the update kept beside it in CUSA00900-UPDATE, the way shadPS4 keeps
    a game library). Neither is in this package.

Built static: libstdc++ ($glibcxx), SDL3, ffmpeg (movie decoders only),
SPIRV-Tools.

Setup
  Untar this folder somewhere of its own, for example ~/Games/HeliasDream, and
  run ./HeliasDream, the launcher. On its Setup page pick the game folder and
  the decrypted eboot, then press Play.

  The settings are kept in ~/.config/HeliasDream/HeliasDream.toml, which every
  copy of Helia's Dream reads, so a new download or an update needs nothing set
  again. The F10 settings and your account sign-in are kept beside it, saves
  and caches in ~/.local/share/HeliasDream/data. heliasdream.example.toml
  lists every setting.

Run
  ./HeliasDream                 the launcher: press Play (its Console page shows the log)
  ./run-heliasdream.sh          the game alone (the log goes to logs/ and the terminal;
                                --setup opens the engine's own setup window)
  The first visit to an area stutters for a moment while its graphics are
  prepared for your GPU; later visits do not. F10 in the game window opens
  the PC settings: resolution, window mode, V-Sync, frame cap, mouse, key
  bindings and your account.

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
  Bell, in the same area. Invasions: the Sinister Resonant Bell. The website
  has the live map of deaths and messages, the leaderboards and your account
  page (Public profile puts your name on them).

  Other players reach you on UDP port 9307: a firewall on this PC must let it
  in. If summons keep failing with one person, forwarding that port to this
  PC on your router often fixes it. Two PCs in one house: set p2p_addr under
  [online] in ~/.config/HeliasDream/HeliasDream.toml to each PC's local address.

Steam Deck
  The release's heliasdream-steamdeck tarball is this package with the Deck's
  settings (its screen's 1280x800, full screen, 30 fps) and STEAMDECK.md,
  the steps for adding it to Steam and playing in Game Mode.

Plugins
  plugins/ holds the official ones, signed by the heliasdream release key
  (the .sig beside each). Turn them on and set them up in the setup
  window's Plugins tab (heliasdream --setup), which can also fetch the latest
  official plugins; in the game, F9 opens the plugin menu (start the boss
  rush there). Or in the per-user heliasdream.toml:
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
  Send the log of that session from logs/ (privately: it contains your
  internet address), with what happened, when, your account name and your
  GPU. If the game crashed, the log ends with a block starting "SIGSEGV pc="
  with the registers and the frames; the release's $name.debug symbolizes
  it.

  A wrong picture (wrong colours, a green or black screen, missing parts):
  press F12 in the game while it shows. heliasdream writes that frame, the images
  the game drew it from and its list of draws into a folder of its own in
  logs/ (logs/f12-<date>-<time>; the log names it). Pack that folder
  (tar czf f12.tar.gz logs/f12-<date>-<time>) and send it with the log.
  HELIAS_F12_PNG=1 ./run-heliasdream.sh writes PNG files instead of PPM (any
  image viewer opens them, a fifth of the size); HELIAS_F12_TEXTURES=1 adds
  every texture the frame used (the game pauses while it writes them).
EOF

# heliasdream-vk: nothing proprietary goes out - no DLL, no game files, no
# FSR 4.1.1 assets (they are built from AMD's DLL and stay on each machine).
bad=$(find "$dir" \( -iname '*.dll' -o -iname 'eboot*' -o -iname '*.pkg' -o -iname 'initializer.bin' -o -iname 'param.sfo' \
       -o -iname '*.sprx' -o -iname '*.self' -o -path '*fsr4_411*' -o -path '*dvdroot*' -o -iname 'keys*' \) -print)
if [ -n "$bad" ]; then
    echo "package: refusing - these must not be in a package:"; echo "$bad"; exit 1
fi

# Packed from a local copy with plain modes: on a filesystem without them
# (NFS here) every file would go out executable.
stage=$(mktemp -d); trap 'rm -rf "$stage"' EXIT
cp -r "$dir" "$stage/"
find "$stage" -type d -exec chmod 755 {} + && find "$stage" -type f -exec chmod 644 {} +
chmod 755 "$stage/$name/heliasdream" "$stage/$name/run-heliasdream.sh" "$stage/$name/build-fsr4.sh"
find "$stage/$name/tools" -name '*.sh' -exec chmod 755 {} +
if [ -d "$stage/$name/tools/fsr4cap/bin" ]; then chmod 755 "$stage/$name/tools/fsr4cap/bin/dxil-spirv" "$stage/$name/tools/fsr4cap/bin/"*.so*; fi
rm -f "$out/$name.tar.gz"
tar czf "$out/$name.tar.gz" -C "$stage" --owner=0 --group=0 "$name"
( cd "$out" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256" )
echo "package: $out/$name.tar.gz ($(( $(stat -c %s "$out/$name.tar.gz") / 1048576 )) MB, sha256 $(cut -d' ' -f1 "$out/$name.tar.gz.sha256")); symbols: $out/$name.debug"
