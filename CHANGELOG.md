# Changelog (Helia's Dream, formerly bbhost-vk)

## Unreleased

### The name: Helia's Dream
- The project is now **Helia's Dream** (GitHub: `Heliasyu/helias-dream`; the old
  `Heliasyu/bbhost-vk` address redirects there).
- The setup window, the game window's title, the update line and the F11 snapshot header
  carry the new name. The launcher's tooltips lose their "bbhost-vk:" prefix.
- Release packages are `helias-dream-linux-<version>` (and `helias-dream-steamdeck-`). The
  packaging scripts still accept the older names.
- The updater checks `Heliasyu/helias-dream`. Older builds still find the releases there,
  because the GitHub API follows the rename.
- Unchanged: the binary (`bbhost`, installed as `bbhost.bin`), the settings in
  `~/.config/bbhost/`, the version scheme (`v0.2.18-vk.<n>`), and online play.

### The launcher (`launcher/HeliasDream`, Avalonia, Windows and Linux)
- A frosted glass launcher: a moonlit scene with parallax layers, pill tabs, pages that
  glide in; opens on the primary monitor, resizes by its corners, drags by its top.
- **Play:** the ready check (engine, game files and version, eboot, FSR 4.1.1, DualSense,
  sign-in, server), this hunt's settings, quick picks, trophy progress; Play or Enter.
- **Display** (as Lantern Engine's): monitor, resolution, window, V-Sync, menu key; Advanced:
  live resolution change and presentation mode (Mailbox by default); frame rate; the FPS
  counter's look and contents; HDR.
- **Upscaling, Graphics** (effects, picture, camera), **Controller** (haptics, mouse, and a
  keyboard and mouse bindings editor), **Online**, **Trophies** (every trophy with its picture
  from the player's dump and its date), **More** (game options, save backups with restore,
  best settings for this PC, the launcher's own).
- A hover explanation on every setting; the header shows the engine's version and a newer
  release when there is one.

### The engine
- **HDR output:** HDR10 (BT.2020, PQ) where the desktop has HDR on for the display; the SDR
  frame's white at paper white and its highlights expanded towards the peak (Lantern
  Engine's expand mode); the peak from the display's EDID or the setting; HDR metadata.
- **Picture:** brightness, contrast and hue shift at the presenter (the game's picture only).
- **FPS counter:** position, size, colours, opacities, border, and what it shows (frame rate,
  frame time, upscaler, CPU, GPU).
- **Trophies:** a frosted glass pop-up when one is earned; the platinum is awarded; the
  game gets the real names, descriptions and grades.
- **Texture detail:** a LOD bias for the game's textures (Auto: the upscaler's render scale).
- **F10:** frosted glass (the game blurred behind it), tabs, two columns, switches and pills;
  the launcher's fonts. **Menu key:** Insert, Home or F1 as well as F10.
- **Smoke:** the effect manager's and GPU particles' rate stays the data's 30 (Kyo's lists'
  60 halved emitter lifetimes: smoke popped in and out). BBHOST_EFFECT_RATE=60 for the old.
- **Live resolution change** is a setting; **Fullscreen** shows as borderless;
  **monitor** choice (BBHOST_DISPLAY); `--version`.

## vk.2 (2026-10-22, `v0.2.18-vk.2`)

Everything since vk.1: the rc2-rc7 performance work below, batch 1's fixes, and DualSense
haptics. Release notes for players: `docs/vk/RELEASE-v0.2.18-vk.2.md`.

### DualSense / DualSense Edge (`src/host/haptics.cpp`)
- **HD haptics over USB.** The controller is a 4-channel sound card; channels 3 and 4 drive
  its motors. They play the game's rumble (each motor a tone) and its sound effects from the
  main audio port's 7.1 mix - which carries the music too (the BGM port the game opens stays
  silent): the centre, LFE, surrounds and backs through a transient gate (a hit is twice the
  last 800 ms), the front pair - the music's - only at four times, then an expander that fades
  what stays under 0.04. Tuned offline on recordings of the main port (`BBHOST_AUDIO_DUMP`).
  `BBHOST_HAPTICS_TUNE=cutoff=,sustain=,floor=,ratio=,front=,slow=,expand=,carrier=,gain=,rumble=`
  and `BBHOST_HAPTICS_CHANNELS=` for tuning.
- **Menu taps:** a cursor move in any of the game's lists (engine/menu_pointer.cpp's list
  update), a decide, a back; 0.55 / 0.65 / 0.60 of full scale, not scaled by Haptics strength.
- **Adaptive triggers** through SDL's DualSense effect report, by the player's animation
  (`world_player_anim`: SprjChrTimeActModule +0x20, found with `BBHOST_CHR_SCAN`): R2's wall
  at zone 4 with the press registering at 0.55 (`BBHOST_R2_FIRE`), wind-up 032600
  resistance, charge 032610 a growing hum, charged swing 032630 a kick, nothing after an
  attack until R2 is let go; L2's break with the press at 0.65 (`BBHOST_L2_FIRE`), a recoil
  kick on a shot (117051000), slack while no Quicksilver Bullets are carried
  (`world_player_bullets`: goods 900 and 901 in the game data's inventory entries, found with
  `BBHOST_FIND_BULLETS`). `BBHOST_HAPTICS_ANIM_LOG=1` logs each animation.
- The controller's ALSA playback volume is raised to its maximum when HD opens (Linux leaves
  it at -20 dB); HD only while a DualSense is connected by USB, with a message for one on
  Bluetooth and a log line for a sound card in PipeWire's Pro Audio profile.
- F10 > CONTROLLER: Haptics (Off / Rumble / HD), Haptics strength, Adaptive triggers,
  Trigger strength, Menu haptics.

### Fixes and tools (batch 1)
- Linux: `host_desktop_max_size` reads the DRM connectors, so the display buffers are the
  largest display's size and a Native AA start can split the scene live.
- Upscaling quality changes live; AA Auto keeps the game's AA at Native AA.
- Lazy pass barriers: hazards matched by range; a hang (an unclosed GPU-timer pair read with
  WAIT) fixed; off by default again after white flashes at load-in (`BBHOST_PASS_BARRIERS=lazy`).
- No layout change inside a render pass when a target is made mid-pass; `occlusion.spv.h`
  rebuilt as valid SPIR-V.
- `tools/vvl_game.sh` (a game session under synchronization validation),
  `packaging/vk/run-tests.sh` (one command for a round of tests), `BBHOST_GPU_SAMPLE_SLOW=1`.
- In-game help text for PC Upscaling and Vignette.

## vk.1-rc7 (2026-10-20, test build "build 6"/rc7, branch `phase-5-test-release`; not released)

On top of rc6 (bddd030). Same rules: no picture, HDR, chromatic-aberration or menu change,
nothing new in the launcher; each `BBHOST_*` below is a debug revert. The translator is
unchanged: **the shader caches are kept** (no rebuild).

- **The command processor's stage memo (key words).** Each draw resolved every image and
  sampler binding of both shader stages again for the pipeline key (`resolve_key_stage`,
  cp_split `key` ≈ 280-300 ns a draw in rc5). When the last draw of a stage took every
  binding from its GX records through the same binding plan, and this draw's records still
  hold every one of those objects with the same bytes, the words, image shapes and sampler
  modes are the same and are kept; the key's shape list is not rebuilt when both stages kept
  theirs. A binding read through user data or guest memory, a missing object, plans off or in
  their check mode: resolved as before. Exact by construction: the only inputs compared are
  the bytes the resolution would copy. Expected: some of `key` and of `prefetch` (which
  takes these words); roughly 0.1-0.3 ms of bb-cp0 a frame at ~1,400 draws, depending on
  how often consecutive draws repeat a stage's textures and samplers (the exit line says).
  `BBHOST_CP_STAGE_CACHE=0`: resolve every draw (rc6). `=2`: resolve on every hit as well and
  count disagreements (should stay 0). Exit log line `render: stage memo (rc7 …)`.
