heliasdream-vk profiling kit vk1-rc5-prof (measures what the next build should target)
==================================================================================

One guided session of about 28 minutes (30 with the optional drift check). One script
does everything: bbprofile.sh. It tells you in the terminal what to do in the game,
starts the game for each step with that step's settings, and at the end leaves ONE
file to send back: ~/bbprofile-<date>-<time>.tar.gz

Binaries: rc5b (v0.2.18-vk.1-6-g7b65cd0) plus debug instrumentation that is off unless
its variable is set (source commit ea885ba, branch phase-5-test-release, PR #6):
  heliasdream-prof     rc5b + the instrumentation, the same release build flags (every step but pgo)
  heliasdream-pgogen   the same, PGO-instrumented (the pgo step only; the CPU side runs slower)
The script swaps them in as the game folder's heliasdream.bin and puts your original back
at the end, also on Ctrl-C (the original waits as heliasdream.bin.bbprofile-orig meanwhile).
No game files, DLLs or shaders are in the kit; the tarball has no game files either.

Commands (the kit tarball downloaded to ~/Downloads)
----------------------------------------------------
  cd ~/Downloads && tar xzf bbprofile-kit-vk1-rc5-prof.tar.gz && cd vk1-rc5-prof
  sha256sum -c SHA256SUMS
  bash bbprofile.sh --dry-run        checks the kit, the game folder and tools; changes nothing
  bash bbprofile.sh                  the session

Run it in Konsole (bash is needed; your zsh is fine for typing the command). It asks for
your sudo password once at the start. If perf is missing it runs
`sudo pacman -S --needed perf` (answer the pacman prompt). It sets
kernel.perf_event_paranoid=1 and kernel.kptr_restrict=0 for the session and puts your
values back at the end, also on Ctrl-C.

Before you start: load the game once with your installed build and quit while standing
at the Old Yharnam spot (outdoors, the heavy spot of the rc5 snapshot 100844), so every
start of the session loads you right there. Keep the same camera direction each time.

The steps (each asks first: Enter = go, s = skip, q = stop for now)
-------------------------------------------------------------------
Every "spot" step: the script starts the game; you load in at the spot, press F11 once
when you stand there, then don't touch anything until two more "Snapshot saved" banners
have appeared (~65 s; a sound plays when done). Then close the game window (or switch to
the terminal and press Enter, which closes it). The script waits for the game to quit.

  sys  ~1 min   System facts: RAM speed vs rated (EXPO?), power profile, governor/EPP,
                amd_pstate, sched_ext, GPU clocks/power cap/ReBAR, display, packages.
  a1   ~4.5 min Plain at the spot (the baseline), then the CPU profile in the same run:
                keep standing ~95 s more (perf stat, a flat profile, DWARF call graphs of
                bb-cp0 and of the game thread/GXRenderThread/bb-record/bb-submit/bb-present,
                and a short scheduler/GPU-scheduler trace).
                Decides: the per-stage memo and bb-cp0 split items (where ~5 us/draw goes).
  a2   ~2.5 min HELIAS_TABLES_VRAM=0          Decides item 1 (GPU idle with work queued:
  a3   ~2.5 min HELIAS_PARAMS_WC=1             page tables / params ring placement).
  a4   ~2.5 min HELIAS_PARAMS_VRAM=1 (new: params ring in ReBAR VRAM)
  m4   ~3.5 min The periodic ~12.4 ms frame: per-flip trace (HELIAS_FLIP_TRACE=1, logs every
                frame >= 11 ms with its stage split), frame stats, waits by site, HLE counts,
                and a 22 s system-wide `perf sched` trace; then 2 screenshots (stand 5 s more).
                Decides item 2: game thread, CP walk, back-pressure or the scheduler.
  pgo  ~6 min   PGO training with heliasdream-pgogen: 2 min playing in Old Yharnam near the spot
                (walk, fight a little, open the menu), lamp to the Hunter's Dream, 1.5 min
                walking there, lamp back to Old Yharnam, close. The prompts say when.
                Profiles go to the game folder's pgo/<session>/ (also copied to the tarball).
                Decides item 6 (PGO build; merged on the box).
  a5   ~2.5 min HELIAS_PS_WAVE32=32           Decides item 7 (wave32 pixel shaders).
  a6   ~2.5 min Plain again (optional, asked)  Shows whether the A/B differences beat drift.
  then ~3 min   perf reports and the tarball; the original heliasdream.bin and sysctls go back.

The most valuable data comes first, so stopping early still helps. Interrupted or
stopped? Continue later with:
  bash bbprofile.sh --resume
Other options: --list, --only a2,m4, --skip pgo, --no-sudo, --keep-data (the big perf
.data files into the tarball too), --bundle (remake the last tarball), --game DIR
(default: ~/BloodBorne PC/heliasdream-linux-v0.2.18).

What stays on the PC
--------------------
~/bbprofile-work/<session>/  everything, including the full perf .data files (delete when done)
~/bbprofile-<session>.tar.gz the file to send (expect ~20-80 MB)
<game>/pgo/<session>/        the PGO profiles (also in the tarball; delete when done)
~/.debug/                    perf's build-id cache (copies of the profiled binaries; rm -rf ok)

If something goes badly wrong (power loss mid-session), the next run of bbprofile.sh puts
the original heliasdream.bin back by itself. By hand:
  cd ~/"BloodBorne PC/heliasdream-linux-v0.2.18" && mv -f heliasdream.bin.bbprofile-orig heliasdream.bin
  sudo sysctl -w kernel.perf_event_paranoid=$(cut -d' ' -f1 ~/bbprofile-work/.sysctl-orig) kernel.kptr_restrict=$(cut -d' ' -f2 ~/bbprofile-work/.sysctl-orig)
(that file holds the values found at the start, until they are restored; a reboot also
resets both, the session never makes them permanent.)

Box side (not needed on the PC)
-------------------------------
  python3 tools/bbprofile_analyze.py bbprofile-20261021-190000.tar.gz --symbols symbols/heliasdream-prof.debug
  python3 tools/bbprofile_analyze.py pgo-merge bbprofile-20261021-190000 heliasdream.profdata
(the tarball's real name instead of the example; pgo-merge needs llvm-profdata 19.)
symbols/ holds the unstripped binaries (inline view of bb-cp0's hot code); not in the kit tarball.