- Not in this build (see DEVLOG): memoizing `resolve_stage_buffers` and
  `prefetch_stage_images` (their per-draw side effects - texture uploads on CPU writes,
  buffer shadows, copy versions, the temporal observer, scratch copies - must run, and no
  cheap generation proves their guest words unchanged); the flexible/ring import is already
  a dma-buf since rc6; indexed compute constant buffers (needs a translator change, i.e. a
  shader-cache rebuild, and exactness past the records); the bb-bind thread; moving post
  effects before FSR (changes pixels).

## vk.1-rc6 (2026-10-20, test build "build 5"/rc6, branch `phase-5-test-release`; not released)

On top of the profiling-kit commits (ea885ba, b30746b, fbfe181), from the rc5 profile's plan
(the session of 2026-10-20 11:25). Same rules: no picture, HDR, chromatic-aberration or menu
change, nothing new in the launcher; each `BBHOST_*` below is a debug revert. The translator
is unchanged (fingerprint cbb93a8a…): **the shader caches are kept** (no rebuild).

- **The periodic ~12 ms frame: the native-draw report off the command processor.** Every
  100,000 token draws bb-cp0 wrote ~30 log lines and read `/proc/self/maps` (3,000+
  mappings) inline - 74 of rc5's 92 frames of 11 ms or more fell within -5…+25 ms of one. The
  CP now only marks the report due; a reporter thread (`bb-gx-report`) writes the same lines.
  The slow guest reads' regions are named from the process's map only at exit (the report
  runs once more there) or with `BBHOST_GX_REPORT_MAPS=1`. `BBHOST_GX_REPORT_INLINE=1`: on the
  CP again (still without the map read unless `BBHOST_GX_REPORT_MAPS=1`). The shadow mode's
  report (`BBHOST_GX_NATIVE=1`) goes the same way.
- **Asynchronous logging** (`src/core/log_async.cpp`). A log line no longer waits for the log
  file: lines go into a 4 MiB ring (one CAS to reserve, a copy, no lock, no sleep) and a
  writer thread (`bb-log`) writes them in order, whole lines, at most 4096 bytes a write (so
  a pipe - run-bbhost.sh's `tee` - never gets another writer's bytes inside a line). rc5's m4
  caught GXRenderThread 3.1-4.7 ms in a synchronous write to game.log on btrfs. Same bytes,
  same order; the F11 30-second log tap still sees every line as it is logged. Flushed and
  then synchronous on every exit path (window closed, `BBHOST_EXIT_*`, SIGTERM, a crash in the
  fault handler, `abort()` via a new SIGABRT handler that then dumps core as before, `exit()`,
  the stream selftest). Only a ring full of a log storm makes a logging thread write (it
  drains the ring itself, keeping the order). `BBHOST_LOG_SYNC=1`: the old synchronous writes.
  Snapshot [cpu] `log_writer:`; exit line `log: N lines through the writer thread`.
- **Cheaper submissions.**
  - The GX table ring (bbhost's own 128 MiB GPU mapping) is a sealed memfd imported as
    dma-bufs where direct memory already goes in that way (AMD on Linux), instead of a
    host-pointer import: amdgpu re-checked that userptr page by page at every submission
    (`hmm_range_fault`, 22.6% of bb-submit in rc5). Only after a probe import of a memfd of
    its own succeeds, and only with sparse binding (or pieces of 128 MiB+); otherwise as
    before. `BBHOST_HOST_MAP_DMABUF=0`: anonymous memory and a host-pointer import. Log
    `gpu: the host's own GPU mappings …`, `gpu: host mapping 0x… imported as …`.
  - dma-buf pieces as large as the udmabuf module allows (up to 1 GiB, the chunk size), so
    a 1 GiB direct-memory chunk is one buffer object per submission instead of 16. The module's
    default is 64 MiB (unchanged pieces); `echo 1024 | sudo tee
    /sys/module/udmabuf/parameters/size_limit_mb` before starting (docs/vk/TESTING.md). Log
    `gpu: dma-buf pieces of N MiB`. `BBHOST_DMABUF_PIECE_MB=64`: as before.
  - `BBHOST_SUBMIT_EVERY_JOB=0` already exists and stays rejected (test4: flashing effects);
    no new A/B for it.
- **Recorder wait timed with the TSC**: the four clock reads around every block are RDTSC
  where the TSC is invariant and the kernel's clock source (so synchronized across cores),
  converted with the TSC's rate measured against CLOCK_MONOTONIC. `BBHOST_RECORDER_TSC=0`:
  clock_gettime. [cpu] `recorder_wait: … clock=tsc`.
- **Camera table search**: `host_memmem` compares 32 (AVX2) or 16 (SSE2) positions at once
  (first and last byte, then the rest) - the same first match (new test `memmem_test`), ~2-4x
  faster over the 8 GiB scan here.
- **PGO + ThinLTO release binary** from the rc5 session's merged profile; the package also
  has `bbhost.bin.nopgo`, the same commit without it, for an A/B. New CMake options
  `-DBBHOST_PGO=OFF` (ignore `BBHOST_PGO_USE`) and `-DBBHOST_PGO_WARN=ON` (report profile
  mismatches); `packaging/vk/build.sh` takes `PGO_PROFILE=` (and builds the nopgo twin) and
  `tools/ci_build.sh` `CI_BUILD_DIR=`. The profile (gathered on the ea885ba-era kit binary)
  still matches 6,686 of its 6,696 functions; the 10 that changed here are built without it
  (4 of them among the profile's 300 hottest: host_memmem, hle_gx_native_token,
  rebuild_page_tables, recorder_thread). The logging change keeps the inlined log code's
  shape on purpose so the functions that log keep their profiles.
- **Not in this build: the per-stage exact-key cache** (resolve_stage_buffers,
  prefetch_stage_images, resolve_key_stage). Their inputs include guest memory read through
  user-data pointers (V#/T# tables) that nothing tracks, and the texture-cache and
  buffer-shadow state they update; a hit cannot be proven exact without write tracking of
  those tables, and nothing on the build box runs the draw path to prove byte-identical
  output. Left for build 6 with a draw-path replay harness (docs/vk/DEVLOG.md).
- Profiling kit: game.log on tmpfs while the game runs (`$XDG_RUNTIME_DIR`, moved into the
  step folder at the exit; `BBPROFILE_LOGDIR`), a GPU-queue trace (gpu_scheduler and
  amdgpu CS tracepoints, 8 s) in every step's second window, and the analysis warns when a
  step's draws per frame differ from a1's by more than 5% (not the same view). a6 kept.
- New tests `log_async_test` (8 threads, 160,000 lines of 1-4 ring slots through a pipe read
  late: every line once, whole, in each thread's order) and `memmem_test`.

## Profiling kit instrumentation (2026-10-20, branch `phase-5-test-release`; debug only, not released)

On top of rc5b (7b65cd0), for the measured profiling session before the next build
(`bbprofile.sh`, docs/vk/TESTING.md "Profiling kit"). **Nothing changes unless a variable is
set**: no default, picture or snapshot format changes; a normal build is the same build.

- `BBHOST_FLIP_TRACE=1` (`BBHOST_FLIP_TRACE_MS`, default 11): per flip, the game thread's
  submit and back-pressure, the command processor's pickup, walk and hand-off to video, the
  present, the CP's jobs/busy/idle since the flip before, the main loop's last frame, and the
  hot threads' run / run-queue time between presents (`/proc/self/task/*/schedstat`). A present
  interval over the threshold logs `fliptrace: slow frame …` (three lines); snapshots gain a
  `[flip_trace]` CSV section (only with the variable set). For the periodic ~12 ms frame (M4).
- `BBHOST_FRAME_SLOW_MS` (default 33, as before): the main-loop frame length from which
  `BBHOST_FRAME_STATS=1` logs that frame's waits by call site (`main loop: a N ms frame`).
- `BBHOST_PARAMS_VRAM=1`: an A/B that puts the params ring in ReBAR video memory (the page
  tables' rule); the CPU only writes it. Without such a heap, the default.
- Build options `-DBBHOST_PGO_GENERATE=ON` (an instrumented binary that writes
  `$BBHOST_PGO_DIR/bbhost-<pid>.profraw`, default `./pgo`, every 30 s and on exit, since bbhost
  leaves through `_exit`) and `-DBBHOST_PGO_USE=<merged .profdata>`; both off by default, the
  decomp sources stay out of both.
- New test `flip_trace_test`.

## vk.1-rc5b (2026-10-20, test build "build 5", branch `phase-5-test-release`; not released)

On top of rc5 ("build 4", df31b8b). Same rules: permanent, chosen automatically, no menu or
launcher switch; each `BBHOST_*=0` is rc5's behaviour. The translator is unchanged
(fingerprint cbb93a8a…), but compute shaders are translated with different options, so
**each compute shader is translated and compiled once more** on the first start (the
precompile workers do the known ones before the title; graphics stages keep their cache).

- **Compute constant buffers through storage-buffer bindings** (GPU audit #1). The game's
  compute shaders read every constant through the page-table walk (two dependent 64-bit
  loads per constant); they now read them as the graphics stages do, from a binding over
  the same memory, with the walk kept as each buffer's fallback. A buffer is bound only
  when the binding provably reads the walk's bytes: a V# or a pointer in user data or a
  table, read at constant offsets (not by index), whose memory is imported as one buffer,
  holds no copy-back, is on a vec4 of its binding, is not stored to by the dispatch, and is
  not written by the GPU earlier in the same command buffer (nor are the tables its V# was
  read from). Anything else takes the walk, as before. The game's compute pipelines get a
  layout of their own (the shared one plus bindings 112-127); the tessellation pass and the
  graphics pipelines keep theirs. Log `gpu: compute cb:` (start, first pipelines, every
  20,000 dispatches); [walks] `compute_cb:`. `BBHOST_CS_CB=0`.
- **[cpu] `cp_split:` / `cp_split_total:`**: the command processor's draw path in 20
  sections (registers, targets, programs, key, lookup, prefetch, stage_user, buffers, sets,
  images, update, variant, pass, bind_cmds, dynamic, geometry, record, …) in ns per draw,
  from the TSC on the one draw in 16 the phase timers already sample (~10-20 ns a draw
  on average). Log `cp split:` every 100,000 sampled draws. `BBHOST_CP_SPLIT=0`.
- The motion-blur log's name search in the per-stage image binding stops once its 8 lines
  are written (it ran for every stage of every draw).
- New test `gcn_cs_cb_test` (compute translation with constant-buffer bindings: set 0,
  binding 112, constant offsets, valid SPIR-V; nothing bound when off).

## vk.1-rc5 (2026-10-20, test build "build 4", branch `phase-5-test-release`; not released)

CPU headroom for heavy Old Yharnam (bb-cp0 ~91% of a core, bb-record ~87%, in rc4's
`010838`). Plan with the evidence and the ranked next steps: docs/vk/DEVLOG.md (rc5). Rule
as before: permanent, chosen automatically, no menu or launcher switch; each `BBHOST_*=0`
is rc4's behaviour. The translator is unchanged (fingerprint cbb93a8a…): no shader cache
rebuild.

- **Recorder wait with MONITORX/MWAITX.** bb-record spun 4,000 PAUSEs before every sleep
  (`recorder spin 1656 ms, replay 611 ms` a window: 72% of the thread). On AMD (CPUID
  8000_0001h ECX[29]) it now arms a monitor on the published-block counter and waits in
  C0 for the same length (TSC ticks, calibrated at start), then sleeps on the futex as
  before. Elsewhere the PAUSE loop is unchanged. Log `recorder: wait for the next block`;
  the 300-flip line `rc5 wait mwaitx: mwaitx N (M ended by a block)`; [cpu]
  `recorder_wait:`. `BBHOST_RECORDER_MWAITX=0`.
- **CCD-aware placement.** With two or more L3 domains of six or more CPUs (the 5950X's
  CCDs): the main thread, GXRenderThread, bb-cp0, bb-record, bb-submit, bb-present and
  bb-vblank share one whole CCD (larger L3, then prefcore ranking / CPPC / max frequency);
  bb-compile (in game, not while booting or loading), bb-cs-optimize, bb-tablemask and
  bb-lean use the other; every other thread keeps the whole mask. Log `placement:`;
  [cpu] `placement:`. `BBHOST_AFFINITY=0`.
- **Timer slack 1 ns** on the timing threads (Linux's default 50 us made every sleep wake
  ~55 us late). With it set, the limiter's learnt margin floor goes 300 → 150 us and the
  presenter's spin 300 → 150 us. Log `timer slack:`; [cpu] `timer_slack:`.
  `BBHOST_TIMER_SLACK=0` (also restores both floors).
- **ThinLTO** for the bbhost executable (clang + lld). The decomp sources are built
  without it (byte-identical objects). Build option `-DBBHOST_THINLTO=OFF`.
- **Page tables in ReBAR video memory** when a host-visible video-memory heap over 1 GiB
  exists (else system memory, as before). The same bytes; the walks' loads stop crossing
  PCIe. Log `page tables:`; [walks] `page_tables:`. `BBHOST_TABLES_VRAM=0`.
- The lazily started TableMask and LeanBuilder workers are named `bb-tablemask` and
  `bb-lean` (they inherited `bb-cp0`, the menu's "bb-cp0=2885 ms/s").
- The motion-blur diagnostic stops searching every draw's pipeline name once it has
  logged (CPU audit C3).
- **[cpu]** (new F11 section): `placement:`, `timer_slack:`, `recorder_wait:` and
  `cp_cost:` (the command processor's exec/draw/bind/prefetch ns per draw, since start
  and since the previous snapshot).
- `tools/cpuprofile.sh` and `tools/perf_cpu_summary.py` (from the CPU audit): a perf
  profile of the running game, shipped beside the build (TESTING.md).

## vk.1-rc4 (2026-10-20, test build "build 3", branch `phase-5-test-release`; not released)

Rule as in rc3: optimizations are permanent (no menu or launcher switch); `BBHOST_*`
variables only for debugging, each `=0` falling back to rc3's behaviour.

- **GPU idle with work queued (item 1):**
  - **Query resets before the first timestamp.** On RADV a timestamp leaves the command
    buffer's query flush pending (partial flushes, L2 write-back and invalidate, CB/DB
    flush) and the next `vkCmdResetQueryPool` emits it. The occlusion, coverage and
    profiler resets came after the busy meter's TOP timestamp, so every command buffer
    started with a drain and an L2 flush. They now come first. Log `gpu: query resets
    before the first timestamp`; [gpu_idle] `reset_first_cmdbufs`;
    `BBHOST_RESET_ORDER=0` restores the old order.
  - **The blit rides the flip's last game submission.** When that submission is still
    waiting for bb-submit, the blit's command buffer and its `rendered` semaphore are
    appended to it (one kernel job), and an empty `vkQueueSubmit` gives the presenter
    its fence. Nothing is held back or merged: the blit could not have started earlier.
    This is not submit batching (t9). [present] `blit_ride:` (`rode`, `alone`, `missed`);
    `BBHOST_BLIT_RIDE=0` never rides.
  - **Attribution:** the queued gaps are split by what ended the command buffer before
    them and by how long that command buffer ran. Log `gpu idle queued after:`;
    [gpu_idle] `idle_by_span_before:`; `gpu_gaps.py --idle` prints both.
- **Fetch shader inlined (item 2).** A vertex shader and its fetch shader that are both
  branch-free are translated as one straight line, the fetch shader in place of
  `s_swappc_b64`. Their bound loads are no longer excluded with "registers redefined,
  dispatcher order", so the post chain's VS page-table walks go. Log `fetch inline:`;
  [walks] `fetch_inline:` (`fetch_inlined`, `fetch_inlined_walking`);
  `BBHOST_FETCH_INLINE=0` uses the dispatcher.
- **Bound loads at an SGPR offset (item 3).** An indexed buffer load with a register
  `soffset` adds it at run time. It falls back to the walk when the offset is not whole
  dwords or the element lies past the binding. Stores always walk: a binding can be the
  draw's copy or a device-local mirror, and a store there would never reach the game's
  memory. 16e3fa9f's 28 stores therefore stay as they are. Log `soffset bound:`;
  [walks] `soffset_bound`, `soffset_bound_sites`; `BBHOST_SOFFSET_BOUND=0`.
- **Main scene pass diagnostics (item 6), no behaviour change:**
  - Pipeline statistics in the GPU sampler's frames (one every 10 s or more): vertex and
    fragment invocations and primitives for each timed pass.
  - Log `gpu sample stats:`; new snapshot section [pass_stats]
    (`vs_per_us`, `fs_per_us`, `fs_per_prim`); `BBHOST_GPU_SAMPLE_STATS=0` turns it off.
  - `BBHOST_RGP_LABELS=1` inserts a VK_EXT_debug_utils label at each render pass, named
    by its first pipeline, for RGP.
  - Also fixed: the profiler's statistics pool is reset with the timestamp pool, and a
    pair nested in another no longer ends the outer pair's query.
- **Robustness (item 7):**
  - The presenter's priority wait is capped at 100 ms, down from 2 s
    (`BBHOST_PRESENT_PRIORITY_WAIT_MS`). Past the cap the blit takes the stream path,
    behind its frame. Waits over 50 ms are logged as `present: the blit waited`. [present]
    `blit_ride:` shows `cap_fallbacks`, `waits_over_50ms` and `wait_max_ms`.
  - bbtest no longer counts "render target re-created (contents lost)" lines and their
    summary as errors. They get their own line in session.txt.
  - dfmt 6 (10_11_11) was already mapped to B10G11R11 for textures, render targets and
    vertex fetch. The "has no texture: unsupported format" message for that T# now says
    "no address (an unbound slot)", which is what it was.
- The translator changed, so on the first start the translation and pipeline caches are
  rebuilt (the translator fingerprint).
- **Not in rc4:**
  - CA and bloom pass 1 at render size (item 4). CA's offsets are in pixels, and before
    FSR its fringe would be upscaled and accumulated, which changes its look. Bloom pass
    1 must see FSR's 4K picture or it shimmers.
  - The glare composite 8747a367 at render size (item 5) would change glare sharpness.
    Scissoring each sprite to its quad leaves the picture identical but saves nothing:
    the rasterizer shades only the quad already.
  - Neither could be shown identical on the box, which has no game files or captures to
    compare.

## vk.1-rc3 (2026-10-19, test build, branch `phase-5-test-release`; not released)

- **Frame-latency limit on the uncapped/120 path.** rc2's limit only covered flips
  queued by the capped path. A flip the command processor recorded itself (`from_cp`) now
  waits for the presenter too, instead of being replaced.
- **Present pacer:**
  - It is fed the time just before `vkQueuePresentKHR`, not after it.
  - It judges each 30-frame window on the 3rd-earliest arrival, not the earliest, so one
    outlier no longer sets the creep.
  - A window with a few late frames keeps creeping; only one with more than a quarter late
    drops the creep.
  - When the last present was on time and a frame is more than 1 ms early, the grid is
    pulled in by up to 0.5 ms, so the added latency comes back down sooner.
- **The presenter's blit no longer queues behind bb-submit.** When nothing new reached the
  stream while the blit was recorded, the presenter waits only for the game work its frame
  needs and submits directly on the queue. Otherwise it goes through the stream as before.
  This is not a separate queue: sharing the renderer's resources across queues would need
  semaphores on every game submission. `BBHOST_PRESENT_PRIORITY=0` always uses the stream
  (debugging only).
- **Occlusion resolve:**
  - It runs on 64 lanes with shared memory. Only the ordered scan stays on one lane.
    Blocks that dump to the same or overlapping addresses fall back to the serial order.
  - The diagnostic pair statistics are off on the GPU path. `BBHOST_OCCLUSION_STATS=1`
    brings them back (debugging only).
  - The barrier after the resolve covers only transfer, compute, host and conditional
    rendering, not all commands.
  - It is timed on the GPU: the `occlusion:` log lines and the snapshots' [occlusion].
- **Fewer page-table walks in vertex shaders.** MUBUF `buffer_load_format_*` with idxen
  (the post chain's YEBIS windows among them) reads through the bound, length-clamped
  buffer. The walk stays as the fallback. The snapshots' [walks] counts the pipelines,
  and the post pipelines, whose VS still walks, with their names.
- **Stage manifest:** stages a graphics pipeline had to build on the command processor
  (the host-tessellated 52d756df+2e86d610 hitch) are marked urgent and precompiled first
  next start (`<manifest>.urgent`). No manifest ships: it holds the game's shader code.
- **Small fixes:** the `~fallback` pipeline label only appears when the shader has
  constant buffers; the texture log says unsupported or unusable, with the reason. dfmt 6
  (B10G11R11) was already mapped. rc2's line came from a T# with no address.
- **Snapshots gain:**
  - `flip_to_present_split:` (record, submit, pacer wait, `vkQueuePresentKHR`);
  - [frames] columns for the same, plus whether the blit took the priority path;
  - [present]: pacer state, priority/stream blit counts, latency-limit waits;
  - [occlusion]: resolve GPU time;
  - [walks];
  - [gpu_idle]: GPU idle with work queued, total, after an occlusion resolve and after a
    present. The `gpu idle:` log lines add `queued after an occlusion resolve`, and
    `gpu_gaps.py --idle` prints it.
- **Not in rc3:**
  - Bloom's first pass at render size. It reads CA's output, so moving it would change
    CA's contribution.
  - FSR on async compute.
  - The ~200 µs idle after the occlusion submission: not fixed, diagnostics only (above).

## vk.1-rc2 (2026-10-19, test build, branch `phase-5-test-release`; not released)

- **Post-processing at render size covers more passes.** Besides the vk.1 list, a pass now
  joins by its shape: one RGBA16F target, no depth or MRT, reading the chain's stand-ins
  and a smaller RGBA16F input. Four programs seen in the Hunter's Dream (88414cd6, 597cbbcc,
  9e1cb278, c3eff688) are added by hash. Bloom's first pass and CA stay at output size. The
  per-frame fallback stays.
- **Frame-latency limit.** The command processor runs at most one frame ahead of the
  presenter and waits for it instead of having its frame replaced: vk1-dream's ~500 drops.
  `hle_video` no longer discards the oldest flip when its queue of 4 is full; it waits up
  to 34 ms. `BBHOST_FRAME_LATENCY=0` brings the vk.1 behaviour back (debugging only).
- **The present pacing settles faster.** It creeps up to 100 µs a frame and checks every 30
  frames (was 20 µs and 120), so the ~3 ms of latency t13 measured comes back down.
- **Sharpening in the present pass** (`BBHOST_SHARPEN_AT_PRESENT=1`): off by default.
  The present pass sees the HUD, so it would put a halo on the HUD. The default stays
  FSR's sharpening of the scene before the HUD.
- **F10 menu:**
  - The overlay buffer is 1 MiB. On overflow it draws what fits and logs once; before,
    it drew nothing.
  - Notes wrap to 3 lines.
  - Selection changes are logged with the vertex count.
  - Fewer sections, by subject: DISPLAY, GRAPHICS & EFFECTS, CAMERA & CONTROLS, GAME,
    ONLINE & ACCOUNT, DEBUG.
  - Every setting that only takes effect after a restart says "(restart required)": frame
    rate, upscaler and upscaling quality.
- **Removed switches:** "Even frame pacing" and "Post-processing at render size" are gone
  from F10 and the launcher, along with any optimization switch. Optimizations are always
  on. Saved keys are ignored. `BBHOST_PRESENT_PACE=0` and `BBHOST_POST_AT_RENDER=0` remain
  for debugging. The launcher has the same sections.
- **Performance snapshots: F11, or `bbtest snap "note"`.** These save the last 30 s from
  memory into a text file, `snaps/snap-<date>-<time>-<map>.txt`, on a background thread,
  with a brief on-screen confirmation. The file has:
  - every frame's time, present delay, drops and the game's waits;
  - GPU busy every second;
  - per-pass GPU times from the GPU sampler;
  - FSR and sharpening ms;
  - post-at-render coverage and fallback reasons;
  - every shader translation or pipeline compile, with ms each, which thread, and whether
    the frame waited;
  - the area or map id, the settings and the busiest threads' CPU ms;
  - the log.

  `bbtest snap --show` summarizes snapshots: avg, 1%/0.1% lows, over 8.5/9/12 ms, drops,
  the top 10 passes and the area. `BBHOST_SNAP=0` turns snapshots off and
  `BBHOST_SNAP_DIR` sets the folder.
- **GPU sampler:** one frame every ~10 s is timed pass by pass, skipped when the frame
  before it was over budget, and logged as `gpu sample:`. `BBHOST_GPU_SAMPLE=0` turns it
  off.
- **Startup log:**
  - whether the shader caches were kept or cleared for a new translator;
  - how many pipelines the precompile built before the first in-game frame.
- **`bbtest profile ["note"]`:** a session with RADV's RGP tracing armed
  (`MESA_VK_TRACE=rgp`, a per-session `MESA_VK_TRACE_TRIGGER`).
  - Each snapshot also captures the next frame as `rgp/<snapshot>.rgp`, at most one every
    10 s, listed in `rgp/captures.tsv`.
  - Every pipeline's shader statistics go to `shader-stats.tsv`: VGPRs, SGPRs, spills,
    LDS, code size, occupancy, with the pass label. They come from
    VK_KHR_pipeline_executable_properties and the capture-statistics flag, built without
    bbhost's pipeline cache.
  - Profile mode only. A normal launch is unchanged.

## v0.2.18-vk.1 (2026-10-18): the first release since vk.0

Release notes: docs/vk/RELEASE-v0.2.18-vk.1.md.

- **New switches** in the options and launcher: **Even frame pacing** (`present_pace`) and
  **Post-processing at render size** (`post_at_render`). Both are on; the environment
  variables still win.
- **No pacing while a loading screen shows, and for 2 s after.** That is what made t13's
  first half rough.
- **Graphics defaults stay the game's own** for a new or missing options file: all effects
  on and AA Auto. An existing options file is never rewritten at start.
- **`BBHOST_SUBMIT_BATCH`** stays off. It was rejected on hardware (t9).
- **Hardware results** (t13, 4K/120, FSR 4.1.1 Performance):

  | | pacing on | pacing off |
  |---|---|---|
  | 1% low | 119.4-120.0 | 88-100 |
  | Frames over 8.5 ms per 30 s | 15-34 | ~930-1350 |

  The pacing's average wait was ~2.7-4.3 ms.

## Test builds before vk.1 (branch `phase-5-test-release`)
- **test11 (even present pacing; the motion blur skip with post at render):**
  - **Present pacing (`BBHOST_PRESENT_PACE`, on; `=0` off).** At the game's fixed pace
    (30/60/90/120), every present goes out on an even grid of that period.
    - A frame that is ready early waits for its slot. This is a sleep then a spin on the
      presenter thread, with no lock held.
    - A late frame goes out at once and the grid restarts from it.
    - When the GPU can't keep the pace, nothing waits.
    - The grid moves earlier on its own until the earliest frames are ~0.15 ms ahead. The
      added latency is a few hundred µs.
  - **Motion blur skip:** now also fires with post at render on. Its inputs are judged
    against the real targets' size. Before, the 1080p velocity buffer matched the
    stand-in's size, so t12 skipped 0 pairs in the on halves.
  - **A/B:** `BBHOST_AB_SECONDS` now flips the present pacing. `BBHOST_AB_TOGGLE=post` flips
    post at render, as test10 did. Each `ab: present pace` half logs:
    - its 1% and 0.1% lows;
    - the frames over 8.33, 8.42, 8.5, 9 and 12 ms;
    - how many presents waited, were late or restarted the grid.
  - bbtest counts frames over 8.5 and 9 ms and reads the new A/B lines.
- **test10 (post-processing before the tonemap at render size; FSR moved to the chain's end):**
  - With FSR 4.1.1 or 3.1 upscaling, the game's passes between the scale pass and its bloom
    and tonemap now run at the render size. These are:
    - the colour/fog pass (fbe7c4b6);
    - the DoF downsample and blur (265a6d2d, e80526cc);
    - the DoF composite (111fce32) and the pass on its result (bf368417);
    - the motion blur's velocity (d3c8bb21) and the motion blur (e0305cef), or its one copy
      while off.
  - FSR runs where the next pass begins. That is chromatic aberration when it is on,
    otherwise the bloom's downsample, then the tonemap. CA, bloom, tonemap, grain,
    sharpening and the HUD/blur chain stay at 4K.
  - Each moved pass draws into a render-size stand-in for its 4K target, with its viewport
    and scissor scaled. The passes that read those targets read the stand-ins.
  - **Per-frame fallback:** a frame whose scale pass has no upscaler inputs, or another
    output, runs as test9. A dispatch, copy or clear on a stand-in's target closes the
    window early.
  - **Session fallback:** an upscaler failure at the chain's end turns the change off for the
    session. `post-at-render:` log lines say what happened.
  - `BBHOST_POST_AT_RENDER=0` brings back test9. `BBHOST_AB_SECONDS=<s>` now turns this off
    and on (the motion blur skip stays on).
  - The `ab:` lines and bbtest also count frames over 8.33 ms.
- **test9 (fix: the motion blur skip now fires):**
  - test8 never skipped (t10: 0 pairs). The game's two draws are not a ping-pong: draw 1
    copies the DoF output S (0x165e30000) into B (0x169e00000), and draw 2 copies B into the
    chain's colour D (0x161e60000). Also, draw 1's own bind of B counted as "something else
    wrote B". With motion blur off, both draws now become one copy of S into D, after the
    same 120-frame proof. A `motion blur skip learning ...` line says why, whenever it is not
    skipping.
  - Moving post-processing to render size (`BBHOST_POST_AT_RENDER`) is **not** in this
    build; see DEVLOG.
- **test8 (motion blur skip; one-run A/B; post-chain data):**
  - With motion blur off, its two 4K draws a frame (~0.29-0.33 ms) are left out. The skip
    only starts once it has seen, for 120 frames in a row, that the two draws copy A into B
    and back, without blending, and that nothing else reads or writes B. It goes back to
    drawing at the first sign of anything else: draw 1 is done after all (a copy of A into
    B), and it learns again. With motion blur on it skips nothing. It is on by default;
    `BBHOST_MOTION_BLUR_SKIP=0` turns it off.
  - `BBHOST_AB_SECONDS=<s>` turns the skip off and on every s seconds in one run, and logs
    each half's frame times (`ab:` lines). bbtest's session.txt averages them per side.
  - session.txt has a `120 LOCK: N of M seconds under 120` line of its own.
  - `BBHOST_POST_CHAIN_LOG=<flips>` (default 3600; 0 turns it off) logs one frame's draws
    from the scale pass to the first UI draw: targets, sizes, formats, viewports, blending
    and the images each draw samples. Moving post-processing to render size is built on it.
  - The motion blur's texture lines now print; test7c's were lost to a shared limit.
  - test7c's `BBHOST_SUBMIT_BATCH=1` was rejected on hardware (t9: 1% low 99.6 -> 77.1, 521
    frames over 12 ms). It stays off.
- **test7c (submit batching, experimental; motion blur data):**
  - `BBHOST_SUBMIT_BATCH=1` (off by default): bb-submit puts the game's jobs that are already
    waiting into one vkQueueSubmit (one VkSubmitInfo, all their command buffers). It holds
    a new job only while the GPU still has two of our submissions to finish, so the GPU
    never runs dry because of it, and never for more than 2 ms. Labels and CPU waits are not
    held back: every job is still ended and handed over at once (unlike
    `BBHOST_SUBMIT_EVERY_JOB=0`). The last job's fence covers the merged jobs' slots. The
    `gpu idle:` line adds `batched N jobs/s into M submits/s, held ...`; the stream
    self-test reports what it merged.
  - `fsr4_sharpen_test` also checks that the mean brightness of the sharpened output stays
    within 1% of the input (HDR lamps at 30 included) at sharpness 0 and 0.5. The test7
    shader fails it by far.
  - The log names the motion blur's (e0305cef) draw targets and textures, so the next build
    can skip its two draws while motion blur is off, once that is shown to give the same
    picture.
- **test7b (fix: white haze with FSR 4.1.1 sharpening):**
  - test7's sharpening pass treated the replay's postpass output as colour. That output is
    log-encoded and exposed for RCAS: v = log2(1 + 150 c * exposure / pre-exposure) /
    12.2887, as the replay's own rcas pass decodes it. The encoded values went out as
    colour: a heavy white haze everywhere (t8, 12:44).
  - The pass now runs RCAS in that domain and decodes as the replay's pass does: the
    exposure texture, pre-exposure, and alpha equal to red. `fsr4_sharpen_test` now checks
    the decoded output, so the test7 shader fails it.
- **test7 (FSR 4.1.1 Performance at 120: sharpening, idle gaps, AA, frame pacing numbers):**
  - FSR 4.1.1's sharpening is one pass of our own (`shaders/fsr4_sharpen.comp`) instead of the
    replay's RCAS pass (0.315-0.35 ms at 4K at any strength). It uses AMD's RCAS math
    (fsr1/ffx_fsr1.h, MIT) with noise removal and the same sharpness mapping, reads each texel
    once per 16x16 block from shared memory, and works on `c/(1+max(c))`, so the HDR
    highlights (the lamps) are limited instead of ringing. It is timed as `sharpen` in the
    pass timer, inside the FSR 4.1.1 figure. `BBHOST_FSR4_SHARPEN=amd` brings back the
    replay's pass. `fsr4_sharpen_test` checks it against RCAS in double.
  - Anti-aliasing has a new **Auto** value, now the default. It turns the game's AA pass
    (~0.35 ms at 4K) off while FSR 4.1.1 upscales and keeps it on otherwise, including
    when FSR 4.1.1 cannot run and FSR 3.1 stands in. A saved On or Off still wins.
    `BBHOST_ANTI_ALIAS=auto|0|1`. It is in the launcher (Anti-aliasing combo) and the F10
    screen. The in-game row shows what is in effect. The log says when the game's AA
    changes.
  - A `gpu idle:` line under `gpu busy:` every 300 flips sorts the GPU's idle time between
    command buffers:
    - late: the GPU had run dry when the submission came;
    - queued: the time between command buffers that were already queued;
    - before the presenter's blit.
    Each kind is split by size. The line also gives the hand-over -> recorded ->
    vkQueueSubmit latency and the CPU's waits for the GPU (flush_locked), and a second line
    shows what ended the command buffer before each late gap. `tools/gpu_gaps.py --idle LOG`
    sums and names them.
  - `BBHOST_SUBMIT_SPLIT=<n>` (off by default, experimental): every n-th draw, if the GPU
    has run dry, submit the recording mid-job. This is the same draw-boundary submission
    as `BBHOST_PASS_BREAK_SUBMIT`. Unlike `BBHOST_SUBMIT_EVERY_JOB=0` it never holds labels
    back.
  - bbtest: session.txt now has an `in-game frames (MangoHud):` line: average fps, 1% and
    0.1% lows, frame-time mean and deviation, frames over 8.42 ms and over 12 ms, and the
    whole seconds under 120 fps.
- **test6 (FPS that matches what is shown, FSR 4.1.1 cost):**
  - `present: 300 frames in N ms` now says `N fps shown`: the frames presented, which is
    MangoHud's number. The `pass-timer:` line now gives its real window and calls its count
    "upscaled frames ... not the FPS shown". It includes loading screens and frames the
    presenter drops for a newer one: test4 counted ~119 a second while 112 were shown.
  - bbtest: MangoHud logs the whole session (`log_duration=0`; a MangoHud.conf with
    `log_duration=30` cut test5's CSVs to the first 30 s). session.txt has an
    `in-game fps shown:` line, from the frames after the first in-game frame. Its error
    count leaves out the lines that only contain the words (the occlusion report's
    "0 lost", "keyboard focus lost", the DL_PANIC patch, MangoHud's power and battery notes).
  - FSR 4.1.1 takes no reactive mask, so neither the mask nor the opaque snapshot it is made
    from are made for it (test5/test6: 0.09-0.11 ms a frame at 4K). They come back when
    FSR 3.1 stands in and with `BBHOST_FSR4_REACTIVE=1`.
  - The 3000-frame `fsr3:` summary is logged for FSR 4.1.1 too (resets, direct writes).
- **test5 (character shimmer with FSR 3.1, FSR 4.1.1 copy):**
  - The game's object-velocity scale is now checked before use. test4 read (1, 1) for two
    of its three windows. That decodes a still character's velocity texel to a 5-pixel
    motion, and anything else to hundreds of pixels. A real scale (test4's third window:
    8.89, -5, the 160x90 target's size / 18) keeps the scene's aspect. Anything else is
    skipped and logged (`motion: velocity draw ... not a packing scale`).
  - Object motion now replaces the camera's only inside the velocity target's objects (not
    on their 16x16-pixel edges). It is also used only where it differs from the camera's
    motion by more than `BBHOST_OBJECT_MOTION_MIN` (1.5 render pixels; one 8-bit step is
    ~1.1). The motion-vector view shows such pixels dim blue, and used object motion
    bright blue.
  - FSR 4.1.1 also writes the scale pass's target directly (test4: copy 0.26 ms).
    `BBHOST_FSR_DIRECT_TARGET=0` keeps the copy for both upscalers.
  - Rejected and documented: `BBHOST_SUBMIT_EVERY_JOB=0` (test4 run C: 64 fps, 1% low
    14.5, flashing effects). The default stays a submit per job.
- **test4 (performance at 4K output, camera):**
  - The scene camera is now found every frame. test3's probe showed that most of the
    G-buffer draws' V#s carry no record count, so their size read as 0 and their scene
    constants were never looked at; the camera only came from one pipeline whose V# said
    864 bytes (test3: 1582 history resets in 3000 dispatches, jitter pausing and resuming).
    Such a V# is now read as far as the shader reads it, and a block whose head matches is
    read on to its 864 bytes when that memory is mapped.
  - FSR 3.1 writes the scale pass's 3840x2160 target directly instead of its own image plus
    a copy (test3: copy 0.23 ms a frame). Only the output-sized RGBA16F targets take STORAGE
    usage, only with the split and FSR 3.1/4.1.1 chosen. `BBHOST_FSR_DIRECT_TARGET=0` keeps
    the copy. The `fsr3:` summary counts `written directly`.
  - Measured (TESTING.md "test4"): at 4K output the GPU is the limit (busy 91-93% from the
    busy meter, ~300 W). `gpu-wait` is the command processor waiting for one of its 16
    submission slots, i.e. it runs ahead of the GPU. It is not a host-side stall.
- **test3 fixes:**
  - The dark, stretched picture with a grey band at 3840x2160 (test2) is fixed. Any upscaler
    preset below Native AA now makes the display buffers the output's size at the start.
    Before, YEBIS's post-processing was built at the 5120x2160 buffers and read the
    3840-wide picture across 5120. A Resolution change while the game runs that no longer
    matches the buffers runs without the upscaler until the next start (logged).
    `BBHOST_SPLIT_DISPLAY=max` restores the old sizing.
  - The scene camera is looked for as bbport does: any shader stage, any buffer size, and
    inside the G-buffer draws' buffers. `BBHOST_MOTION_PROBE=1` logs what those draws bind.
  - The jitter pauses while no scene camera is found (it could only flicker).
    `BBHOST_JITTER_NO_CAMERA=1` keeps it on.
  - New debug option: `BBHOST_FSR_DUMP=1` (or `[debug] fsr_dump = true` in bbhost.toml)
    writes one frame's FSR inputs, output and target as PNGs, with numbers. It runs at
    `bbtest mark`, with `fsr-dump.request`, and once by itself.
  - The `upscaler: settings at start` line now computes the render size from the Resolution
    setting, not a fixed 3840x2160.
- **test2 fixes:**
  - FSR 3.1 and 4.1.1 now upscale to the active output (the Resolution setting) instead of
    the larger display buffers that live resolution keeps. test1 showed the scene in only
    about a quarter of a 4K screen.
  - The scene camera is also found by the contents of its constants, so FSR's history
    should no longer reset every frame (needs a hardware check).
  - Clearer reset lines in the log.
  - The setup window's token row says the token is optional.
- **Per-pass GPU timing:** **Show Upscaler GPU Time** now breaks the renderer's work down: the
  motion vectors, reactive mask, opaque snapshot, FSR 3.1 or 4.1.1 dispatch, and output copy.
  It is on the overlay and in the log (`pass-timer:` every 10 s).
- **Fewer allocations per frame:**
  - The FSR 3.1 backend reuses its constant buffers and descriptor pools. Before, it made a
    driver memory allocation and a descriptor pool for every dispatch. This comes from a
    build-time patch to copies of FSR-Vulkan's files (`cmake/patches/`); the submodule is
    unmodified.
  - The motion pass's and FSR 4.1.1's image views are kept while their images live.
- **Game Settings menu resolution freeze:** a resolution, upscaler or quality change made in
  the game's own System menu now waits for the pick list to close before it resizes. A
  never-shown question is withdrawn. F10 is unchanged.
- **Packaging:** `tools/package_linux.sh` names the tarball for `git describe` and adds
  `build-fsr4.sh` with the FSR 4.1.1 builder. It refuses to package any DLL, game file or FSR
  4.1.1 asset, and writes a `.sha256`.
- **Vulkan validation pass:** every Vulkan test and the stream selftest run clean of our
  own errors under the Khronos validation layer with sync validation (`tools/vvl_check.sh`).
  New `fsr411_fallback_test` covers FSR 4.1.1's failure paths. The selftest now checks the
  kept pass views. motion_shader_test no longer leaks. FSR-Vulkan's own back-to-back clears
  are reported in DEVLOG, not patched.
- **Docs:**
  - README: a "Testing bbhost-vk" section.
  - TESTING.md: the test-build session.
  - The updater needs no token (the repository is public).
- **CI fix:** the release workflow's package-all job (the Steam Deck kit and the bundle check) failed
  because it looked for `bbhost-linux-*` while the Linux package is now `bbhost-vk-linux-*`. The
  Steam Deck and playtest kits, the bundle check, the publish step and the release announcement
  now take either name (the kit is `bbhost-vk-steamdeck-*`), so the live-server check covers the
  Linux and Steam Deck bundles again.

## Unreleased (Phase 4 groundwork and test-build prep, branch `phase-4-fsr411`)
- **FSR 4.1.1** replays AMD's 4.1.1 upscaler on Vulkan (ported from
  bbport) from assets you build once from your own
  `amd_fidelityfx_upscaler_dx12.dll` (`tools/fsr4cap/build_assets.sh`, under
  Proton; for example the DLL in an OptiScaler release). It uses FP8 on
  RDNA4 and INT8 elsewhere. Without the assets, or on a GPU that lacks the
  features, it is FSR 3.1, and the log says why.
- Setup window: the FSR 4.1.1 DLL path and asset status, and **Upscaler
  jitter** (also in F10).
- The update check looks at bbhost-vk's own releases; versions like
  `v0.2.18-vk.1` are ordered.
- docs/vk/KNOWN_ISSUES.md: the resolution question in the game's own
  Settings menu (upstream issue).

## Unreleased (Phase 3: FSR 3.1 temporal, branch `phase-3-fsr31`)
With Upscaler Off or FSR 1, or Upscaling quality at Native AA, nothing
changes. Online play (servers, sign-in, networking), HDR and motion blur are
untouched.
- **FSR 3.1 upscales the 3D scene.** With the split active and Upscaler FSR
  3.1, FidelityFX FSR 3.1.5 (FireBurn/FSR-Vulkan, MIT) runs at the game's
  scale pass with the scene colour, depth, motion vectors, jitter and a
  reactive mask, resets its history on camera cuts, uses its own auto
  exposure and sharpens with RCAS from **Upscaler sharpness** (0 = off).
  Its output replaces the game's bilinear copy; the UI is drawn on top at
  the output size as before. Every preset (Native AA to Ultra Performance)
  works; a preset or resolution change makes a new context.
- **FSR 4.1.1** selected uses FSR 3.1 until Phase 4 (logged once).
- **Fallback**: if FSR 3.1's context cannot be made, or a frame lacks its
  inputs, the frame is drawn with the Phase 1 bilinear copy and the reason
  is logged (`fsr3:`).
- **Upscaler reactive mask** (F10 and the setup window, default On): a mask
  from the scene against the opaque snapshot - particles, fog, transparents,
  Blood Echoes, the lantern's glow - so FSR keeps less history there.
- **Show Upscaler GPU Time** adds the FSR 3.1 dispatch's own GPU time to the
  GPU line (`FSR 3.1 x.xx ms`) and to the log.
- Test switches: `BBHOST_FSR3=0` (bilinear copy instead of FSR 3.1),
  `BBHOST_REACTIVE=0`, `BBHOST_FSR_JITTER_SIGN=x|y|xy` (flip the jitter
  FSR is told, should the picture wobble), `BBHOST_REACTIVE_SCALE`,
  `_THRESHOLD`, `_VALUE`.
- Tests: `fsr3_upscaler_test` (contexts at every preset for 720p and 4K
  on software Vulkan, resource sizes, flat frames, reset on a cut) and the
  FSR 3.1 policy in `temporal_test`.
- Not done: object motion from skinned meshes' previous bone palettes
  (bbport) - deferred, see docs/vk/DEVLOG.md.

## Unreleased (Phase 2: inputs for temporal upscaling, branch `phase-2-mv-jitter`)
With Upscaler Off or FSR 1, or Upscaling quality at Native AA, nothing
changes: no draw, viewport or pass is touched. Online play, HDR and motion
blur are untouched.
- The scene's motion vectors: a compute pass at the scale pass turns the
  scene depth and this and last frame's camera (from the game's 864-byte
  scene constants, matrices handled in double) into an RGBA16F motion
  image; where the game draws its own object velocity, the object's motion
  is used. Camera cuts (teleport, a fast turn, FOV change, resize, a gap)
  reset the history and are logged (`camera-cut:`).
- Halton(2,3) jitter on the main scene's geometry draws, only with the
  split active and FSR 3.1 or FSR 4.1.1 selected; the phase count follows
  FidelityFX (18 at Quality). Nothing resolves it until Phase 3, so the 3D
  scene shimmers slightly in this build with those upscalers
  (`BBHOST_JITTER=0` turns it off). Logged (`jitter:`).
- An opaque-colour snapshot before the first transparent draw, for the
  upscalers' reactive masks; exposure is left to their auto-exposure.
- A backend-agnostic `UpscalerInputs` (src/host/upscaler_inputs.h) for FSR
  3.1, FSR 4.1.1 and later backends.
- **Show Motion Vectors** now draws the motion vectors over the picture
  (red/green = motion, blue = object motion).
- Test switches: `BBHOST_JITTER=0`, `BBHOST_MOTION_VECTORS=0`,
  `BBHOST_OBJECT_MOTION=0`, `BBHOST_OPAQUE_SNAPSHOT=0`.
- Tests: `temporal_test` (Halton, camera history and cuts, motion math) and
  `motion_shader_test` (the shader against the CPU math on any Vulkan
  device).
- Not yet: object motion from last frame's bone palettes (bbport) is only
  surveyed and logged.

## Unreleased (Phase 1: scene/UI resolution split, branch `phase-1-scene-ui-split`)
With Upscaler Off, or Upscaling quality at Native AA (the default), nothing
changes: the game's code and draws are stock. Display-only; online play,
HDR and motion blur are untouched.
- Upscaling quality now sets the 3D scene's render size when an upscaler is
  on: output (the Resolution setting) / preset ratio, a multiple of 8 - e.g.
  4K Quality renders the scene at 2560x1440. Post-processing, the Scaleform
  UI and the display buffers stay at the output size, so menus and text are
  sharp. Based on Kyo's Decoupled UI patch sites for 1.09, re-encoded for
  any size, with host stubs instead of eboot code caves
  (`src/engine/scene_split*`). `BBHOST_SCENE_SPLIT=0` turns it off.
- The game's scene-to-UI scale pass (`ccbf44a6`) is drawn as a plain
  bilinear copy while split (FSR 3.1 replaces it in Phase 3).
- Changing Upscaler or Upscaling quality in F10 or the System menu resizes
  the scene live, at the same point and delay as a resolution change.
- Log lines for testing: `scene-split:` (render size, scale pass, site
  bytes), `heap:` before/after each change, `yebis: restart N`.
- `scene_split_test`: the site table against Kyo's patches at four sizes.

## Unreleased (Phase 0: groundwork, branch `phase-0-groundwork`)
Nothing changes on screen or online in this phase: with every new setting at
its default (and with Upscaler Off or FSR 1) the game is stock bbhost v0.2.18
plus the 120 fps option.
- FSR-Vulkan (FireBurn, MIT) added as the submodule `third_party/FSR-Vulkan`,
  pinned at c64f093, and built into bbhost: the FSR 3.1.5 and 3.1.4 upscalers
  with their embedded SPIR-V tables (all 11 FSR 3.1.5 passes; checked by
  `upscaler_test` and logged at start). Not called yet. `THIRD_PARTY_NOTICES.md`;
  the Linux package ships the MIT notices under `licenses/`.
- New settings in `bbhost-options.toml`, the F10 screen (UPSCALING), the
  in-game System menu (PC Upscaling, Upscaling Quality) and the setup window:
  `upscaler` (Off / FSR 1 / FSR 3.1 / FSR 4.1.1, default FSR 1),
  `upscale_quality` (Native AA 100% / Quality 67% / Balanced 58% /
  Performance 50% / Ultra Performance 33%, default Native AA),
  `upscale_sharpness` (0.0-1.0, default 0.8 = stock FSR 1's sharpening),
  `debug_motion_vectors`, `debug_upscaler_time`. Stored and logged only.
- Debug tooling: a per-pass Vulkan timestamp timer for the presenter (shown
  under the FPS counter and logged with Show Upscaler GPU Time), a motion-vector
  view hook (no-op until Phase 2), and a presented-frame PNG capture
  (`BBHOST_VK_CAPTURE=1`; Shift+F12, a `capture.request` file, or
  `BBHOST_VK_CAPTURE_EVERY`).
- `packaging/vk/build.sh`: the CI's portable release build, stripped like the
  release package, with SHA-256 and BUILDINFO.txt.
- `packaging/vk/bbtest` and `docs/vk/TESTING.md`: test sessions on the
  player's PC (log, MangoHud CSV, monitor screenshots, replay clips, marks).
- `docs/vk/DEVLOG.md`: the development log.
- CI checks out submodules.

## v0.2.18-vk.0
- Based on bbhost v0.2.18 (c421ea0).
- Added a 120 fps frame-rate option to the launcher and the F10 menu.
- Added the KDE/Wayland scaling wrapper (`packaging/vk/bbhost`).
- Added the development plan (`docs/vk/PLAN.md`) and the launcher mockup.
