# heliasdream-vk development log

A running log: one dated entry per phase or work session (times are
America/Chicago). Each says what was done, the decisions and why, what was
tried and did not work, what is still open, and the test results.
The plan is [PLAN.md](PLAN.md); how sessions on the player's PC are recorded
is [TESTING.md](TESTING.md).

---

## 2026-10-18 (Sun), Phase 0: Groundwork (branch `phase-0-groundwork`)

Base: `main` = upstream Helia's Dream v0.2.18 (c421ea0) + the 120 fps option
(e9d124d) + fork docs; tag `v0.2.18-vk.0`. Work done on the build box
(Debian 13, clang 19.1.7, glibc 2.41), where the game cannot run.

### Done
- **Reproducible build** (`packaging/vk/build.sh`). Found how the shipped
  `v0.2.18-vk.0` binary was made: `CI_PORTABLE=1
  CI_CMAKE_ARGS=-DHELIAS_RELEASE=ON tools/ci_build.sh` on this box
  (`build-ci/build/CMakeCache.txt`: RelWithDebInfo, HELIAS_RELEASE=ON,
  `-static-libstdc++ -static-libgcc -fuse-ld=lld`; prefix with
  Vulkan-Headers 1.4.350, SDL 3.4.16, ffmpeg 7.1.1). The stripped
  `heliasdream.bin` is exactly `objcopy --strip-debug` of it (as
  `tools/package_linux.sh` does): byte-identical SHA-256. build.sh wraps
  that, inits the submodules, strips, writes the SHA-256 and a BUILDINFO.txt,
  and says when the host is not the reference Debian 13 / clang 19.
- **FSR-Vulkan** (FireBurn, MIT) as the submodule `third_party/FSR-Vulkan`
  at c64f093, built by `cmake/fsr_vulkan.cmake`: `ffx-vulkan::portable`
  (FSR 3.1.4) and `ffx-vulkan::fsr3-vk-backend-3.1.5` with the checked-in
  SPIR-V tables. Not called. `upscaler_test` and the start-up log check
  that all 11 FSR 3.1.5 passes' modules are embedded (11 of 11).
  THIRD_PARTY_NOTICES.md, README credits; the Linux package now ships the
  MIT notices; CI checks out submodules.
- **Settings**, stored and logged only: `upscaler`, `upscale_quality`,
  `upscale_sharpness`, `debug_motion_vectors`, `debug_upscaler_time` in
  `heliasdream-options.toml` (src/host/options.cpp, settings.h); F10 screen
  (new UPSCALING header after Resolution); in-game System menu (two new
  sections); setup window (two combos, a slider, two checkboxes).
  `src/host/upscaler_presets.h` is the one place the ratios and the render
  size are computed.
- **Debug tooling**: `GpuTimer` (src/host/gpu_timer.*), Vulkan timestamps
  around the presenter's passes (`present`, `upscale` or `one pass`), read a
  frame late without waiting, shown under the FPS counter and logged every
  300 frames; the motion-vector hook (logs once); the presented-frame capture
  (src/host/frame_capture.*).
- **bbtest** (`packaging/vk/bbtest`, docs/vk/TESTING.md).

### Decisions and why
1. **Upscaler "Off" draws as FSR 1 in Phase 0.** The brief says Off/FSR 1
   must be exactly stock, and stock Helia's Dream always uses FSR 1 when it renders
   smaller than the window. Making Off mean bilinear or native now would
   change the picture. Later phases can give Off its own meaning (the plan
   for that: Off = render at the window size, no upscale); the line help
   says "not used yet" today.
2. **Defaults change nothing**: upscaler FSR 1, quality Native AA, sharpness
   0.8. 0.8 is chosen because FSR 3's sharpness runs 0..1 and stock FSR 1's
   RCAS uses 0.2 stops, and `rcas_stops = 1 - sharpness` maps 0.8 to exactly
   that. Sharpness is stored in tenths (eleven steps) so the in-game slider
   (0..10) and the toml agree without conversion.
3. **Two in-game sections, not one.** The game's option section takes one
   pick list and only on its last row (it is translucent and opens over the
   rows below); the left/right choice row holds two entries at most. Upscaler
   (4 choices) and Quality (5) both need pick lists, so: **PC Upscaling**
   (Show Motion Vectors, Show Upscaler GPU Time, Upscaler) shaped like PC
   Enhancements, and **Upscaling Quality** (Sharpness slider, Quality) shaped
   like Steam Deck. System rows 110014/110015, sprite depths 391/392, title
   ids 125020/126020, generator version 9.
4. **The new sections are made defensively.** They cannot be tested here (no
   game dump on the build box). They are generated after the stock eight,
   inside their own try/catch, with a check that depths 391/392 are free;
   if anything fails the stock movie is kept, a `vk-sections` file beside
   the stamp records it, and the System menu leaves the two rows out. They
   are also left out when the player's own optionsetting.gfx (paths.mods)
   overrides the generated one. So the worst case is "two System rows
   missing", not a crash.
5. **Frame capture is opt-in** (`HELIAS_VK_CAPTURE=1`). It needs
   TRANSFER_SRC on the swapchain images; stock Helia's Dream does not ask for it,
   so it is only added when the variable is set. Triggers: Shift+F12 (only
   with the variable; otherwise Shift+F12 stays F12), a `capture.request`
   file in the capture folder, or a timer. A file trigger was chosen over a
   signal (SIGUSR2) because a signal lands on any thread, including the
   game's, and Helia's Dream runs guest code natively; polling one stat() every
   100 ms from the window pump is harmless. The copy is recorded after the
   image is in PRESENT_SRC and read after the next fence wait, then written
   by a thread of its own (8-bit BGRA/RGBA and 10-bit formats).
6. **GPU timer records nothing while its toggle is off**, so the command
   buffer is stock; it gets its own query pool (gpu_busy and fsr.cpp have
   theirs).
7. **Not labelling FSR-Vulkan's "source-v07 FSR4" provider as FSR 4.1.1**,
   and not building it. FSR 4.1.1 comes in Phase 4 from the player's own DLL.
8. **Windows**: `HELIAS_FSR_VULKAN` defaults OFF on WIN32 until it is tried there.
9. **bbtest**: screenshots prefer `gpu-screen-recorder -w <monitor> -o x.png`
   (that monitor's own pixels), falling back to `spectacle -b -n -f` cropped
   with ImageMagick to the monitor's rectangle from `kscreen-doctor -j`
   (scaled by image width / logical desktop width, which handles the 150%
   monitor). MangoHud gets `read_cfg,...` so goverlay's config is kept. The
   options file copies drop the `[account]` section (the token). Replay via
   SIGUSR1 to a `gpu-screen-recorder -r 30` started with the session.
10. **No release, no tag.** main untouched; PR into main.

### Tried, did not work / dead ends
- `llvm-strip`, `strip` and `llvm-objcopy --strip-debug` all give binaries
  different from the shipped one; only GNU `objcopy --strip-debug` matches.
- A first frame-capture memory-type loop over a braced list of flags did not
  compile (mixed int / VkMemoryPropertyFlags deduction); replaced by an array.
- Starting the built binary on the box with an empty dump only reaches the
  "no eboot" stage, so the start-up `upscaler:` log line and the menus could
  not be seen running here.

### Open issues / risks for the player's test
- The in-game System sections are untested against a real dump. If the
  generator rejects them, PC Upscaling/Upscaling Quality are absent from
  System (log: `menu assets: the PC Upscaling sections were not made`), and
  F10 + the setup window still have every setting. Regenerating the menu
  assets (generator 9) happens once at the first start.
- The GPU timer and frame capture are untested on a real GPU (no GPU here).
  Both are inert unless switched on.
- The F10 screen gains six rows; it scrolled already.
- Upstream's updater still compares against droogie/bbhost's releases
  (unchanged from v0.2.18-vk.0); don't accept an in-app update, it would
  replace the fork's binary.

### Test results (build box)
- `packaging/vk/build.sh`: configure, build and ctest pass: 43 tests, 0
  failures (20 skip without game files, as upstream). `upscaler_test`:
  presets (3840x2160 Performance -> 1920x1080, Balanced -> 2259x1271, ...)
  and "FSR 3.1.5 embedded SPIR-V modules: 11 of 11".
- `bbtest` dry run with a stand-in `./heliasdream`: start (foreground and
  `--detach`), status, mark (log line, `capture.request`), clip without
  `--record` (says so), stop, bundle with session.txt / marks.log.
- Phase 0 binary: built by `packaging/vk/build.sh` from ba5082a
  (`v0.2.18-vk.0-9-gba5082a`), stripped, needs GLIBC_2.38, SHA-256
  `571c7ebcc82271cc04b47676e40db6800f0b17db9fc405711bc03cf22e1dc5b8`
  (on the build box at /workspace/heliasdream-vk-builds/phase0/; not released).
- Player's PC test: pending (checklist in TESTING.md, "Phase 0 test").

---

## 2026-10-18 (Sun), Phase 1: Scene/UI resolution split (branch `phase-1-scene-ui-split`)

Base: `phase-0-groundwork` (PR #1, unmerged); this branch's PR is stacked on
it. Work done on the build box; there is no game dump here, so nothing below
ran against the game yet.

- **Plan change (2026-10-18):** `main` f813313 added Phase 7 (FSR 4 INT8 for
  Steam Deck/RDNA2-3, DLSS, XeSS) and Phase 8 (Windows release) to PLAN.md at
  the player's request; Phase 1 keeps the upscaler interface
  backend-agnostic (`ScenePass` in host/upscaler.h) so they plug in later.

### Findings
- **The scale pass** is the fullscreen draw with pixel shader `ccbf44a6`
  (IFreemz's `HdrSceneCopy`): it copies the HDR scene (image slot 0) into the
  target the game's post-processing runs on. With the scene and the output at
  one size it is a 1:1 copy; with the decoupled-UI patches it is the
  game's own upscale. In Helia's Dream it is a pipeline `VS+ccbf44a6` in
  host/render.cpp.
- **What sizes the scene** is Kyo's "Decoupled UI" patch set for 1.09
  (shadps4-emu/ps4_cheats `PATCHES/Bloodborne.xml`, 640x360 to 2560x1440).
  Decoded, its 24 sites are (Binary Ninja addresses, 0x400000 base):
  - 0x2697aea/af0: the graphics manager's scene context (G-buffer, depth,
    HDR lighting) at construction;
  - 0x26f8ce1/ce7, 0x26f93d8/3df, 0x26f99db/9e2: the contexts made on demand
    (the scene's helper targets);
  - 0x26c6624/662e, 0x26c6760/676a and the two branches 0x26c65f6, 0x26c6735:
    the resize slot sub_26c6540, so a resize re-makes them at the scene's size;
  - 0x269c7d5: the resize listener stores the render size (a jump to a cave);
  - 0x26c3af3: Initialize's colour is always DefRenderTarget;
  - 0x25d818d: YEBIS's viewport from the output entry (+0x80/+0x84);
  - 0x125b1a3/1aa/1d6/1de and 0x26c3de4/df0: the half- and quarter-size
    targets from the scene's size, not the output's;
  - 0x1258f2c: the output-size query returns the output entry (a cave call).
  Everything else - post-processing after the scale pass, Scaleform, the
  display buffers, DefDepthStencil - stays at the output size, which is what
  live_resolution already sizes.
- Kyo's bytes are the same at every size except the immediates (width,
  height) - `tests/scene_split_test.cpp` checks our encoder byte for byte
  against all four of his sizes.

### Done
- `src/engine/scene_split_sites.h`: the sites as a table with the render size
  as its parameter (no dependencies, unit-tested); `render_size()` = output /
  preset ratio, rounded down to a multiple of 8, at least 64, never above the
  output; the bilinear S# rewrite.
- `src/engine/scene_split.cpp`: keeps the load-time bytes of every site, logs
  them, checks the branch sites are short Jccs and 0x26c3af3 is the game's
  `74 36` or heliasdream's `75 11`, refuses otherwise; the two cave targets are
  host stubs within rel32 reach (`engine_alloc_exec_near`), not eboot data.
  `HELIAS_SCENE_SPLIT=0` never splits.
- `live_resolution` sizes two things at its quiet point: the output (as
  before) and the scene. A scene change writes the sites before the GX
  resize broadcast; the on-demand contexts move from the old scene size to
  the new; the graphics manager's scene size is read back and logged; an
  unfit change is rolled back. The start applies the scene's size at load,
  before the graphics manager is built. Changing Upscaler or Upscaling
  quality (F10, System, setup window) asks for the scene alone after the
  usual 1.5 s menu-settle delay.
- The scale pass is drawn as a plain bilinear, clamped, no-mip, no-aniso copy
  while split (the S# is rewritten before the sampler is made). FSR 3.1
  replaces it in Phase 3.
- Logging for the test session: `scene-split: render size WxH for an output of WxH`,
  `scene-split: scale pass ... drawn` (once per size, then every 3000 flips),
  `heap: render-target heap ...` before and after each change,
  `yebis: restart N ...` for each YEBIS shutdown/restart and its dropped
  frees, and each site's original bytes at load.
- Help texts (setup window, F10, System) say what the options now do.

### Decisions and why
1. **Output = the Resolution setting; render = output / preset ratio.**
   Output-size work (post, UI, display) is what Resolution always meant, and
   the preset is the one control that should cost or save GPU time.
2. **Split only when Upscaler is not Off and the preset is not Native AA.**
   Otherwise every site has its load-time bytes and the scale pass is
   untouched - the same code bytes and the same draws as stock, so Off and
   Native 100% are bit-identical to stock by construction (nothing is
   written, `code_write` skips equal bytes).
3. **Host stubs instead of Kyo's caves.** His caves overwrite eboot rodata
   at 0x4d3bad4/0x4d3baf0; heliasdream's code pages are its own, and a stub near
   the code avoids guessing whether that rodata is unused.
4. **0x26c3af3 gets `EB 36`, not only `EB`.** heliasdream's both-sides patch
   rewrites the same branch to `75 11`, so the displacement has to go back
   to the game's too.
5. **Bilinear by S# rewrite, not a new shader.** The game's own pass stays;
   only its filter changes, so HDR, the colour format and post are as they
   were. No HDR, motion-blur or online changes; nothing touches the
   presenter, so goverlay/MangoHud see the same swapchain as before.
6. **Rescale-only requests reuse the resolution path** (same quiet point,
   same settle delay) instead of a second mechanism.

### What the other projects taught (no code copied)
- **IFreemz** (shadPS4-Bloodborne-DLSS-FSR, GPL-2.0): the name and hash of
  the scale pass (`HdrSceneCopy`, `ccbf44a6`) and the idea that the game's
  own decoupled-UI path is the place to hook an upscaler.
- **Kyo** (patches in shadps4-emu/ps4_cheats): the site list itself;
  heliasdream-vk re-encodes it from the render size (facts about the 1.09 eboot,
  not code).
- **bbport** (deadinside28/bloodborne_pc, GPL-2.0): a host-side approach -
  keep guest allocations at 1920x1080, render the scene into host proxies at
  output / scale, reconstruct at the first Scaleform pass, resolve proxies
  back for guest compute, align sizes (it hit a 1916x1080 alignment bug).
  We took the alignment lesson (render sizes are multiples of 8) and the
  "resolve/settle at a display-pass boundary" idea; we chose the game's own
  decoupled path over proxies because Helia's Dream runs the game's code natively
  and already resizes the real targets.
- **Supermedo** (Supermedo/bloodborne_pc, GPL-2.0): the Windows build of
  bbport; same scene handling (identical `ui_composition.h` /
  `vk_temporal_upscaler.cpp` approach). Useful as a reference for Phase 8.
- **Awakenchan** (BloodBorneButPc, GPL-2.0, from DarkIzuku's bbport fork):
  memory budgeting - only a scene above 1080p grows the guest heaps; an
  output-only increase does not. Phase 1 never renders the scene above the
  output, so splitting can only shrink the scene's heap use; the `heap:`
  lines confirm it on the first test. Its notes on point-sampled reduced
  proxies (blocky post) are why the open risks list includes post passes
  that read render-size targets.

### Open risks (only the hardware test can answer)
- The sites' original bytes are unverified (no dump here, and no CRC table
  covers them). The 1.09 SHA gate and the branch checks guard it; the first
  log prints every site's bytes.
- Whether the graphics manager re-makes its scene context when only the
  scene size changes (logged: `scene-split: the graphics manager's scene is
  WxH`, or `..., not the WxH asked for`). If not,
  a live Native<->67% toggle needs a resolution change or a restart.
- The scene's colour is DefRenderTarget at the max display size; with YEBIS
  reading its viewport from the output entry, the old black/grey band issue
  could come back at some sizes.
- Post passes that read render-size depth or half-size targets with point
  sampling could look blocky; item icons and Scaleform's offscreen targets
  could be sized from the wrong entry.
- Whether `ccbf44a6` samples with `image_sample` (the S# rewrite) - logged.
- Sites are written at the quiet point while other guest threads may run.

### Test results (build box)
- `packaging/vk/build.sh`: configure, build and ctest pass: 44 tests, 0
  failures (16 skip without game files). `scene_split_test`: "24 sites
  match Kyo's Decoupled UI patches at 4 sizes; render sizes and the bilinear
  S# ok".
- Phase 1 binary: built by `packaging/vk/build.sh` from d38480b
  (`v0.2.18-vk.0-19-gd38480b`), stripped, needs GLIBC_2.38, SHA-256
  `96bfcf84d6ef42f2fa77f9c6e7335aa49ee6e77309c97ac86a507b8275f60f2c`
  (on the build box at /workspace/heliasdream-vk-builds/phase1/; not released).
- Player's PC test: pending (TESTING.md, "Pending combined test session").

---

## 2026-10-18 (Sun), Phase 2: Inputs for temporal upscaling (branch `phase-2-mv-jitter`)

Base: `phase-1-scene-ui-split` (PR #2, unmerged); this branch's PR is
stacked on it. Work done on the build box, with no game dump: the math and
the shader are tested here (the shader on lavapipe), everything that
depends on the game's own data is logged for the first test.
No upscaler files were needed: no OptiScaler release was downloaded and no
FSR DLLs were used (FSR 3.1 comes from the FSR-Vulkan submodule, as in
Phase 0).

### Findings
- **Pipeline names are hashes the other projects use.** Helia's Dream names a
  pipeline `VSHASH+PSHASH` from the OrbShdr footer, which is shadPS4's
  `pgm_hash`, so IFreemz's shader hashes apply unchanged.
- **The scene constants** are an 864-byte (216-float) constant buffer
  (bbport): [0] far (3000), [1] 1/far, [4..5] the render size, [6..7] its
  reciprocals, the view as 3x4 rows at [8..19], the projection at
  [52, 57, 62, 63], the inverse view at [180..191]. The previous frame's
  matrices are not in it, so Helia's Dream keeps its own history.
- **Two cameras a frame**: two map sections can bind different scene
  constants in one frame (IFreemz); the camera used by most draws is the
  scene's.
- **The game draws its own object velocity** (IFreemz): pixel shaders
  `34bc187c` (opaque, packing scale in constant buffer 0), `749e4f9e` and
  `b25e4fae` (alpha-tested, scale in buffer 1), into a coarse 160x90 RGBA8
  target; decoded delta = (rg - 0.5) * (-w/sx, h/sy) where alpha > 0.5.
- **Jitter eligibility** (IFreemz, bbport): draws whose depth is the scene
  depth and that are indirect, or have more than 6 indices, or more than one
  instance; the scene depth is the one bound while drawing into the scene
  colour. Fullscreen passes (6 indices or fewer) and UI vertex shaders
  (`34e8a281`, `81d336ce`, `09957251`, `24042a9b`, `a400228b`) never jitter.
- **Phase count**: FidelityFX's `int(8 * (display / render)^2)`: 18
  (Quality), 23 (Balanced), 32 (Performance), 72 (Ultra Performance), 8 at
  native.

### Done
- `src/host/temporal/` (pure, header-only, unit-tested in
  `tests/temporal_test.cpp`):
  - `halton.h`: Halton(2,3), phase count, offset = halton(i % n + 1) - 0.5
    in render pixels (+x right, +y down).
  - `camera.h`: scene-constant parsing with signature checks (far, size,
    reciprocals, unit rotation rows) and a double-precision inverse;
    `CameraHistory` (majority camera of up to 8 a frame, previous frame
    kept); camera-cut detection - first frame, no camera, a gap of more than
    2 frames, a teleport (> 20 units, IFreemz's section switch), a rotation
    (> 45 degrees), a projection change (> 10% FOV), a resize;
    `make_reprojection` (previous view x inverse current view, in double);
    `camera_motion` (the shader's math in double, for the tests).
  - `motion_classify.h`: the hashes above, buffer classes (864 = scene,
    640..16384 = skeleton, 96..639 in steps of 48 = small skeleton; bbport),
    `jitter_eligible`, the blend bit, and bbport's moving gate (a palette
    whose hash matches the previous frame is static).
- `src/host/upscaler_inputs.h`: the backend-agnostic `UpscalerInputs` -
  colour, depth, motion, the opaque-colour snapshot, (reactive, exposure:
  empty, the backend generates them), the jitter and its phase count,
  reset/cut, near/far, vertical FOV, frame time. Phase 3's FSR 3.1 and
  Phase 4's FSR 4.1.1 take this; Phase 7's FSR 4 INT8, DLSS and XeSS take
  the same struct.
- `src/host/temporal_inputs.cpp` (the runtime) and the hooks in
  host/render.cpp:
  - constant buffers are observed while their V#s are resolved (only while
    watching); the scene block is parsed once per address and content, the
    camera accepted only if it carries the render size;
  - the jitter is added to the viewport of jitter-eligible draws;
  - at the scale pass (`ccbf44a6`): the camera history ends the frame, cuts
    are logged, `shaders/motion.comp` fills an RGBA16F motion image at the
    render size (rg = current-to-previous motion in render pixels, b = object
    motion, a = camera valid), and the next jitter phase is chosen;
  - before the first blended draw into the scene colour, the colour is
    copied (the opaque snapshot, for the backends' reactive masks).
- Show Motion Vectors (Phase 0's toggle) draws the motion image over the
  picture in the presenter (`shaders/motion_view.frag`): red/green =
  horizontal/vertical motion (|d| / 8 px, square root), blue = object
  motion, a dark wash where there is no valid camera. With the toggle on the
  motion vectors are computed even without a temporal upscaler (still only
  while the scene is split).
- Logs: `jitter:` (on/off and why, phase count), `camera-cut:` (each cut
  and its kind, repeats compressed), `motion:` (scene constants found, the
  scene depth, the first motion pass, a summary every 3000 frames:
  cameras, cuts, velocity draws, opaque snapshots, palettes moving/static).
- Switches for A/B on the test PC: `HELIAS_JITTER=0`,
  `HELIAS_MOTION_VECTORS=0`, `HELIAS_OBJECT_MOTION=0`,
  `HELIAS_OPAQUE_SNAPSHOT=0`.
- `tests/motion_shader_test.cpp`: runs the compiled `motion.comp` on any
  Vulkan device (lavapipe here) over a synthetic floor/wall/sky scene 1800
  units from the origin with a turning, moving camera and a jitter, and
  checks every pixel against `camera_motion` in double.

### Decisions and why
1. **Jitter only with the split active and FSR 3.1 or FSR 4.1.1 selected**
   (as the plan asks). Nothing resolves the jitter before Phase 3, so a
   Phase 2 build with FSR 3.1 selected shows a sub-pixel shimmer of the 3D
   scene. That is expected; `HELIAS_JITTER=0` removes it for comparison.
2. **Off, FSR 1 and Native AA are untouched by construction**: the runtime
   decides once per flip whether it watches (split, and a temporal upscaler
   or Show Motion Vectors); when not, every hook is a single branch and no
   draw, viewport, buffer or pass changes.
3. **Jitter is removed from the motion vectors** in the shader (the pixel's
   position is unjittered before reprojection), so they describe motion
   only; the upscaler still gets the jitter separately.
4. **Matrices in double on the CPU**: Bloodborne's world coordinates reach
   the thousands; the reprojection matrix is built as prev x inverse(cur)
   in double and only the product goes to the GPU, so float precision is
   spent on view-space distances (the GPU test: worst 0.016 px at 1800 units).
5. **Object motion: the game's own velocity first.** It is exact where the
   game draws it and costs nothing. bbport's previous-frame positions for
   skinned meshes, cloth and weapons are only **surveyed** in this phase
   (moving vs static palettes counted, static map pieces skipped by the
   moving gate); re-running those draws with last frame's palettes is
   deferred until the first test shows where the game's velocity is
   missing. This is the main piece of Phase 2 not done.
6. **Reactive mask and exposure**: the opaque snapshot is the input FSR's
   own reactive-mask generation wants; no dedicated R8 mask yet. Exposure
   is left to the backends' auto-exposure (FSR 3.1 and 4 have one), so no
   guess at the game's tonemapping is needed.
7. **The overlay is drawn only in the presenter's one-pass path** (the
   split's normal path); the FSR 1 magnified path does not draw it.
8. **Online play, HDR, motion blur** are untouched; the motion-blur shader
   (`e0305cef`) is not hooked. The presenter's swapchain is unchanged, so
   goverlay/MangoHud see the same as before.

### What the other projects taught
- **IFreemz** (shadPS4-Bloodborne-DLSS-FSR, GPL-2.0-or-later): the velocity
  shaders and their decoding, the jitter rule, the majority camera, the
  section-switch threshold, the view-space reprojection with the sky as a
  direction. `motion.comp` adapts that math (credited in the shader and in
  THIRD_PARTY_NOTICES.md; GPL-2.0-or-later is GPL-3.0-compatible).
  IFreemz replays the velocity draws at render size into an RGBA16F mirror;
  Helia's Dream samples the game's 160x90 target for now (see the risks).
- **bbport** (deadinside28/bloodborne_pc, GPL-2.0): the layout of the
  864-byte scene block, the buffer classes, the viewport-offset jitter,
  the moving gate and previous-frame positions for object motion. Facts and
  ideas; no code copied.
- **Supermedo** (the Windows build of bbport): same approach; nothing new
  for Phase 2 beyond confirming bbport's layout.
- **Awakenchan** (BloodBorneButPc): no new guest allocations in Phase 2 -
  the motion image and the opaque snapshot are host images at the render
  size, so the guest heaps are as in Phase 1.

### Open risks (only the hardware test can answer)
- **Whether the scene constants are found at all** (`motion: scene
  constants found ...`) and whether they carry the render size or the
  output size while split (logged once if the output's).
- **The velocity target**: its alpha semantics, its coarse 160x90 edges
  (object motion bleeds a few pixels around moving enemies in the overlay),
  and whether it is drawn before the scale pass (`velocity draws ... after
  the scale pass` in the summary). If it is late, the frame uses last
  frame's velocity.
- **Jitter coverage**: shadow maps, the sky, decals and particles that use
  the scene depth may or may not shift; a shadow that shimmers more than
  the geometry is the sign.
- **Camera-cut thresholds** (20 units, 45 degrees) against fast rolls,
  ladders, lamp warps, cutscenes and the boss arenas' camera.
- **The opaque snapshot**: the first blended draw may come before all the
  opaque geometry (then the snapshot misses some); its copy costs one
  render-size colour copy a frame.
- **Cost**: one 8x8 compute dispatch at the render size and one colour copy
  a frame, plus a hash of each 864-byte block; measured with Show Upscaler
  GPU Time on the PC.

### Test results (build box)
- `packaging/vk/build.sh`: configure, build and ctest pass: 46 tests, 0
  failures (19 skip without game files). `temporal_test` (Halton, phase counts, scene-constant parsing, history
  and every cut kind, motion against direct projection to 0.02 px);
  `motion_shader_test`: "llvmpipe: 320x180 pixels match the CPU math
  (worst 0.0156 px, mean |motion| 8.83 px, 20404 sky)".
- Phase 2 binary: built by `packaging/vk/build.sh` from 4341e1a
  (`v0.2.18-vk.0-31-g4341e1a`), stripped, needs GLIBC_2.38, SHA-256
  `fd56aa030e6ddbbafdac70647d4e3dd3fa1e445880c5904415fffd4832846728`
  (on the build box at /workspace/heliasdream-vk-builds/phase2/; not released).
- Player's PC test: pending (TESTING.md, "Pending combined test session",
  part C).

## 2026-10-18 (Sun), Phase 3: FSR 3.1 temporal (branch `phase-3-fsr31`)

Base: `phase-2-mv-jitter` (PR #3, unmerged); this branch's PR is stacked on
it. Work done on the build box with no game dump: FSR 3.1 itself is tested
on lavapipe (software Vulkan), everything that depends on the game's frames
is logged for the first test. No upscaler files were needed: no OptiScaler
release was downloaded and no FSR DLLs were used (FSR 3.1.5 comes from the
FSR-Vulkan submodule linked in Phase 0).

### Done
- `src/host/fsr3_upscaler.{h,cpp}`: FSR 3.1.5 over FSR-Vulkan's bridge,
  independent of the renderer (the test drives it on its own device): a
  context for exact render/output sizes with HDR input and FSR's auto
  exposure; its three shared resources (dilated depth R32F, dilated motion
  RG16F, reconstructed previous depth R32UI, all at the render size) and an
  RGBA16F output, allocated by Helia's Dream and imported; per-frame imports of
  the inputs, released only once their submission has retired (the bridge
  destroys the views at release).
- `src/host/fsr3_pass.{h,cpp}`: the renderer's side. At the scale pass
  (`ccbf44a6`) with FSR 3.1 chosen and the split active: checks the frame's
  inputs, makes or reuses the context, decides the history reset, records
  FSR into the frame's command buffer, copies its output into the pass's
  target (or blits, for a non-RGBA16F target) and drops the game's draw.
- `src/host/temporal/fsr3_policy.h`: the pure rules (sizes FSR takes,
  resets, sharpening, conventions, camera), tested in `temporal_test`.
- The motion pass also writes an R32F copy of the scene depth (the bridge
  imports colour images only). A new reactive pass
  (`src/host/shaders/reactive.comp`) builds an R8 reactive mask from the
  scene against Phase 2's opaque snapshot.
- Settings: **Upscaler reactive mask** (F10 and the setup window; not in
  the in-game System menu, which needs game message ids), and FSR 3.1 help
  text for the upscaler, sharpness and GPU-time rows.
- **Show Upscaler GPU Time**: timestamps around the FSR dispatch, read when
  the submission retired, averaged over 0.5 s, shown on the GPU line and
  logged.
- `gpu.cpp` enables `shaderStorageImageExtendedFormats` where the GPU has
  it (FSR's shaders and the R8 mask need it); enabling a feature changes no
  draw.

### Decisions and why
- **Where FSR runs: at the scale pass, instead of the game's draw.** The
  scene is complete there and the UI is not yet drawn, the same point
  Phase 1 made its bilinear copy and Phase 2 took its inputs. FSR writes its
  own image (FSR's output must be a storage image; the game's target is a
  colour attachment), then a `vkCmdCopyImage` (same format) or a blit (any
  other) puts it in the target, with the target's bookkeeping (written,
  pending clears dropped) as for a draw.
- **Exact-size contexts.** FSR's internal resources are sized at creation;
  a context is made per (render, output) size, so a preset or resolution
  change makes a new one (with a reset) and the old one is destroyed once
  the GPU is past its last frame. A failed creation is remembered for those
  sizes and logged once, not retried every frame.
- **Fallback = Phase 1's bilinear copy.** Any frame FSR cannot do (no
  context, no inputs - menus, loads -, a ratio over 3x, a target it cannot
  copy to, `HELIAS_FSR3=0`) runs the game's draw with the bilinear samplers,
  and the reason is logged when it changes.
- **Native AA stays stock.** At Native AA the split is off, so FSR 3.1 is
  not used (no native-resolution AA mode in this phase); it keeps "Native
  identical to stock" simple and true.
- **FSR 4.1.1** uses FSR 3.1 until Phase 4, logged once.
- **Resets**: a camera cut (Phase 2's history), a new context, or a gap of
  more than 3 flips without FSR (its history is of another picture).
- **Conventions, checked in the FidelityFX 2.3 source**: FSR samples
  `pixel + 0.5 - jitter`, so its jitter offset is heliasdream's viewport shift
  in render pixels as is; motion vectors are scaled by `scale / renderSize`
  and added to the UV, so heliasdream's pixel vectors (current to previous) go
  in with scale 1. Depth is not reversed. `HELIAS_FSR_JITTER_SIGN` flips an
  axis should the hardware test show a wobble.
- **Exposure**: FSR's auto exposure, pre-exposure 1 (Phase 2 found no
  exposure value worth trusting).
- **Sharpening**: FSR's RCAS from **Upscaler sharpness**, 0 = off.
- **Reactive mask**: AMD's GenerateReactiveMask rule (tonemap, max
  component of the change, scale 1, threshold 0.2, binary 0.9; tunable with
  `HELIAS_REACTIVE_SCALE/_THRESHOLD/_VALUE`). The opaque snapshot is taken
  before the first blended draw, so everything blended after it -
  particles, fog, transparents, Blood Echoes, the lantern's glow - is in
  the mask.
- **Skeleton-based object motion (bbport): deferred.** Doing it properly
  means running the skinned meshes' guest vertex shaders a second time with
  last frame's bone palettes (a translator change: a second output, palette
  history per draw), and none of it can be checked without game data. The
  game's own velocity target (Phase 2) already gives object motion for
  characters and enemies, and the moving gate keeps static map pieces out.
  The value is in the details (cloth, weapon trails) and is worth revisiting
  once the hardware test shows how much ghosting remains on them.
- **A test that did not hold**: comparing a small change with and without
  a reset assumed FSR would keep the old history without one. For a flat
  picture FSR's shading-change detection takes the new colour anyway (0.0029
  without vs 0.0034 with), so the test checks only that a reset takes the
  new picture at once; the reset rule itself is tested in `temporal_test`.

### What the other projects taught
- **IFreemz** (shadPS4-Bloodborne-DLSS-FSR): run the upscaler at
  `HdrSceneCopy` into its own image and copy it over the target instead of
  the game's draw ("CopyInsteadOfDraw"), RGBA16F scene and target. No code
  copied; heliasdream's copy has its own fallback for other formats.
- **AMD FidelityFX** (MIT): the reactive-mask rule and the conventions
  above. Credited in `reactive.comp` and THIRD_PARTY_NOTICES.md.
- **bbport / Supermedo / Awakenchan**: nothing new this phase beyond
  Phase 2's (bbport's previous-palette approach is the deferred item).

### Online play check (the hard rule)
`git diff c421ea0..HEAD` over phases 0-3:
- `src/net/` (HTTP, auth, STUN, the account flow, the updater's user
  agent): **no changes**.
- `src/hle/`: only `modules.h` and `video.cpp`, the 120 fps pacing (commit
  e9d124d) - no network, HTTP or NP code.
- `src/core/` (config, server defaults): no changes; `heliasdream.example.toml`
  only an upscaler comment; `online.host`, `auth_server` and the STUN server
  defaults are upstream's.
- The user agent is `heliasdream/` + `HELIAS_VERSION` (`src/net/http.cpp`,
  `updater.cpp`, unchanged), and the version is `git describe`, e.g.
  `heliasdream/v0.2.18-vk.0-37-g...`: upstream's base version first.
TESTING.md's combined session now has an online check (part E: sign-in,
messages and bloodstains, a summon sign or bell).

### Open risks (only the hardware test can answer)
- **Jitter and motion conventions on real frames**: a wobble when still
  means the jitter sign (`HELIAS_FSR_JITTER_SIGN`); smearing when moving
  means the motion vectors' scale or direction.
- **Ghosting** on enemies, cloth, weapons and particles: depends on the
  velocity target's coarse edges (Phase 2), the reactive mask's threshold,
  and the missing skinned-mesh motion.
- **Whether the scene is RGBA16F at the scale pass** and the target's
  format (copy or blit; logged with the context).
- **Cost**: FSR at 2560x1440 -> 4K on the PC's GPU (shown on the GPU line);
  FSR-Vulkan allocates its constant buffers per pass per dispatch, a known
  cost to look at in Phase 5 if the time is high. Memory: 346 MiB at
  Quality 4K, 281 MiB at Performance (measured on lavapipe).
- **Camera cuts** missed (a blend of two pictures for a few frames after a
  warp or cutscene) or too many (a softer picture after each roll).
- **Driver**: FSR 3.1.5's shaders on RADV/the PC's driver; a failure is
  logged and falls back to the bilinear copy.

### Test results (build box)
- `ninja` and `ctest`: 47 tests, 0 failures (19 skip without game files).
- `fsr3_upscaler_test` on llvmpipe: contexts at all five presets for 720p
  and 4K, shared resources at the render size in the formats FSR asks
  (100 R32F, 83 RG16F, 98 R32UI); memory 26-59 MiB at 720p, 235-531 MiB at
  4K; a flat frame reproduced within 0.0017 on the reset frame and 0.0020
  at frame 5; a cut with a reset within 0.0078.
- `temporal_test`: the FSR 3.1 policy (sizes, every reset kind, sharpening,
  conventions, camera defaults).
- `motion_shader_test`: the depth copy is bit-exact.
- `packaging/vk/build.sh`: configure, build and ctest pass (47 tests, 0
  failures, 19 skipped).
- Phase 3 binary ("early playable FSR 3.1 build"): built by
  `packaging/vk/build.sh` from 008165c (`v0.2.18-vk.0-39-g008165c`),
  stripped, needs GLIBC_2.38, SHA-256
  `d50f617592061a63f8137e4a3a2dcac88606acc6fe372d96c8e66d7ade46f511`
  (on the build box at /workspace/heliasdream-vk-builds/phase3/; not released).
- Player's PC test: pending (TESTING.md, "Pending combined test session",
  parts D and E).

## 2026-10-18 (Sun), Phase L: launcher mockups (branch `phase-L-launcher`)

Base: `phase-3-fsr31`. Part 1 only: HTML mockups (1280x800, rendered with
headless Chrome) for all ten tabs in docs/vk/mock/, in the Paleblood style
of launcher-play.html / launcher-upscaling.html, waiting for the user's
approval per tab. Each lists only what Helia's Dream has today: the setup window
(launcher.cpp), the F10 rows (options.cpp), the plugin settings the four
bundled plugins declare, the patch manifests in patches/, and a few
heliasdream.toml keys ([video] width/height, [player] ime/name). Online &
account shows exactly upstream's options (Live / Playtest / Custom, scheme,
verify TLS, require account, Discord login, create, recover, website code,
sign out) with placeholders; no real code is shown. The Upscaling mockup
now shows what Phase 3 built; the FSR 4.1.1 card says it is not built yet.
Part 2 (the shared ImGui theme and the tab skeleton) was not started: the
user moved every effort to a first test release. The old launcher stays
the only one.

**FaceGen check added (TESTING.md part F).** shadPS4's exploded or
stretched face vertices came from FaceGen's GPU writes that the CPU never
read back. Helia's Dream imports the game's GPU-visible memory into Vulkan, so
GPU writes are what the CPU reads, and that does not happen. Phases 0-3
leave that memory model alone: FSR 3.1, the motion, depth-copy and
reactive passes read and write only heliasdream's own images and the game's
render targets. They do not touch guest buffers, the buffer shadow or
the readback paths. Part F checks it on the PC: the mirror's appearance
editor, a death and a lamp rest.

## 2026-10-18 (Sun), Phase 4 groundwork and test-build prep (branch `phase-4-fsr411`)

Base: `phase-L-launcher`. The user moved every effort to a first test release.

### FSR 4.1.1
- **OptiScaler:** v0.9.4, the latest release, was downloaded to
  /workspace/upscaler-files/ (outside the repo, never committed). Its
  `amd_fidelityfx_upscaler_dx12.dll` is version 4.1.1.2740 (SHA-256
  d0dcccc7...b46), with the `v07_fp8_no_scale` model. It exports the
  FidelityFX API itself (ffxCreateContext, ffxConfigure, ffxQuery,
  ffxDispatch, ffxDestroyContext), so no loader is needed. bbport's
  `dll_info.py` accepts it.
- **Port from bbport (GPL-2.0-or-later, credited in the files):**
  - `src/host/fsr411/` replays the DLL's 29 passes on plain Vulkan: INT8
    dot products, or FP8 cooperative matrices on RDNA4.
  - `tools/fsr4cap/` builds the asset set once from the user's own DLL under
    Proton, into ~/.local/share/heliasdream/fsr4_411 (`HELIAS_FSR411_DIR`).
- **`src/host/fsr4_pass.cpp`:**
  - The device gets FSR 4.1.1's extensions (compute derivatives, VALVE mixed
    float dot product, cooperative matrices, FP8) only when the assets exist,
    so every other device is unchanged.
  - At the scale pass, with FSR 4.1.1 chosen, the replay upscales into its
    own image, copied into the target as FSR 3.1's is.
  - Missing assets or features, or a failing pipeline, switch to FSR 3.1 for
    the session, with an `fsr4:` log line.
  - The GPU time line names whichever upscaler ran.
- **Not testable here:** there are no assets, no Proton and no RDNA4 on the
  box, so the replay itself is first run on the PC. The capture needs Proton
  and the user's GPU.

### Test-build launcher
- Every heliasdream-vk switch testers need is in the current setup window and
  F10, with no redesign: the upscaler, quality, sharpness, reactive mask, a
  new **Upscaler jitter** toggle, the debug toggles, 120 fps, and the
  FSR 4.1.1 DLL path ([upscaler] fsr4_dll) with the asset status and a
  "copy the build command" button.
- The launcher redesign (Phase L Part 2: theme and tab skeleton) is deferred
  until after the test release; the mockups wait for approval.

### Updater
The update check reads Heliasyu/heliasdream-vk's latest release (a public repo,
read anonymously like upstream's; corrected 2026-10-18), and `-vk.N` versions are compared. The official
plugins still come from upstream's release, signed with its key. Nothing is
published.

### Known issue recorded
Upstream's resolution change in the game's own Settings menu (PC Graphics)
can leave the menus unresponsive at the keep-this-resolution question.
Reading live_resolution.cpp turned up three likely causes and no clear
low-risk fix: a pick list still closing, Scaleform viewports reset under
the new dialog, or the modal not taking focus. They are in
docs/vk/KNOWN_ISSUES.md, with TESTING.md part G to reproduce with and
without the split. The `src/core` change on this branch is only the new
`upscaler.fsr4_dll` key; nothing online is touched.

## 2026-10-18 (Sun), the in-game Settings resolution freeze (branch `phase-5-test-release`)

A static review of the game-menu resolution path (live_resolution.cpp,
option_menu.cpp, options.cpp): the confirm logic is all upstream's. The
user then confirmed the freeze happens **only** with the game's own Settings
Resolution pick list, never with F10. That points at the resize running
while the pick list is still up.
- **Fix (game-menu path only):**
  - A request the System menu makes (resolution, upscaler, quality) now waits
    past the 1.5 s until no popup is up on PC Graphics, PC Upscaling or
    Upscaling Quality, for at most 10 s.
  - A question that was never put up is withdrawn at its timeout, instead of
    showing later.
  - `g_confirm_open` is now atomic.
  - F10 and the setup window never set the menu mark, so they are unchanged.
  - The log says "waiting for the menu's pick list to close before the
    change" when the gate holds a change back.
- **Suspected, not changed:** in KNOWN_ISSUES.md with file:line.
- **Updater:** Heliasyu/heliasdream-vk is public, so the update check needs no
  token. The private-repo hints are gone (the optional token_file still
  works, as upstream's).

## 2026-10-18 (Sun), Phase 5 low-risk performance and the first test build

**Per-dispatch allocations: FSR-Vulkan's bridge.** For every FSR 3.1 pass, a constant buffer
was a vkCreateBuffer plus a vkAllocateMemory, and a descriptor set got a descriptor pool of its
own (about a dozen dispatches a frame). All of it was destroyed again at the retire.
- A wrapper can't reach these, so `cmake/patches/fsr-vulkan-recycle.patch` keeps them instead:
  - buffers by capacity, rewritten through map/memcpy/flush;
  - pools reset and keyed by the pipeline's set layout, with a retry on a fresh pool if a
    reused one fails.
- The patch is applied at configure time to copies of the three files in the build tree. The
  submodule stays at its pinned commit, unmodified.
- If it stops applying (a new pin), the build warns and uses the files as they ship.
  `-DHELIAS_FSR_VULKAN_PATCHES=OFF` skips it.
- fsr3_upscaler_test (real dispatches on lavapipe over several frames, with retires) passes
  with the same error figures.

**Persistent resources.**
- The motion pass's depth and velocity views, and FSR 4.1.1's three input views, used to be
  made and destroyed every frame. They now come from `pass_view_locked`.
- Those views are forgotten (and destroyed deferred) in both `defer_destroy_image` overloads,
  so none outlives its image, including the image-heap path render targets use.
- The motion, depth-copy, reactive and opaque images and FSR 4.1.1's output were already
  persistent (resized only with the scene).

**No stalls.** Nothing on the hot path waits. The timers read only retired submissions. FSR
4.1.1 waits only if 8 of its frames are in flight (never in practice).

**Timing.** `host/pass_timer.cpp` puts timestamp pairs in a 64-slot ring around the motion
pass, reactive mask, opaque snapshot, upscaler dispatch and output copy. It feeds the overlay
(`scene: ...`) and a `pass-timer:` log line every 10 s. Test session part 5 is a 1080p -> 4K
table built from it.

**Test build.**
- `tools/package_linux.sh` changes:
  - The tarball is named for `git describe`.
  - It adds `tools/fsr4cap` and `build-fsr4.sh`, plus the testers' docs.
  - A guard refuses any DLL, eboot, PKG, param.sfo, keys or fsr4_411.
  - It writes a `.sha256`.
- The FSR 4.1.1 helpers were prebuilt on the box (MinGW installed for it):
  - `fsr4cap.exe` (MinGW, static);
  - dxil-spirv at its pinned commit, with the class-bindings patch and rpath `$ORIGIN`, so
    testers need only Proton, python3 and spirv-tools.
- The plugins in the package are unsigned (there is no release key here). Testers keep their
  own signed `plugins/`.

## Test build hardening: Vulkan validation on lavapipe (2026-10-18)

The Khronos validation layer (vulkan-validationlayers 1.4.309, synchronization validation on,
errors + warnings + perf) was run over everything that runs without the game, on lavapipe
(llvmpipe, LLVM 19.1.7). `tools/vvl_check.sh` repeats it.

| Run | What it covers | Result |
|---|---|---|
| fsr3_upscaler_test, recycle patch ON | FSR 3.1 contexts at every preset (720p, 4K), frames, a reset | 4 SYNC-HAZARD-WRITE-AFTER-WRITE, FSR-Vulkan's (below) |
| fsr3_upscaler_test, `-DHELIAS_FSR_VULKAN_PATCHES=OFF` | the same, unpatched | the same 4: not the patch's |
| motion_shader_test | the MV shader | was 20 VUID-vkDestroyDevice-device-05137 (the test leaked); fixed, clean |
| fsr411_fallback_test (new) | FSR 4.1.1 init failures: no assets, not SPIR-V, no initializer.bin, an unknown resource, a bad size; each retried, then destroyed | clean |
| gcn_sampling_test | sampling | clean |
| `heliasdream --stream-selftest` | device creation with our feature chain, the recorder, deferred destroys, and now the kept pass views on 16/32 px images | 10 SYNC-HAZARD-WRITE-AFTER-WRITE on the selftest's own back-to-back fills (upstream test code); pass |
| `HELIAS_FSR4_FEATURES=1 Helia's Dream --stream-selftest=2` | the FSR 4.1.1 feature check on a device without them | clean; "the GPU lacks VK_VALVE_shader_mixed_float_dot_product: FSR 4.1.1 is FSR 3.1" |
| `heliasdream --setup` under Xvfb, closed with WM_DELETE_WINDOW, SDL's default renderer and `SDL_RENDER_DRIVER=vulkan` | the setup window open/close | clean ("setup: closed") |
| all 48 ctest tests with the layer on | | pass |

**Fixed (ours):**
- motion_shader_test destroyed the device with its images, memory, buffer, command buffer,
  views, pipeline and pools still alive. It now tears down in order.

**Added (tests only, no feature):**
- `tests/fsr411_fallback_test.cpp`: each failure Record() can take before Helia's Dream falls back
  to FSR 3.1, on a plain device, each one twice (as a resize asks again), then destroyed.
  Its stand-in pass is a 4-line compute shader; no FSR 4.1.1 assets are used or shipped.
- The stream selftest's temporary images now take a kept view (`pass_view_locked`): the same
  view while the image lives, forgotten by `defer_destroy_image`, at two sizes, as
  resolution changes would. A missing or different view fails the run. That covers the
  create/destroy/resize cycle, and the layer saw no view outliving its image.

**Reviewed, no change needed:**
- Every image `pass_view_locked` sees (render targets, temporal_inputs' images, the depth
  copy) is destroyed through `defer_destroy_image`. The direct `vkDestroyImage` calls in
  render.cpp and temporal_inputs.cpp are only on failed creation, before any view exists.
  So a recycled handle can't pick up a stale view.

**Left alone (third party, not caused by our patch):**
- FSR-Vulkan bridge, `FFX_GPU_JOB_CLEAR_FLOAT` (bridge.cpp ~1289) clears FSR's internal
  resources back to back with no barrier between them. One hazard is on a 1-mip image and
  three are on a 7-mip one (the luminance/SPD chain). The test's own images were checked by
  handle and aren't involved. The hazards are identical with the patch off. Clears to the
  same value are harmless in practice; it's reported, not patched.
- The stream selftest (upstream) fills the same buffer twice with no barrier, on purpose:
  it checks the recorder's ordering, not the GPU's.

**Not runnable here:**
- The scene split, opaque snapshot, reactive mask pass and the in-renderer use of the kept
  views run only inside the game's frame (smoke.sh needs the eboot). On the user's hardware
  the same layer can be turned on with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
  (or heliasdream's own validation option) for one session.

**Rebuilt test tarball** (the selftest is in the binary). The first one, -56-ge8a71d4, is kept beside it:
`/workspace/heliasdream-vk-builds/test1/heliasdream-vk-linux-v0.2.18-vk.0-60-g3f98fb0.tar.gz`, sha256
`8b523a95a41ade1b9e868bba77c78e39ea491e79e1470d556d47569274fd0b0d`. It has no proprietary files and
no missing libraries, and it reports `heliasdream v0.2.18-vk.0-60-g3f98fb0`. Only this DEVLOG note
comes after it.

## 2026-10-18 (Sun), package-all CI fix (branch `phase-5-test-release`)

PR #6's `package-all` check failed (run 39109940887): `tools/package_steamdeck.sh` looked for a
`heliasdream-linux-*` folder in the Linux tarball, found none (it is `heliasdream-vk-linux-*` since
e8a71d4), and copied the Deck files to `/`. The publish step and `check_release_bundle.sh` had
the same old name, so the bundle check would have skipped the Linux and Steam Deck bundles
(the live-server and https defaults that online play relies on).

**Fixed:** the Steam Deck kit, the playtest kit, the bundle check, the publish step (it now
reads the Linux tarball's own name instead of rebuilding it from the Windows zip's revision)
and `announce_release.py` take either prefix. The kit is named `heliasdream-vk-steamdeck-<rev>`.
An empty match now stops with a message instead of copying to `/`.

**Checked here:** the publish step's Linux half, run on the test tarball
(-60-g3f98fb0): the Deck kit builds, the bare `heliasdream` and the plugins extract, and the
bundle check passes on 2 bundles (live server over https, no playtest kit). The Windows half
runs in CI only.

## test1 hardware result and the test2 fixes (2026-10-18)

The user's PC: RX 9070 class on RADV, a 4K monitor, `v0.2.18-vk.0-60-g3f98fb0`. Launch, menus,
online, 120 fps and FSR 1 were fine. FSR 3.1 Quality at 3840x2160 showed the scene only in about
the right quarter of the screen, with the rest dark. The HUD and menus were right.

**1. FSR output size (fixed).**
- Live resolution keeps the display buffers at the largest Resolution entry (5120x2160). The
  FSR pass took its output size from the scale pass target's extent, so the context and
  dispatch were 2560x1440 -> 5120x2160 (2.00x by 1.50x) instead of -> 3840x2160. The copy back
  was 5120 wide as well.
- Now `temporal::fsr3_output_rect` (fsr3_policy.h) decides the output: the Resolution
  setting's size, placed at the scale pass's viewport when that viewport is the same size.
  Never the target's extent.
- FSR 3.1's context, FSR 4.1.1's output and both copies back use that rect, and so does the
  jitter phase count (it was computed for a 5120 output).
- `fsr3: output WxH at X,Y in the scale pass's ... target` is logged when the rect changes,
  with a warning if it isn't the Resolution size or the viewport disagrees. The first frame
  also logs the input images and the regions read from them.
- A size change makes a new context, as before.
- `test_fsr3_output_rect` in temporal_test covers a 5120x2160 buffer with a 3840x2160 output.
- The scene inputs were already right: colour, depth copy, motion and reactive are read at
  the render size from (0,0).

**2. History reset every dispatch (fixed in part, needs hardware).**
- `3000 dispatches, 3000 resets` and `3000 cuts, 0.0 scene blocks a frame`: no 864-byte scene
  constant buffer was ever seen. So there was no scene camera and every frame was a cut
  (NoCamera). FSR therefore never accumulated, and the camera motion vectors were zero.
- The "camera cut" lines every 601 flips were only the log throttle (one line per 600 flips)
  over a reset every frame. They now say how many resets came in between.
- The scene constants are now also accepted from any vertex-stage buffer of 768 bytes or more,
  V# or pointer, that carries their signature: far plane and its reciprocal, plus the scene
  or output size. The full parse still decides.
- The log says where they were found. If 600 frames still pass with no camera, it says that
  FSR then runs without temporal accumulation.

**3. Fence waits.**
- None of our passes waits on the host. The timers read only retired queries, FSR 3.1
  retires by serial, and FSR 4.1.1 is the only one with a wait (only with 8 of its frames in
  flight; not used here).
- The FSR 1 baseline ran at 2560x1440 while the FSR 3.1 session ran post-processing and UI at
  3840x2160, so the two aren't comparable. The higher guest GPU waits (about 2.1 s per 300
  flips) fit a heavier 4K post-processing frame plus the oversized 5120 FSR output and copy.
- Check again after the size fix, against FSR 1 at 3840x2160.

**Also.** The setup window's token row no longer says "only while the releases are private".

## test2 hardware result and the test3 fixes (2026-10-18)

test2 (`-67-g221d653`), FSR 3.1 Quality at 3840x2160:
- The left ~75% of the screen showed a dark, smeared scene, magnified 4/3 across.
- The right ~25% was a flat light-grey band. The HUD was right.
- The log said: output 3840x2160 at 0,0 in a 5120x2160 target, 3000 resets in 3000 dispatches,
  and no scene camera. About 88 fps, with the GPU at 100%.

A second run at 5120x2160 (`t2-5k.log`) looked right. That run had FSR 1 selected: the scale
pass was drawn bilinear, and there are no `fsr3:` lines.

**1. The picture: YEBIS built at the display buffers' size (fixed).**
- The split's Initialize site (0x26c3af3, Kyo's `jmp`) makes the graphics manager's scene
  take the output entry's DefRenderTarget for its colour, whatever its size. YEBIS then takes
  that target for its back buffer and builds its chain at that size.
- Kyo's patches assume display buffers of the output's size. Live resolution makes them the
  largest size a change can reach. So at 3840x2160, YEBIS ran at 5120x2160: the log's
  post-processing targets are 2560x1080, 1568x441, 3136x882 and so on.
- The picture was then read across 5120 texels onto 3840 pixels. That gives the 4/3 stretch,
  and the grey band from x = 3840*3840/5120 = 2880, the target's untouched texels past 3840.
- This is the same failure that live resolution's both-sides test fixed for the unsplit
  scene (its comment describes it). The split had brought it back, for the bilinear path
  too, since that is also a split. 5120x2160 matched the buffers, so it was right.
- FSR's own copy was correct. The game's draw that FSR replaces writes 0..3840 as well.
- Fix:
  - `scene_split::display_size_at_start`: a split at the start makes the display buffers
    the output's size.
  - `display_fits_split`: `scene_split_wanted` makes no split while they differ. It logs
    `scene-split: not split at WxH - the display buffers are ...`, and the scene renders at
    the output size until the next start.
  - So a Resolution change while the game runs to a size below the buffers still works,
    without the upscaler. A larger one already applied only at the next start.
  - `HELIAS_SPLIT_DISPLAY=max` keeps the old sizing.
  - Tests are in scene_split_test.

**2. The scene camera (broadened, needs hardware).**
- test2's search found nothing: `scene blocks seen 0`.
- It now follows bbport (vk_rasterizer.cpp / vk_camera_motion.cpp):
  - Any stage (PS too), any V# or pointer holding a whole block.
  - The signature is far and 1/far at [0],[1] and a size of at least 64 at [4],[5], no
    longer the render or output size.
  - After 60 frames without a camera, it also searches inside the G-buffer draws' buffers
    (4+ colour targets and a depth; first 4 KiB, 16-byte steps).
  - Cameras of another size are only taken from G-buffer draws.
- Rejections are logged. The 600-frame line now counts the buffers looked at, the G-buffer
  draws, and the most colour targets seen with a depth.
- `HELIAS_MOTION_PROBE=1` logs each G-buffer draw's buffers once, with their first floats.
- Tests are in temporal_test (`test_signature`).

**3. Jitter.** It never depended on the camera. test2's "jitter 1 phases" came from the first
frame, which was drawn before the phases were set (later lines say 18 phases), and the line
now says so. But with no camera, FSR resets every frame and the jitter is on screen as
flicker. So it pauses after 60 frames without a camera (`jitter: paused ...`).
`HELIAS_JITTER_NO_CAMERA=1` keeps it on.

**4. gpu-wait.**
- The `sceGnmSubmitAndFlip` line's `gpu-wait` is the time the host blocked on submission
  fences: `retire_slot_locked`, when all 16 command-buffer slots are in flight, plus
  explicit `wait_submission_locked` waits. A high value means the GPU is the bottleneck.
- It is about 1500 ms per 300 flips in the 5120x2160 FSR 1 run, which has no FSR 3.1 at all,
  and about 1900 ms in test2. The FSR 1 baseline ran at 2560x1440 (about 5 ms).
- So it follows the output's pixel count, not the FSR path. Per frame our passes measured
  MV 0.02, reactive about 0.1, FSR 3.1 0.8-0.95 and copy 0.15-0.25 ms. They wait for nothing
  on the host: the timer reads retired queries without WAIT, and FSR 3.1 retires by serial.
  FSR-Vulkan waits for the device only when an FSR 4 v0.7 context is destroyed.
- test2 also ran YEBIS at 5120x2160 for a 3840x2160 picture, 78% more post-processing pixels
  than needed. Fix 1 removes that.

**5. The FSR stage dump.** `HELIAS_FSR_DUMP=1` (or `[debug] fsr_dump = true`) writes these
PNGs into the capture folder's `fsr-<date>-<time>-flip<n>/`:
- scene colour, depth copy, motion, reactive, opaque colour;
- the upscaler's output;
- the scale pass's target before and after the copy.

It also writes `info.txt`: sizes, rect, jitter, reset, and per image the min/mean/max
luminance with separate means left and right of x = 3/4. It runs when `fsr-dump.request` or
`capture.request` (`bbtest mark`) appears, on Shift+F12 with `HELIAS_VK_CAPTURE=1`, and once
at the 600th upscaled frame. The pixel code is `temporal/dump_pixels.h`, tested in
`test_dump_pixels`.

## test3 hardware result and the test4 changes (2026-10-18)

test3 (v0.2.18-vk.0-73-g1700b9a): the 3840x2160 FSR 3.1 Quality picture is correct (the user,
and the stage dump: the output fills the target evenly). Next: performance at 4K output, and a
camera that holds.

### Where the frame time goes (world, same spot; the busy meter's `gpu busy:` lines)
| run | output (scene) | fps | GPU busy | GPU ms/frame | gpu-wait /window |
|---|---|---|---|---|---|
| t1 FSR 1 Native AA | 2560x1440 (2560x1440) | 120 (capped, limiter sleeps 300/300) | 76% | 6.3 | 5 ms |
| t3 FSR 3.1 Quality | 3840x2160 (2560x1440) | ~104-113 | 90-91.5% | 8.4 | 1.1-1.55 s |
| t3 Upscaler Off | 3840x2160 (3840x2160) | ~88-92 | 91-93% | 10.1 | 1.7-2.1 s |
| t2 FSR 1 Quality | 5120x2160 (3408x1440) | ~99 | 92-93.5% | 9.4 | 1.45-1.65 s |

- MangoHud per 250-frame segment in t3: gameplay at 98-100% GPU load, ~300 W, 3.0 GHz. The
  57% and 72.7% session averages include the title and menus (~20% at 120 fps).
- `gpu-wait` is the command processor inside `retire_slot_locked`'s vkWaitForFences for the
  slot it reuses (16 in flight). It runs ahead of the GPU and waits for it. The present
  thread agrees: `display wait` is 2.5-3.2 s of each 2.7-3.4 s (the acquire waits on the GPU),
  against 27 ms in t1.
- The same scene size (2560x1440) costs 6.3 ms in t1 and 8.4 ms in t3. Of the 2.1 ms more,
  1.78 ms is ours (`pass-timer:`: MV 0.06, reactive 0.21, opaque 0.04, FSR 3.1 1.23, copy
  0.23). About 0.3 ms is the post-processing and UI at 3840x2160 instead of 2560x1440. The
  rest of the 120 fps budget (8.33 ms) is lost to the ~9% of time between command buffers
  (~0.8 ms a frame over ~11 submissions).
- The CPU side (translation, recording) is not the limit: cpu-ms 6.1-7.4 s per 2.6-2.8 s
  window (2.4-2.8 cores), and the CP spends 1.1-1.55 s of each window waiting for the GPU.
  The recorder replays 0.35-0.43 s and sleeps ~1700 times a window. vkEndCommandBuffer +
  vkBeginCommandBuffer cost 2.9-3.5 us x ~3150 = ~10 ms a window. Scaleform's aligned
  scratch copies: 940k in the session, 62 MiB, about 70 small copies a frame, which is
  negligible. What the GPU runs for the translation layer is inside the busy time: per
  recorder window ~27k barriers, ~57-66k copies and ~100-114k fills (about 90, 200 and 350
  a frame). HELIAS_GPU_PROFILE=2 (test4 run E) charges that to recording sites.
### Camera (intermittent in test3)
- `motion: scene constants found at 0x14364e8c0+0 (PS 8a6c7657, binding 1, V# of 864 bytes)`
  first at flip ~2499. Then gaps of 2-433 frames, 1582 resets in the first 3000 dispatches
  and 92-600 later. The flip 8864 dump was inside a found stretch, so the dump and the
  `jitter: paused` lines were both right, at different times.
- The probe: the G-buffer draws' V#s mostly print `V# of 0 bytes` (no record count). The
  hook computed records x stride = 0, so a scene block there was never looked at (the
  probe's zeros were this too: nothing was read). One shared V# (0x1434e0040: VS binding 3 /
  PS binding 1 in every G-buffer pipeline) is the likely block.
- Fix: a V# with 0 records is taken at the shader's extent. A block head ([0] far, [1]
  1/far, [4], [5] a size: `scene_constants_head`) is read on to 864 bytes when
  `hle_kernel_va_mapped` says that memory is there. The jitter and the history both follow
  the same per-frame "camera found" state, as in test3.

### FSR 3.1 writes the target directly
The scale pass's target (RGBA16F, the display buffers' size) is made with STORAGE when the
scene is split and FSR 3.1/4.1.1 is chosen (`fsr3_target_storage_wanted`, policy
`direct_target_wanted`). FSR 3.1 imports it as its output (`Fsr3Frame::output`), and the
copy is skipped (`direct_output_ok`: from 0,0, the FSR output's size, and no stage dump that
frame). A second scale pass in a frame copies from that target. RGBA16F's view list is
itself alone, so no view in another format gets STORAGE. FSR 4.1.1 and FSR 1 still copy.
`HELIAS_FSR_DIRECT_TARGET=0` restores the copy.

### What can still be cut (needs test4's A/B runs)
- Submissions: `HELIAS_SUBMIT_EVERY_JOB=0` merges a job's submit with the next one when more
  are queued (upstream's switch). This could recover part of the ~0.8 ms of gaps.
- Reactive mask 0.21 ms and sharpening (RCAS, inside FSR's 1.23 ms): settings, run D.
- Barriers, copies and fills between the game's draws: measure with HELIAS_GPU_PROFILE=2
  first. Any change there is correctness-sensitive (upstream renderer).
- FSR 3.1's own 1.23 ms at 2560x1440 -> 3840x2160 is AMD's cost on this GPU.

## test4 hardware result and the test5 changes (2026-10-18)

- **Run A (FSR 3.1 Quality, 3840x2160):** the camera fix works: 3000 dispatches with 0-2
  resets, 3000 written directly. MangoHud 117.7 fps (from ~113), gpu busy ~91%. The user
  saw "strange shimmering on character", with FSR 3.1 only.
- **Run C (`HELIAS_SUBMIT_EVERY_JOB=0`):** 64 fps, 1% low 14.5, flashing effects.
  Rejected. The default stays a submit per job.
- **FSR 4.1.1 (Quality, FP8 t2160_m0):** works. The user says it looks cleaner. 101.6 fps,
  FSR 4.1.1 2.11 ms plus a 0.26 ms copy (`pass-timer:`).

### The shimmer on characters (FSR 3.1)
- The `motion:` summaries' velocity packing scale was `1 1` in the first two windows and
  `8.88889 -5` in the third. The real one is the 160x90 target's size / 18, y negative:
  it decodes the 8-bit texel to +-144 render pixels. With (1, 1) the decode is
  (rg - 0.5) x (-2560, 1440): a still character's 128/255 is a 5-pixel motion, and moving
  ones hundreds. Every object the game draws velocity for (characters, enemies: 32-36 draws
  a frame) got wrong motion in those windows. The 0-record V# fix (test4) lets the scale
  be read from more buffers, and some of them hold (1, 1).
- Even the right scale has limits. The target is 160x90 RGBA8: a texel is 16x16 render
  pixels and a step ~1.1 pixels, and 128/255 is not 0.5 (a still object reads 0.56 pixels
  of motion). FSR 3.1 trusts the motion vectors and shows such errors as shimmer and
  ghosting. FSR 4.1.1's network is far more tolerant, which fits "FSR 3.1 only" without
  clearing the vectors. The other FSR 3.1-only input is the reactive mask (FSR 4.1.1 takes
  none). TESTING test5 A has the one-switch-at-a-time order.
- Changes:
  - `velocity_scale_plausible`: both sides >= 1.5, the scene's aspect within 10%.
    Otherwise the last good scale is kept, the read is counted and logged.
  - `motion.comp`: object motion only where the 2x2 texel neighbourhood is all object
    (FSR's depth dilation carries it onto the edges), and only where it differs from the
    camera's by more than `HELIAS_OBJECT_MOTION_MIN` (1.5 px).
  - The view's dim blue marks the rest.
- IFreemz replays the velocity draws at render size into RGBA16F, the exact fix (Phase 2's
  deferred item). Worth doing if test5 still shows object-motion errors.

### FSR 4.1.1
- The replay writes its output only (its history is internal: `history`, `recurrent`,
  `reprojected`), so the target is as safe a destination as for FSR 3.1:
  `Fsr4Params::direct_image/direct_view`, the same `direct_output_ok` rule. It saves the
  0.26 ms copy.
- The replay's per-frame CPU work: push descriptors per pass and a constant ring of 8
  frames (`kFramesInFlight`, so `wait_submission_locked` is never reached at ~1.5 frames of
  lead). The barriers between its passes are real dependencies. FP8 runs natively on the
  9070 XT; fp8emu is only taken with `BB_FSR411_VARIANT=fp8emu`. Performance at 3840x2160
  uses t2160_m0 with a 1920x1080 scene (m1 only for Ultra Performance).
- With sharpening on, RCAS adds a pass and a 3840x2160 intermediate. At sharpness 0 it is
  skipped.

## test5 and t6 hardware results: where FSR 4.1.1 Performance's frame goes (2026-10-18)

FPS here is frames shown (the presenter's 300-frame lines after the first in-game frame,
which MangoHud's on-screen counter agrees with), not the pass-timer's count of upscaled
frames. test4's "119" was that count: the game made ~119 frames a second and 112 were shown.
test5's MangoHud CSVs stopped after 30 s (the user's MangoHud.conf `log_duration=30`).

- **test5 FSR 3.1 Quality:** 110.2 fps over 101 s in game, 107-108 settled (the user's
  mark: 108). gpu busy 91%. The velocity scale is 8.88889 -5 in every window (test4: 1 1
  in two of three), 2 resets (the context, the first in-game frame), FSR 3.1 1.20-1.25 ms,
  2999 of 3000 frames written directly.
- **test5 FSR 4.1.1 Performance:** 115.7 fps over 132 s: 120 (the cap) looking up, 108-111
  looking down. GPU work 7.33 ms a frame at 460 draws, 8.2 at 766 draws; our passes the same
  in both (~2.3 ms). Direct write on, FSR 4.1.1 2.05-2.12 ms. No shimmer on clothes.
- **t6 native 1920x1080 (GPU_PROFILE=3):** 120.0 fps, the frame-rate mode's cap (not more,
  so "well above 120" is the GPU's headroom: busy 69.4%, 5.8 ms of GPU work a frame with
  the profiler).
- **t6 FSR 4.1.1 Performance (GPU_PROFILE=3, FSR4_PROFILE=1):** 111.5 fps looking forward,
  120 at the ground (the other way round from test5: it is the view, not the direction).
  Busy 93.4%. Without the profiler (the "noaa" run, which still had anti-aliasing on: its
  options say `anti_alias = "On"` before and after): 112.7-113.3, busy 93.3%, so 8.85 ms
  a frame of which 8.26 GPU work and 0.59 idle.

The frame looking forward (ms, GPU_PROFILE=3 passes per frame; native from its run):

| | native 1080p | FSR 4.1.1 Perf 4K |
|---|---|---|
| YEBIS DoF composite d3ca03f3+111fce32 | < 0.11 (1080p) | 0.436 (4K, 2 fragments a pixel) |
| motion-blur pass 7d668276+e0305cef x2 (blur off: patched, still drawn) | < 0.11 | 0.325 |
| YEBIS 7d668276+fbe7c4b6 | < 0.11 | 0.306 |
| dc609f83+da1faca9 (one full-screen pass) | 0.151 | 0.341 |
| 0b0acf50+f7229091 x6 (the chain after it) | 0.160 | 0.240 |
| post-processing at the output, total | ~0.6 | 1.65 |
| FSR 4.1.1: prepass / network / postpass / RCAS | - | 0.485 / 0.79 / 0.50 / 0.315 = 2.08 |
| motion vectors, reactive, opaque | - | 0.03 / 0.07 / 0.02 |
| present | 0.06 | 0.07 |
| sum of the profiled game passes | 4.34 | 5.39 |
| idle between submits | (the cap) | 0.59 |

Scaleform's UI is not in the top passes in either run. The game's own passes cost 1.05 ms
more at FSR 4.1.1 Performance than at native 1080p, and the 4K post chain accounts for all
of it (1.65 at 4K against ~0.6 at 1080p). For 120 looking forward 0.52 ms is needed (8.85 to
8.33).

Ranked by measured ms (looking forward):
1. **The YEBIS post chain at the render size, FSR after it:** ~1.0 ms (0.7 without the
   motion-blur pass, which stays as it is). This is not a single commit. Today the scale
   pass (ccbf44a6) is where the scene meets the output size, and YEBIS's whole chain is
   built on the output entry's DefRenderTarget (scene_split_sites.h, 0x26c3af3 and the
   viewport site 0x25d818d). Moving the boundary needs three things. YEBIS's chain at the
   render size (both sites at the render size). A host upscale at YEBIS's last pass, before
   Scaleform's first draw (the boundary the presenter's Scaleform work already finds). And
   FSR 4.1.1 then gets the post-processed, tone-mapped picture: AMD's guidance is to upscale
   before post, so the DoF and bloom it then sees need a visual check on hardware. Next
   step: a diagnostic run with the viewport site alone at the render size, to see whether
   YEBIS's passes follow the viewport or their targets' size.
2. **Sharpening 0 for FSR 4.1.1:** 0.315 ms (RCAS plus the 4K image it reads). A setting:
   test6 run B. A cheaper RCAS cannot go much below ~0.2 ms (it reads and writes a 4K
   RGBA16F image), so this is the setting's cost rather than an inefficiency.
3. **Idle gaps:** 0.59 ms; needs `HELIAS_GPU_PROFILE=2` for where.
   `HELIAS_SUBMIT_EVERY_JOB=0` stays rejected.
4. **No reactive mask or opaque snapshot for FSR 4.1.1:** 0.09-0.11 ms. In test6.
5. **The game's anti-aliasing:** not measured yet (t6's run had it on); test6 run C.

2 + 4 make 0.42 of the 0.52 ms; 1 alone covers it.

## test7: towards a steady 120 with FSR 4.1.1 Performance (2026-10-18)

The goal: FSR 4.1.1 Performance (1080p -> 4K), sharpness 0.5-0.6, game AA off, never under
120 fps, the spawn lamp and fog included. The worst case seen so far is the lamp in view
at sharpness 0.5 with AA off: 115-117 fps (8.6 ms a frame). To hold 120 with margin, the
lamp view's GPU work should be about 7.5 ms or less, against roughly 8.0 now (8.6 ms minus
~0.6 ms of idle).

What test7 changes, and what each is worth:

| Item | Expected | How it is measured |
|---|---|---|
| Our tiled RCAS instead of the replay's | -0.1 to -0.15 ms (0.32-0.35 -> ~0.2; a 4K RGBA16F read+write is ~133 MB, a ~0.2 ms floor) | pass timer `sharpen`, FSR 4.1.1 total |
| AA Auto (off with FSR 4.1.1) | -0.35 to -0.45 ms against AA on (t7: 112.5 -> 118.5 fps) | already measured; now the default |
| Idle gaps (~0.59 ms/frame) | none until the data says which kind; HELIAS_SUBMIT_SPLIT targets "late" gaps | `gpu idle:` line |
| Lamp flicker at sharpness 0.5 | RCAS on `c/(1+max c)` limits HDR highlights instead of ringing | eyes, B in the test7 checks |

That is not the 7.5 ms target yet. The next step depends on the t8-lamp-p3 profile, which
shows which pass grows with the lamp and fog. The 4K post passes measured in t6: DoF
composite 0.44, motion blur 0.63 (not ours to change), 0b0acf50+f7229091 x6 0.24 (glow).

Spike sources looked at in the t7 log, over 224 s in game:
- **Presenter drops:** 11 in game (122 -> 133). That is not the cause of the lows.
- **Frame times:** the spread is wide even at an average of 120: 6-10 ms, ±0.66 ms, with
  half the frames over 8.42 ms. 1% low 98.5, 0.1% low 87.4.
- **Hitches:** 19 frames over 12 ms (max 24.6).
- **Pipelines:** 206 first binds of pipelines in game, all from the cache ("pipeline in
  0 ms").

So the 1% lows are mostly frame pacing jitter around a GPU-bound 8.3-8.6 ms, not
hitches. A frame time with GPU headroom (the 7.5 ms target) is what narrows it. bbtest's
new frame statistics track it.

Deferred, with the reason:
- **Post-processing at 1080p, or a half-resolution DoF/glow blur:** this needs the lamp
  profile to pick the pass, and a hardware A/B. The game's YEBIS chain is built on the
  output target (sites 0x26c3af3 and 0x25d818d), so this is a redesign, not a toggle.
- **Batching queued jobs in bb-submit** (one kernel submission for jobs already waiting):
  this only helps "queued" gaps. Do it once `gpu idle:` shows they dominate.
- **The game's frame limiter pacing (the 6-10 ms spread):** this needs a pacing trace on
  hardware.
- **Pipeline and descriptor churn:** no hitch in t7 points at them. Watch the "over 12 ms"
  count.

## test7c: submit batching, and the plan for post-processing at render size (2026-10-18)

test7b fixed the haze (the sharpened output is now decoded as the replay's RCAS decodes it).
At the lamp, AMD's sharpening ran at 116.6 fps, and ours should be about the same or a little
faster. Holding 120 there still needs about 0.4 ms.

**Where the idle time goes (t8, `gpu_gaps.py --idle`):**
- Queued gaps total ~56 ms/s over ~1180 gaps/s (~0.46 ms a frame at ~10 submissions a frame):
  - <25 us: 7.4 ms/s;
  - 25-250 us: 24.6 ms/s;
  - >250 us: 23.6 ms/s.
- The small and middle gaps (~0.27 ms a frame) are what one kernel submission per job costs:
  the scheduler, the fence, the cache flush and wait at the submission's end.
- The gaps over 250 us between jobs that were already queued are not that. They are more
  likely the semaphore waits of the jobs that carry one.

**HELIAS_SUBMIT_BATCH=1 (bb-submit):**
- **Policy:** take a game job (no foreign id, no semaphore to signal). If the GPU still has
  two of our submissions unfinished (one running and one queued behind it), hold the job,
  waiting 0.1 ms at a time on the oldest, for at most 2 ms. Then take the game jobs queued
  behind it that have no semaphore to wait on: up to 12 jobs or 32 command buffers.
- **Submit:** one VkSubmitInfo, with the first job's wait and the last job's fence. RADV
  chains the command buffers of one VkSubmitInfo into one kernel job.
- **Why it cannot starve the GPU:** a held job could not have started earlier anyway, with
  two submissions ahead of it.
- **Why it does not repeat EVERY_JOB=0's failure:** labels and CPU-visible sync are not held
  back. The command processor still ends every job and hands it over at once. Only bb-submit
  waits, and its published "submitted" serial covers the merged jobs.
- **Fences:** the merged jobs' slots never had their own fences submitted. `g_fence_owner`
  names the covering serial. It is reset at hand-over and set before `g_stream_submitted` is
  published. `slot_fence()` gives the slot's own fence, the covering slot's fence, or none
  when that slot has already retired (slots retire in order). Every slot-fence wait and
  status check now goes through it:
  - the retire;
  - `wait_submission_locked`;
  - `host_gpu_submissions_completed`;
  - the two in glitch.cpp.
- **Pruning:** `g_completed_atomic` lets bb-submit drop a finished entry without reading a
  fence the CP may already have reset and reused.
- **Default: off.** It changes fence ownership and adds a wait on the submit thread. The box
  can only run it on lavapipe, so it needs the hardware A/B (TESTING, test7c) before it can
  become the default.
- **Expected gain:** 0.15-0.3 ms a frame, if the <250 us queued gaps mostly go away. That is
  most of the ~0.4 ms, but not certain to be all of it. RADV still flushes caches at each
  command buffer's end inside a submission.
- **On the box (lavapipe):**
  - `--stream-selftest=20` with it on, twice: 0 differences. It merged 212 and 215 jobs into
    82 and 86 submissions, with ~130 holds per run.
  - With `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`: the same 10 SYNC-HAZARD
    WRITE_AFTER_WRITE reports (the self-test's own back-to-back vkCmdFillBuffer) with it on
    and off. Batching adds none.

**b57af00c+b9bb200e (the Cathedral fog, 0.86 ms where it draws):**
- It is not a post pass. It draws ~7.0M vertices, the same order as the scene passes
  3deaa573 and b10c4f55, at render size (5.4M fragments, about 2.6x 1080p of overdraw).
- So moving post-processing to render size does not touch it. A cheaper variant means
  changing a game shader: fewer layers, or a cheaper fragment program.
- That needs the program disassembled and an A/B in the fog area, so it is not something to
  contain in a toggle this round. Deferred until the t9 profile in the Cathedral shows
  whether it is fragment-bound (it would then be worth a reduced-quality patch) or
  vertex-bound.

**Motion blur while it is off (e0305cef, 2 draws a frame, ~0.29-0.33 ms, 4K):**
- The patch makes the blur compare always false. Each pixel then still reads the velocity
  and the centre sample and writes it through.
- Skipping both draws gives the same picture only if they ping-pong back into the image the
  next pass reads, or if their output can be aliased to their input.
- Copying instead saves almost nothing at 4K RGBA16F: ~133 MB, about the cost of the draw.
- The log did not say which images the draws use (the YEBIS texture log stopped at 24
  lines). test7c logs its targets and textures: `render: motion blur ... draws into`.
- The skip (`HELIAS_MOTION_BLUR_SKIP`, test8) will be written against that data. It will be
  verified by bit-comparing presented-frame captures with the skip on and off before it
  becomes the default.

### Next: post-processing at render size, before FSR 4.1.1 (design)

**Today's order:**
1. Scene at 1920x1080.
2. The game's scale pass ccbf44a6, which FSR 3.1/4.1.1 replaces, at 3840x2160.
3. The YEBIS chain on the 4K output (sites 0x26c3af3, 0x25d818d).
4. UI.

**The 4K passes in t8 (per frame):**

| Pass | ms | Draws | Fragments |
|---|---|---|---|
| d3ca03f3+111fce32 (DoF composite) | 0.42 | 1 | 16.6M (2x 4K) |
| 8747a367+d92266b9 (glare/bloom composite) | 0.39-0.44 | 1 | 6.2-7.1M |
| 7d668276+fbe7c4b6 (colour/LUT pass, LUT 2048x2) | 0.30 | | 8.3M |
| 7d668276+e0305cef (motion blur) | 0.29 | 2 | 8.3M |
| b28f5a56+765450df | 0.26 | | 10-14M |
| 7e956bc3+e80526cc (small: the glare chain's downsamples) | 0.22 | 15 | |
| 0b0acf50 (glow) | ~0.22 | | |

About 1.9-2.1 ms in all, against the 7.5 ms target.

**Target order, as AMD's guidance and the user's research have it:**
1. Scene.
2. At 1920x1080: DoF, bloom/glare/lens flares, motion blur (SSR and SSAO are already at
   render size, in the scene).
3. FSR 4.1.1 to 4K.
4. At 4K: the sharpening, anything grain- or aberration-like the chain has, and the UI.

**Mechanism (host side, `HELIAS_POST_AT_RENDER=1`, off by default):**
1. At ccbf44a6, do not upscale. Hand the YEBIS chain the 1080p scene colour under the 4K
   target's address: a render-size alias of the output RtImage.
2. While the chain runs, its 4K targets are made at render size. Their viewports and scissors
   are scaled by render/output, and so are the constants that hold pixel sizes. Most YEBIS
   constants are UV-normalised: e0305cef's tap offsets, and the threshold at 1/1080. These
   are logged per pass (`yebis:` lines) and checked one by one.
3. Run FSR 4.1.1 where the chain's last output meets the first UI draw. That point is already
   classified (motion_classify.h has the Scaleform UI VS hashes). FSR writes the real 4K
   image, and the UI draws on it.

**FSR inputs:**
- **Colour:** post-DoF, post-bloom and post-motion-blur colour, which the user accepts.
- **Depth, motion and jitter:** unchanged. They are at render size and match.
- **Format:** if the colour/LUT pass fbe7c4b6 runs before the last post pass, FSR gets
  display-referred colour. It is then given as non-HDR input, with exposure 1. That is our
  upscaler's input mode, not the game's HDR setting, which stays untouched.
- **Reactive mask:** motion-blurred and DoF-blurred areas should get a reactive value so FSR
  does not smear them with history. The mask is built from the same CoC/velocity terms (DoF
  blend factor t, |v|).
- **Motion blur on:** it runs at 1080p on 1080p velocity, which is its native space (the
  1/1080 threshold). So it should look as intended, and the reactive mask keeps FSR from
  ghosting it.
- **Jitter:** with post at render size, the jitter is in the image the post passes blur.
  DoF and motion blur are filters, so they carry it through. Bloom thresholds may shimmer
  slightly.

**Expected saving:** the fragment-bound 4K passes drop to about a quarter. That is ~1.2-1.5 ms
of the ~2 ms, minus fixed costs (the downsample chain is already small). It is more than the
remaining gap to 7.5 ms on its own.

**Risks:**
- The chain's targets are allocated by the game at output size. Every alias and rescale has
  to be exact, or edges and borders break. A mismatched constant shows as a half-size or
  offset effect. The answer is one pass at a time, behind the toggle, with captures.
- FSR sees blurred and bloomed colour. Bokeh edges, bright bloom halos and motion-blur
  streaks can ghost or flicker, and the reactive mask is the mitigation. Some upscaler
  documentation prefers DoF and motion blur at output size for quality, so the A/B decides.
- The glare is at 1080p and the composite is upscaled. Thin lens-flare streaks lose
  sharpness at 4K.
- Getting it wrong for one frame (cuts, menus, the Dream) falls back to today's order.
  The decision is per frame, with history reset on a switch.

**Order of work:**
1. A pass-order log of one frame (targets, sizes, formats), from a test build.
2. Motion-blur skip while off.
3. Then DoF, then glare/bloom, each behind the toggle with a capture comparison.

## t9 on hardware: submit batching rejected (2026-10-18)

- **Batch off** (t9-lamp): 116.9 fps, 1% low 99.6, 0.1% 88.5, 8.55 ± 0.57 ms, 2 frames over
  12 ms.
- **Batch on** (t9-lamp-batch): 116.8 fps, 1% low 77.1, 0.1% 71.4, 8.56 ± 2.29 ms, 521 over
  12 ms, max 41 ms.
- **What batching did:** it merged 1049 jobs/s into 214 submits/s, but held 227 jobs/s for
  495 ms/s. The time to vkQueueSubmit rose to 1810 us.
- **Gaps:** queued gaps fell only from 66.5 to 53.3 ms/s, while gaps before the present rose
  from 8.3 to 22.1 ms/s.
- **Why:** holding a job until the GPU had fewer than two submissions left kept bb-submit
  waiting on fences for half of each second. Jobs the presenter's frame depended on queued
  behind the hold, so frame pacing broke. The queued gaps are mostly not a per-submission
  cost, which is what the plan assumed.
- `HELIAS_SUBMIT_BATCH` stays in the code, off. No more time is going into it.

## test8: the motion blur skip, and the data for post-processing at render size

**Motion blur's targets (t9 log):** e0305cef draws into 0x169e00000, then 0x161e60000, both
3840x2160 RGBA16F (format 97), every frame. That is the ping-pong the skip needs. The skip's
policy (`temporal/mblur_skip.h`, `tests/mblur_skip_test.cpp`) proves it at run time:
- **Learning:** a frame counts only if:
  - its two draws are exactly A->B then B->A;
  - neither draw blends;
  - the colour input is found as the one sampled render target of the target's size and
    format;
  - nothing else sampled, wrote, copied or cleared B. The hooks are prefetch_stage_images
    (draws and dispatches), rt_written, render_copy_target_locked and clear_image_locked.
- **Armed** after 120 frames like that: draw 1 is held back and draw 2 is checked against
  the pattern. Any other work in between, or a mismatch, does draw 1 after all as a copy of
  A into B, which is what the patched shader writes, and disarms.
- **Why it is exact:** with motion blur off, the shader patch makes every pixel take the
  unblurred path, so the pair is an identity copy, and B is a scratch target nobody else
  reads.
- **Motion blur on:** the draws are not eligible, so nothing is skipped and the blur is
  unchanged.
- **Expected saving:** ~0.29-0.33 ms a frame.
- **Not covered:** a CPU read of B's memory. There is no such path for render targets in
  YEBIS's chain, and the off/on capture comparison in TESTING checks it.
- **Default on,** with `HELIAS_MOTION_BLUR_SKIP=0` as the fallback, and
  `HELIAS_AB_SECONDS=30` for the A/B in one run.

**Post-processing at render size: not in test8, and why.**
- **What the design needs** (test7c section), one pass at a time:
  - which draws are the DoF, glare and colour passes;
  - the order of their targets;
  - which constants are in pixels;
  - whether any compute dispatch sits in the chain.
- **What we have:** none of that is in any log yet. The F12 dump and the YEBIS texture lines
  stop after 24 entries.
- **The risk of building it blind:** half-size or offset effects, or a broken frame, in the
  user's only run. Defaulting it on that way is not something the box can verify, because
  the box cannot run the game.
- **So test8 collects that data in the same run:** `HELIAS_POST_CHAIN_LOG` (on, one frame
  every 3600 flips) logs each draw from the scale pass to the first UI draw, with its
  target, size, format, viewport, blending and the images it samples.
- **Next build:** with those lines, test9 can implement `HELIAS_POST_AT_RENDER` (DoF, then
  glare/bloom, then the colour/LUT pass, each with its own switch), with no extra trip to
  the hardware for data.

## t10 / test9: the motion blur skip fixed; post-processing at render size planned, not built

**t10 (test8, Cathedral lamp, FSR 4.1.1 Performance):**
- 116.1 fps, 1% low 101.3, 8.62 ± 0.53 ms; 306 of 324 s under 120.
- The skip never fired (0 pairs), for two reasons:
  - the draws are S->B, B->D, not A->B->A;
  - binding B as draw 1's target marked it written before the draw was known to be the
    motion blur's.
- test9 handles both:
  - S->B->D becomes one copy S->D;
  - a bind of B is judged at the draw's prefetch.
- **Expected gain:** two 4K draws (~0.29 ms) become one 4K copy (~0.12-0.15 ms). That is
  ~0.15 ms, about +1-2 fps at 116. It is not enough for 120 on its own.

**The post chain at the lamp (t10 `post-chain:`, 54 draws):**

| # | Pass | What it does |
|---|---|---|
| 0 | scale pass (FSR) | 1080p scene -> A 0x161e60000 (4K RGBA16F) |
| 1 | 7d668276+fbe7c4b6 | onto A at 4K (depth, last frame's 1256x1060 bloom, LUT) |
| 2-3 | 1baf9590 / 7e956bc3 | A -> 2352x882 DoF buffers |
| 4 | DoF composite d3ca03f3+111fce32 | A + DoF -> C 0x165e30000 (4K) |
| 5 | 4c37ae6d+bf368417 | onto C at 4K |
| 6 | | the motion blur's 1080p velocity |
| 7-8 | motion blur | C -> B -> A (4K) |
| 9-41 | bloom pyramid | from A: 1080p, then 628x530 down to 10x9 (B10G11R11), composited to 1256x1060 |
| 42 | 1ea5a07e+ccb08339 | A + bloom + exposure -> 0x1533b0000 4K BGRA8 (the tonemap; LDR from here) |
| 43-53 | 0b0acf50 chain | a 16x16 noise texture into the final image (grain/dither), and a 1080p/4K blur chain blended into it |
| 54 | | the first UI draw |

**Design for `HELIAS_POST_AT_RENDER` (test10):**
- Run #1-#9 at render size, then FSR 4.1.1 just before #42, on A' (still linear HDR).
- Bloom (#9 on) then starts from a 1080p A. The tonemap (#42), the grain (#43) and the blur
  chain stay at 4K after FSR. FSR's input stays HDR, so no HDR or exposure mode changes.

**What it takes:**
1. Render-size shadow images for A, C and B.
2. Viewport and scissor scaling for draws into them (the game sets them as scale/offset:
   `vp -1080x1080+1920+1920`).
3. Sampling those bases through the shadows inside the window.
4. FSR's colour input moved from #0 to #42, with the reactive mask from #4's DoF factor
   and #6's velocity.
5. A whole-frame fallback: today's order whenever the frame's chain signature (pipelines
   and targets #0-#9) differs from the learned one, decided at #0, before anything is
   redirected.
6. Every pixel-size constant in #1, #2, #4 and #5 found and scaled. These are known only for
   e0305cef, where they are UV-normalised.

**Why it is not in test9:**
- Item 6, and whether the translated shaders read the T#'s size, can only be checked in the
  game. One wrong constant is a shifted or half-size DoF or fog in the user's only run.
- A per-frame self-check can catch a changed chain, but not a wrong picture.

**Expected gain when done:**
- #1 0.30 + #4 0.42 + #5 ~0.2 + #2/#9 reading 4K ~0.1 ms, at about a quarter of the cost:
  ~0.7-0.8 ms saved.
- The motion blur is then already at 1080p: another ~0.1 over the test9 copy.
- In all ~0.8-0.9 ms. From 8.6 ms that is ~7.7-7.8 ms, about 128-130 fps average. It is near
  the 7.5 ms target, not certainly under it.

**What makes the next build safe:** one in-game step that dumps those passes' constants and
shader size queries (`HELIAS_POST_CHAIN_LOG` extended to their C# and the translator's
`image_get_resinfo` use). That is cheap to add, and it can ride along with any run.

## test10: the post chain before the tonemap at render size (2026-10-18)

**t11 (test9):** the skip worked, +~1 fps (0.15 ms); 152 of 220 s under 120.
- The "constants" t11 logged for #1-#10 are the user-data tables (descriptor words), not the
  passes' constant values. The pixel-size question stays open. Each moved pass is judged by
  its inputs instead.

**What moves.** Between the scale pass and the first other program, these draw at render size:

| Pass | Program | Risk |
|---|---|---|
| #1 colour/fog | fbe7c4b6 | medium: reads depth at 1080p, last frame's bloom, a LUT; a pixel offset would shift the fog |
| #2-#3 DoF downsample and blur | 265a6d2d, e80526cc | low: their targets keep their own 2352x882 size; #2 now reads 1080p instead of 4K, so a 4K-texel kernel samples a little wider |
| #4 DoF composite | 111fce32 | medium: a blend by depth/CoC; offsets would show as DoF edge halos |
| #5 onto the DoF result | bf368417 | medium: reads depth and the DoF buffer |
| #6 velocity | d3c8bb21 | none: already 1080p |
| #7-#8 motion blur, or the skip's one copy | e0305cef | low: UV taps; velocity in 1080p units (the 1/1080 threshold) |

**Mechanism:**
- At the scale pass, A's stand-in gets the scene by a 1080p copy (~0.03 ms). The scale pass
  draws nothing.
- Draws of the moved programs into an output-size RGBA16F target are retargeted to its
  stand-in, with the viewport scale/offset and the scissor scaled by render/output.
- Reads of targets that have a stand-in, through an RGBA16F T#, bind the stand-in's view.
- The window closes at the first draw of any other program. The other things that close it:
  - a dispatch, copy or clear touching a stand-in's target;
  - the next frame.
- When it closes, FSR runs with the last stand-in as its colour input
  (`fsr3_scale_pass_locked(..., color)`) and writes the real 4K target. Depth, motion,
  jitter, reactive and exposure are unchanged. The input stays linear HDR.
- An upscaler failure there copies the stand-in in once and turns the mode off for the
  session.
- **Not done:** an extra reactive mask over DoF and blur. FSR's own reactive mask from the
  opaque snapshot is still used. DoF bokeh ghosting is a watch item.

**Frame-time consistency** (the lights and fog flicker with uneven frame times):
- Nothing new is made per frame. The three stand-ins are made once, on the first moved
  frame or a resolution change, and the FSR context does not change.
- The A/B flip only changes where FSR runs, from the next frame, with no allocation.
- No CPU waits are added. The only extra GPU work is the 1080p copy.

**Expected** (honest, from t8/t10 per-pass costs at 4K):
- #1 0.30, #4 0.42, #5 ~0.15-0.2, the skip's copy 0.13 and #2's 4K read ~0.05: ~1.0-1.1 ms
  today. About a quarter of that at 1080p, so ~0.75 ms saved, minus the 0.03 copy.
- **Frame time:** 8.6 -> ~7.9 ms. Uncapped average ~125-127 fps, so the 120 cap holds most
  of the time.
- **Frames over 8.33 ms:** from ~60% (t10: 22715 of 37690) to perhaps 10-20%.
- **1% low:** about 101 -> 108-114. The remaining lows are pacing jitter and the lamp and
  fog spikes, not the average.
- That is not the 7.5 ms target yet. The next candidates are #42-#53 at 4K: the tonemap
  composite and the 0b0acf50 blur chain.

## test11: even present pacing; t12 read (2026-10-18)

**t12 (test10, `HELIAS_AB_SECONDS=30` flipping post at render), from heliasdream.log:**

| | post on | post off |
|---|---|---|
| fps | 120.0 (126.5 in the first half, which included the load) | 117.8-120.0 |
| GPU busy | 84-88% (~7.0-7.3 ms a frame) | 92-93% |
| GPU idle "late" (waiting on the CPU) | 80-120 ms/s | 0 |
| game's gpu-wait | ~5 ms per 2.5 s | 1.15 s per 2.5 s: GPU-bound, the game waits |
| flip to present | 0.9 ms (max 1.2-2) | 3-4.7 ms (max 7) |

- Post at render took the game off the GPU's back. Still, 39% of the on halves' frames
  were over 8.42 ms. With ~1 ms of GPU to spare, that spread is the present call's timing,
  not the GPU's work:
  - The limiter keeps the game's flips even (its sleeps are <60 µs late at p99).
  - Each present follows by the command processor's work on that frame plus the hand-over.
- "Over 8.33 ms" is about half of any run averaging exactly 120: the cap's interval is
  8.333. bbtest now also counts frames over 8.5 and 9 ms. Those are the ones that show.
- **The 13:50:42 "dip leaving and returning to the lamp room":** it is the 4th off half
  (flips 26.7k-30.3k): 115.7-118.5 fps, GPU 92% busy, the game waiting 1.2 s per 2.5 s.
  That is test9's cost, in a heavier view.
  - The chain's end point moved at flips 19006/19007 (e9986d45+88414cd6, then
    84b3a182+3c2b7974) and back at 23187 (bb92a5b6+483f5dc1). That followed the view: the
    draws per 2.5 s fell 161k -> 91k, a lighter area whose next program differs.
  - No stand-in was made then (only three, at flip 2058). Those halves' max was 12.9 and
    13.7 ms, with 1-2 frames over 12 ms. No stall from the chain change.
- **Motion blur skip:** "not eligible" in every on half. Its inputs were compared with the
  stand-in's 1080p size, which the 1080p velocity buffer also has, so it found two inputs.
  Fixed: inputs are judged against the real targets' size.

**Present pacing (host/temporal/present_pacer.h, tests/present_pacer_test.cpp):**
- An even grid at the game's pace, on the presenter thread after the blit's submit, with no
  lock held:
  - An early frame sleeps, then spins to its slot.
  - A late one goes at once and restarts the grid (one long interval, no short one after).
  - The grid creeps earlier, at most 20 µs a frame, until the earliest arrival is ~150 µs
    ahead.
- The unit test, with arrivals spread over 0.6 ms: 0 of 2700 intervals more than 25 µs off,
  301 µs average wait.
- GPU-bound frames all arrive late and pass straight through, so it can't make a frame
  later than the last present plus a period.

**Not done (c):**
- **Tonemap #42 and the 0b0acf50 chain at render size:**
  - FSR would have to take the tonemapped LDR picture, with a context made for HDR.
  - The grain/dither (16x16 noise) would be upscaled and temporally smeared.
  - The 0b0acf50 chain is also the HUD and menu blur.
- **The bloom pyramid's input from the 1080p stand-in:** it is pre-FSR and jittered. The
  bloom would shimmer with the jitter, which is the flicker this work is trying to remove.

**Expected (test11 vs test10, post at render on):**
- fps stays at 120. The GPU gets ~0.15 ms more headroom from the skip.
- Frames over 8.5 ms: t12 did not count them (39% were over 8.42). Expected under 2-5% paced, if the spread is the present timing as read.
- **1% low:** ~102 -> 112-118 in the paced halves. The rest are the game's own hitches
  (loads, streaming) and GPU spikes past 8.33 ms in heavy views (~84-93% busy).

## t13 (test11) and v0.2.18-vk.1 (2026-10-18)

**t13-pace-ab** (`HELIAS_AB_SECONDS=30` flipping the present pacing; post at render and the
blur skip on throughout):

| Half | on, 1st (load) | on, steady (5 halves) | off (6 halves) |
|---|---|---|---|
| fps | 117.4 | 119.8-120.0 | 119.9-120.0 |
| 1% low | 67.1 | 119.4-120.0 | 88.5-100.1 |
| 0.1% low | 25.3 | 87.9-110.5 | 75.4-90.1 |
| Frames over 8.5 ms | 80 | 15-32 | 931-1354 |
| Frames over 9 ms | 70 | 4-20 | 212-547 |
| Frames over 12 ms | 50 | 0-3 | 1-17 |
| Pacing's average wait | 3966 µs | 2725-4343 µs | - |

- The user marked "good frame time" at 14:10:35.
- The grid sits ~3 ms after the typical arrival. Each late frame restarts it on that late
  arrival, and the creep (20 µs a frame, only when a 120-frame window's earliest arrival
  is >150 µs ahead) rarely brings it back.
- So the pacing costs ~3 ms of latency, in exchange for this frame-time consistency. A
  faster creep could trade some of it back. That is left for after the release, measured.
- The first half's roughness was the load. vk.1 doesn't pace while a loading screen
  shows, nor for 2 s after (`hle_video_loading()`).

**Today's run, in short:**
- **t10 (test8):** the blur skip never fired; the post-chain was logged.
- **t11 (test9):** the skip worked, about +1 fps.
- **t12 (test10):** post at render gave 120 held with ~1 ms GPU headroom. The skip was
  ineligible under it (fixed in test11). 39% of frames were over 8.42 ms, from the present
  timing.
- **t13 (test11):** pacing, results above.

**vk.1:**
- Two option rows, `present_pace` and `post_at_render` (both On), shown in F10 and the
  launcher.
- `post_mode()` and `pace_mode()` are read at the first draw or present, after
  host_options_load. The environment variables win.
- The graphics defaults were already the game's own (each effect's default index is On),
  and host_options_load only reads, so an existing file isn't rewritten.
- **Motion blur ON under post at render:** both draws run at 1080p, on the stand-ins of C,
  B and A.
  - The 1080p velocity (d3c8bb21) has always been 1080p, so the blur's taps, in that
    buffer's units, match the 1080p target.
  - The skip stays out (blur on is not eligible), as it should.
  - test9/t11 looked fine with blur on at 4K. vk.1's defaults check looks at it under post
    at render.

## vk.1-rc2: frame-latency limit, wider post at render, snapshots, profile mode (2026-10-19)

From vk1-dream's analysis, approved as one build, with two steering changes: no switches
for optimizations, and compile tracking in the snapshots.

- **Drops (vk1-dream: ~500).**
  - `host_present` replaced a frame waiting in the mailbox (`g_present_next`) when the
    command processor (CP) handed in the next one.
  - Now, while the presenter has not taken the waiting frame, the CP waits for it on
    `g_present_taken_cv`: up to max(20 ms, 2 frames), then drops as before. This is safe
    because the CP's `done()` callback runs after `host_gpu_submit` with no locks held.
  - `hle_video_finish_flip` did the same at its queue of 4: it threw the oldest flip
    away. Now it waits up to 34 ms for `vblank_tick` to pop one (`g_vo_popped`), then
    completes the oldest normally.
  - The counts are in the 300-frame present log line ("command processor waited").
- **Pacing:** creep 100 µs a frame, window 30 frames. The unit test re-anchors 4 ms late
  and checks the wait is < 300 µs by frame 90; it is 150 µs.
- **Post at render:**
  - A draw that closed the window now continues it when it has the post chain's shape:
    1 RGBA16F target, no depth or MRT.
    - At output size, it needs at least one stand-in input and one smaller RGBA16F
      input.
    - Below output size, it needs one of the two.
  - Bloom's first pass (4c37ae6d+4b766f62) and CA (bb92a5b6+483f5dc1, 84b3a182+3c2b7974)
    are kept out by name.
  - What closed each window, by program, is counted (`g_post_end_by`) and goes into
    snapshots with the fallback reasons.
- **Sharpening in the present pass:** an RCAS 5-tap variant of the present shader, used
  only at 1:1 texel mapping. The present pass draws the HUD, so it would sharpen the HUD
  and halo it. It stays behind `HELIAS_SHARPEN_AT_PRESENT=1`, and FSR's own sharpening of
  the scene stays the default.
- **Overlay:**
  - The 256 KiB vertex buffer overflowed on the reorganized menu with long notes, and
    nothing was drawn (the F10 bug). It is now 1 MiB; on overflow it draws the whole
    triangles that fit and logs once.
  - The options' `wrap_note` caps notes at 3 lines.
- **Menu:**
  - `kPresentPace` and `kPostAtRender` are gone. Unknown keys in a saved file are
    skipped.
  - `pace_mode()` and `post_mode()` read only the environment.
  - Restart labels come from the row's `restart` flag. The upscaler and quality are
    flagged because the scene split is planned at boot.
- **Snapshots (`host/perf_capture.cpp`):**
  - A lock-free seqlock tap on `host_log_line` keeps the last 4096 lines (the fault
    handler logs, so no mutex).
  - The presenter notes each present (8192 entries).
  - The capture thread `bb-perfsnap` keeps a ring of seconds: GPU busy, and
    /proc/self/task CPU time by thread name. It also polls `<snapdir>/snap.request` every
    250 ms and writes the file via `.part` and rename.
  - The renderer publishes its post-at-render text at its next draw when asked.
- **GPU sampler (gpu.cpp):**
  - It reuses HELIAS_GPU_PROFILE=3's pass timestamps, with pools always made unless
    `HELIAS_GPU_SAMPLE=0`. It is switched only in `begin_recording_locked`, so no pair
    spans a switch.
  - It arms once 10 s have passed and the last present interval was within 1.06× the
    frame period. It runs for one flip, then collects the slots it marked (`sampled`).
  - Those go to the sample, not to `profile_ns`, and are logged as `gpu sample:`.
  - The pass timer runs for that frame too, for FSR's ms.
- **Compiles:**
  - `perf::note_compile` is called at:
    - graphics pipeline builds (render.cpp);
    - compute builds, where an optimizer rebuild on `own_cache` counts as a worker;
    - compute precompile waits, which count as waited;
    - translations, through a hook in translation_cache.h so the unit test needs no
      perf code.
  - "Waited" means it ran on the CP and not on a precompile worker
    (`perf::set_worker_thread`).
  - Startup logs whether the caches were kept or cleared for the translator stamp, and
    the totals before the first in-game frame.
- **Profile mode (`bbtest profile`):**
  - Mesa's RGP trigger is `MESA_VK_TRACE_TRIGGER`. wsi_common.c checks it with access() at
    each present, unlinks it and calls `capture_trace`.
  - The file is written by ac_rgp.c to a fixed path,
    `/tmp/<process>_<Y.m.d_H.M.S>[_frameN].rgp`, and "RGP capture saved to" goes to
    stderr.
  - Helia's Dream creates the trigger after a snapshot (10 s debounce). A helper thread waits up
    to 5 s for the driver to take it, then up to 45 s for a new /tmp/*.rgp to hold its
    size for 1 s. It moves the file next to the snapshot's name in `rgp/`, falling back to
    copy and remove across file systems.
  - `HELIAS_SHADER_STATS` enables VK_KHR_pipeline_executable_properties. Every
    library/link/LTO-relink/whole graphics pipeline and every compute pipeline then gets
    `CAPTURE_STATISTICS`, built with `VK_NULL_HANDLE` instead of heliasdream's cache (the flag
    is in RADV's cache key; Mesa's disk cache still applies). One TSV line goes out per
    executable.
  - Host passes (present, overlay, FSR) are not covered.
  - Without the variables none of this runs.
- **Headless .rgp analysis:**
  - RGP has no command-line analyzer, and the .rgp format (RDF chunks: SQTT data, code
    object database, PSO correlation, queue events, written by Mesa's ac_rgp.c and
    ac_sqtt.c) only gives timing after the SQTT tokens are decoded.
  - The box can list a capture's chunks and code objects by following ac_rgp.c. For the
    timing, open it in the RGP GUI, on the PC or in the box's desktop.
- **Verified here:**
  - 51/51 ctest (19 skipped as before: they need the game's files).
  - `--stream-selftest`, `=2` with HELIAS_FSR4_FEATURES=1 (FSR 3.1 fallback) and `=20`:
    pass, with the same 10 upstream SYNC-HAZARD lines as vk.1.
  - bbtest snap and --show on synthetic files.
  - lavapipe lacks VK_KHR_pipeline_executable_properties, so the shader-stats path logs
    that and does nothing. RGP needs RADV.
  - Neither ran on the hardware here.

---

## 2026-10-19 (Mon), vk.1-rc3: pacing, presenter submit, occlusion resolve, walks

The second build of the night. Rule: optimizations are permanent (no new switches);
`HELIAS_*` variables only for debugging.

### Done
- **Latency limit, uncapped path:** `PresentFlip::from_cp` (video.cpp) marks flips the
  command processor recorded itself. `host_present` waits on them as on the capped ones.
  The capped path's queued copy has `from_cp = false`.
- **Pacer (`temporal/present_pacer.h`):**
  - Fed `t_pre` (just before `vkQueuePresentKHR`).
  - A window of 30 judges on the 3rd-earliest early time (`nth_element`).
  - The creep is dropped only if more than a quarter of the window arrived late.
  - While the last present was on time, an arrival more than 1 ms early pulls the grid in
    by `min(early - 1 ms, 0.5 ms)`.
  - `present_pacer_test` has new cases: "late-per-window" and "pull".
  - window.cpp splits it into `pace_slot` / `pace_until` / `pace_presented`, so the pacer
    wait overlaps `host_gpu_wait_submitted_until`.
- **Presenter priority submit (gpu.cpp):**
  - `host_gpu_stream_mark()` is the recorder's activity counter plus `g.flushes`, taken
    after `host_gpu_lock`.
  - If it is unchanged when the blit is recorded, nothing was appended to the stream in
    between. The presenter waits on `g_sub_idle` until `g_stream_submitted >= need`
    (2 s cap), then submits on `g.queue` under `queue_mu`.
  - Otherwise it uses the stream's kForeignSubmit op, as in rc2.
  - Rejected: a separate present queue. The blit reads the renderer's images, and so would
    need cross-queue semaphores on every game submission.
- **Occlusion resolve (`shaders/occlusion.comp`):**
  - A 64-lane workgroup: each lane sums its chunk into LDS, lane 0 runs the ordered scan,
    then each lane writes its chunk's dumps.
  - The host flags a slot serial when a dump address repeats or overlaps (16-byte steps
    under 128 B). In that slot, lane 0 does everything, because a later dump may not be
    ready and the earlier write must stay.
  - Abandoned: skipping a repeated block's earlier write. It is wrong when the later dump
    is not ready.
  - Pair stats run only with flags bit 1 (`HELIAS_OCCLUSION_STATS=1`).
  - The barrier after it covers transfer, compute, host and conditional rendering.
  - Timestamps around the end-of-submission resolve are read back at the slot's next
    recording.
  - Checked against the rc2 shader on lavapipe (a standalone harness on the box): 400 random
    cases, 168 of them serial, 0 mismatches.
- **MUBUF bound path (translate.cpp):** `indexed_buffer_load()` takes
  `buffer_load_format_*` (ops 0-3) with idxen:
  - it reads 4 words clamped by `OpArrayLength`;
  - it decodes with `format_decode_dynamic` (split out of `format_load_dynamic`);
  - it declines lds/tfe and offen, which keep the walk;
  - new test: `gcn_mubuf_bound_test`.
  - render.cpp counts the pipelines whose VS still walks, the post-chain ones separately,
    in the [walks] snapshot section.
- **Stage manifest urgent ordering (render.cpp):** a graphics pipeline built on the
  command processor marks its stages urgent. Save writes them first and adds a
  `.urgent` sidecar. Load stable-partitions them to the front.
  - Shipping a manifest entry for 52d756df+2e86d610 was rejected, because the manifest
    holds game shader words.
  - Note that the domain VS of host-tessellated draws is not noted in the manifest
    (`!vs_in.tess_window`), so this may only help part of that hitch.
- **Idle with work queued (item 7):** not fixed. Candidates that can't be told apart on the
  box:
  - per-submission kernel/firmware overhead;
  - end-of-IB cache flushes for the userptr memory;
  - userptr revalidation (MMU notifier, NUMA balancing).
  - Implicit sync is unlikely: same process, same entity.
  - Added: a per-serial flag (`g_sub_occl`) for "this submission ended with an occlusion
    resolve". The busy meter splits queued gaps into after-resolve, after-present and
    other: `gpu idle:` lines, snapshot [gpu_idle], `gpu_gaps.py --idle`.
  - If the after-resolve share dominates, the narrowed barrier or the resolve's position
    is the next thing to move. If it is spread evenly, it is per-submission overhead, and
    the answer is fewer submissions.
- **Not done:**
  - Bloom pass 1 at render size. CA (84b3a182+3c2b7974) runs every frame and bloom #10
    reads CA's 4K output, so moving it would feed it the pre-CA, jittered image.
  - FSR on async compute: not confident in the cross-queue hand-off.

### Verified here
- 52/52 ctest (19 skipped as before: they need the game's files).
- `--stream-selftest`, `=2` with HELIAS_FSR4_FEATURES=1 and `=20`: pass, with the same 10
  upstream SYNC-HAZARD lines.
- None of it ran on the hardware here.

---

## 2026-10-20 (Tue), async-fsr: FSR 4.1.1 on an async compute queue, not built

This was asked for (rc2 ANALYSIS item 8, deferred in rc3). The design review found it
cannot give overlap in heliasdream-vk's frame, and would add two cross-queue hand-offs to the
frame's critical path. So no code was written. Nothing on this branch changes behaviour.

### Where FSR runs
- FSR 4.1.1 does not run in the presenter after the game's frame. It runs inside the
  game's frame, recorded into the renderer's stream on `g.queue`:
  `post_end_window_locked` (render.cpp) → `fsr3_scale_pass_locked` (fsr3_pass.cpp) →
  `fsr4_record_locked` (fsr4_pass.cpp), at the end of the post-at-render window.
- Its output (written directly into the scale pass's 4K target A) is read at once by the
  game's draws that stay at output size, all on graphics, in the same flip: CA
  (84b3a182 / bb92a5b6, which closes the window), the bloom's downsample (4c37ae6d+4b766f62),
  the bloom, the tonemap 1ea5a07e+ccb08339, the grain and the UI. The presenter only
  blits or draws that finished display buffer.
- RGP `snap-222230` (8.71 ms frame) agrees. FSR is the CS block at about 4.1-6.3 ms, and
  about 2.4 ms of pixel-shader work follows it in the same frame: the rest of submission #5
  (3.56-7.39 ms) and submissions #6-#13. All of it reads FSR's picture.
- Every FSR 4.1.1 pass needs the post chain's colour: SPD exposure and the prepass read
  `r_input_color`. So no part of FSR can start before the window closes.

### Why async compute gives no overlap here
- On one in-order graphics queue, the next frame's depth and shadow passes are submitted
  after this frame's post-FSR draws. Those draws have to wait for FSR. If FSR moves to a
  compute queue, the graphics queue waits on its semaphore for the whole of FSR, then the
  next frame starts, as it does today. RADV exposes one graphics queue, so the next frame
  cannot go on a second one.
- Overlapping FSR(N) with frame N+1 would mean submitting N+1's first passes before N's
  post-FSR draws, which reorders the game's command stream. Frame N's end-of-pipe labels
  and fences tell the guest when it may reuse N's memory: render targets, constants,
  index/vertex data, and userptr pages the CPU writes. Labels written before N's
  deferred draws would let the guest overwrite what those draws still read. Holding the
  labels back blocks the guest CPU instead, and adds a frame of latency under the
  frame-latency limit and the pacer. This is a redesign of the frame, not a queue change.
- Cost if it were built anyway: the graphics submission split before FSR (an
  end-of-IB flush, where today there is a barrier), a compute submission, and a graphics
  submission that waits on it. amdgpu resolves a cross-ring dependency on the CPU (the
  DRM scheduler waits for the finished fence, then pushes the job), so there would be two
  bubbles per frame, about 10-50 µs each, plus the extra submission overhead measured in
  t8 (about 27 µs per submission). Estimate: +0.05 to +0.15 ms of GPU per frame, with no
  concurrency to pay it back. FSR's own passes already run at 75-100% wave occupancy
  in the RGP capture, so even real overlap would buy little.
- Motion vectors on compute, overlapped with the render-size post chain: this is the one
  real overlap. The motion pass is 0.03 ms (t6), and the reactive and opaque inputs are
  not used by FSR 4.1.1. The extra submission split costs about as much as it saves.

### Where the headroom is instead (for the next round, from the same data)
- The 4K tail after FSR is about 2.4 ms, and includes ~20 latency-bound tiny passes at the
  end of the frame (0.6 ms under 15% occupancy).
- The occlusion resolve's bubble. rc3 made it parallel; it needs a new capture.
- The 25-250 µs queued gaps per submission (rc3's [gpu_idle] split says which kind).
- FSR 4.1.1 itself: check the snapshot says the FP8 variant on the 9070 XT.

---

## 2026-10-20 (Tue), vk.1-rc4 ("build 3"): queued idle, fetch inline, soffset, pass stats

The third build of the night, from the rc3 session (`20261020-000251-vk1-rc3`; Old
Yharnam GPU-bound: span 7.82 ms, `idle_with_queued` 7.6 s over 150,789 gaps, 1.83 s of it
after the present). Same rule as before: permanent, `HELIAS_*=0` only for debugging.

### 1. GPU idle with work queued
What it is, from the driver sources (Mesa 26.2 `radv_query.c`, `radv_cmd_buffer.c`,
`radv_queue.c`, `radv_cs.c`, winsys `radv_amdgpu_cs.c`, `wsi_common.c`), and what is
inferred:
- **Proven (source):** `vkCmdWriteTimestamp2` sets the command buffer's
  `active_query_flush_bits` (PS/CS partial flush, INV_L2, INV_VCACHE, CB/DB flush+inv).
  The next `vkCmdResetQueryPool`, or `vkEndCommandBuffer`, emits them. On gfx12, INV_L2 is
  GL2_INV|GL2_WB, a release event plus a PWS wait. Our command buffers began with
  the busy meter's reset+TOP timestamp, then the coverage, occlusion and profiler resets.
  So each began with a full drain and an L2 write-back/invalidate.
  **Fix:** every reset before the first timestamp (`begin_recording`).
  `HELIAS_RESET_ORDER=0` restores the old order. One L2 flush fewer per command buffer
  (~9-11 a frame). The end-of-command-buffer flush stays: it is the last timestamp's,
  and the HOST barrier needs it anyway.
- **Proven (source):** a `vkQueueSubmit` with no command buffers on RADV is CPU-only
  (`radv_amdgpu_cs_submit_zero`: syncobj transfers, no GPU job). Its fence takes the
  queue's last submission's.
- **Inferred, not measurable on the box:** amdgpu keeps `sched_hw_submission` (2) jobs on
  the ring, and the scheduler pushes the next one only after a CPU round trip (fence IRQ →
  scheduler worker). A job shorter than that round trip lets the GPU run dry with work
  queued. The present blit is such a job: ~42 µs on the GPU, followed by a ~138 µs gap
  (28% of the queued idle). The <25 µs gaps (~7 µs each, the same as RGP's) are the
  per-boundary flush plus the kernel's cost.
- **Fix (blit):** the blit rides the flip's last game submission when bb-submit has not
  taken it yet. `submit_job_now` appends the blit command buffer and its `rendered`
  semaphore to the game job whose serial + 1 is the flip's `need`. The presenter then
  submits an empty `vkQueueSubmit` for its own fence.
  - This is not batching (t9 rejected that). No job is held, and no two game jobs are
    merged. The blit could not start before that job anyway.
  - Only blits that wait on no semaphore ride (acquire-by-fence, the default).
  - If the rider is not taken by the time the job has gone in, it is revoked and the
    blit goes alone, as in rc3.
  - Counters `rode`, `alone` and `missed` are in [present] `blit_ride:`.
  - `HELIAS_BLIT_RIDE=0` turns it off.
- **Attribution for the rest (63%, ~9 boundaries):**
  - The meter now keys each queued gap by the site that ended the command buffer before
    it (as the late gaps already were): the `gpu idle queued after:` line, top 10.
  - It also buckets each gap by how long that command buffer ran (<50 µs, 50-200 µs,
    200 µs-1 ms, longer): the same line and [gpu_idle] `idle_by_span_before:`.
  - `gpu_gaps.py --idle` names the sites.
  - If the next session shows the gaps mostly after short command buffers, it is the
    scheduler round trip above. The answer then is fewer short jobs: what ends them is
    named by site.
  - An optional A/B on the PC (TESTING, rc4) confirms it: the kernel parameter
    `amdgpu.sched_hw_submission=4`. Nothing in Helia's Dream sets it.
- **Expected:** −0.1-0.2 ms per GPU-bound frame from the blit (most of the 0.17 ms
  after-present share) and a few µs per command buffer from the reset order. The rest
  depends on what the attribution shows.

### 2. Fetch shader inlined (translate.cpp `run()`)
- The new straight-line path runs when all of these hold:
  - `opt.fetch` is set and `force_dispatcher` is not;
  - the main program has no branch, no `s_setpc`, exactly one `s_swappc`, and
    `s_endpgm` only at its end;
  - the fetch program has no branch, no `s_swappc` and no `s_endpgm`, and exactly one
    `s_setpc`, as its last instruction.
- Emission:
  - `ordered = true`;
  - the main instructions are translated in order;
  - at the `s_swappc`, its destination pair gets the return address (as the call did),
    then the fetch instructions follow, translated with `kFetchBias`, up to the
    `s_setpc`;
  - no blocks are made at the call or the return.
- The bound-V# check then takes the latest definition before each load, as for any
  straight-line shader. The fetch shader's `s_load_dwordx4 s[8:11]` no longer excludes the
  main program's reload of `s[8:11]`.
- `gcn_fetch_inline_test` reproduces rc3's log exactly through the forced dispatcher
  ("indexed, excluded" ×2 and "registers redefined, dispatcher order" ×2). Inline: 0
  walks, both loads bound, valid SPIR-V. A fetch shader with a branch keeps the
  dispatcher.
- Log `fetch inline:` (the first 8 pipelines). [walks] `fetch_inline:` counts
  `fetch_inlined` and `fetch_inlined_walking`; `vs_walking` should drop.
  `HELIAS_FETCH_INLINE=0` uses the dispatcher.
- `TranslateResult::fetch_inlined` and `soffset_bound` are stored in the translation
  cache (`write_result`/`read_result`, size assert 424 → 432). The translator fingerprint
  changes, so the first start rebuilds the caches.

### 3. Register soffset
- `indexed_buffer_load` takes an SGPR soffset (codes < 104). It adds `read_s(soffset) >> 2`
  to the dword index. `read_bound` reads the binding only where the offset is a whole
  number of dwords and the element's last dword is inside the binding (`OpArrayLength`).
  Otherwise it takes the walk. It is rejected under `cb_no_fallback`, which has no walk.
  `HELIAS_SOFFSET_BOUND=0` restores rc3.
- **Stores: not done, on purpose.** There is no bound store path, and adding one is not
  safe. A binding can be the draw's copy (YEBIS windows) or a device-local mirror
  (buffer_shadow), so a store into it would never reach guest memory.
  16e3fa9f+b6e87124's 28 `buffer_store_dword` at a register offset keep walking. The
  expected saving for that shader is 0. The load side helps any load "at a register
  offset" (`soffset bound:` log, [walks] `soffset_bound`).

### 4 and 5: dropped
- **CA (84b3a182 / bb92a5b6) and bloom pass 1 at render size:**
  - CA's offsets are in pixels: at 1080p before FSR, the fringe is 2× wider at the output
    and goes through FSR's temporal accumulation.
  - The rule says CA's look must not change, and nothing on the box can show it
    identical: no game files, no captures. `capture_diff_test.py` and
    `replay_compare_test` need captures from the game.
  - Bloom pass 1 must read FSR's 4K picture or it shimmers (rc2).
- **Glare 8747a367:**
  - At render size, the sprites lose sharpness and pass through the upscale: not
    identical.
  - Scissored to each quad, the picture is identical, but nothing is saved: the
    rasterizer already shades only the quad. The 186-286 µs per draw with 2-71 µs of work
    (rc2) is serialization, not shading outside the quad.
  - So no change.

### 6. Main scene pass: vertex- or pixel-bound
- No behaviour change in normal frames. The GPU sampler's frame (one every 10 s or more,
  already timed pass by pass) now also takes pipeline statistics per pass:
  - `pipelineStatisticsQuery` is enabled when the device has it;
  - `HELIAS_GPU_SAMPLE_STATS=0` turns it off.
- New snapshot section [pass_stats] for the sample's top 8 passes: `ms`, `verts_k`,
  `vs_k`, `prims_k`, `clip_in_k`, `clip_out_k`, `fs_k`, `vs_per_us`, `fs_per_us`,
  `fs_per_prim`. Log line: `gpu sample stats:`.
- How to read 4efb29f6 across samples:
  - pixel-bound: `fs_per_us` stays high and steady (thousands of fragments per µs) while
    the pass time follows `fs_k`;
  - vertex-bound: the time follows `vs_k`/`prims_k`, and `fs_per_prim` is low.
- `HELIAS_RGP_LABELS=1`: a `vkCmdInsertDebugUtilsLabelEXT` at each render pass, named by
  its first pipeline, so RGP shows the passes by name. It enables VK_EXT_debug_utils on
  the instance only then. In place, as the profiler's pairs are, so for profiling only.
- Fixed on the way:
  - `begin_recording` reset the timestamp pool and set `qreset`, so
    `profile_begin_locked` never reset the statistics pool. Its queries began unreset
    under `HELIAS_GPU_PROFILE=3`. It is reset there now.
  - A nested pair no longer ends the outer pair's query (`stat_index`).
  - Results are read with availability rather than `WAIT_BIT`.

### 7. Robustness
- **Priority wait:**
  - capped at 100 ms, not 2 s (`HELIAS_PRESENT_PRIORITY_WAIT_MS`, 5-2000);
  - past the cap, if the device is not lost, the blit goes into the stream behind its
    frame (`host_gpu_submit_presenter` under `g.mu`). The function now returns that
    ticket and window.cpp waits on it as on any stream blit;
  - waits over 50 ms are logged (`present: the blit waited`, the first 16);
  - [present] `blit_ride:` shows `cap_fallbacks`, `waits_over_50ms` and `wait_max_ms`.
- **bbtest:** the `render: target ... re-created ... (contents lost)` lines and the
  `targets re-created in another format:` summary are filtered out of `errors in the
  log`, and counted on their own line in session.txt. rc3's log: 12 such lines, and 0
  errors left.
- **dfmt 6:** already mapped everywhere (textures, render targets, `format_layout`).
  rc3's `unusable T# dfmt=6 ... no address: an unbound slot` was a T# with no base. The
  render log's "has no texture: unsupported format" for it now says
  "no address (an unbound slot)".

### Verified here (box, lavapipe)
- 53/53 ctest (19 skipped as before: they need the game's files), also with
  `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`. New: `gcn_fetch_inline_test`.
- `--stream-selftest`, `=2` with HELIAS_FSR4_FEATURES=1, and `=20`, run with
  `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and synchronization validation: pass,
  with the same 10 upstream SYNC-HAZARD lines as rc3.
  - Pre-existing, not new: the threading checker's `UNASSIGNED-Threading-MultipleThreads`
    message on a VkFence (vkGetFenceStatus on bb-submit). It shows with the rc3 binary
    too. With `HELIAS_VK_VALIDATE=1` it fails `=20` in about 2 runs of 5, for rc3 and
    rc4 alike.
- None of it ran on the hardware. The blit ride, the reset order and the inline fetch
  shader can only be judged there.

## 2026-10-20 (Tue), vk.1-rc5 ("build 4"): CPU headroom for the command-processor chain

Built overnight, pre-authorised; the plan with every item's saving, risk, effort and
evidence is docs/vk/RC5_PLAN.md (also the session's `rc5/PLAN.md`), summarised here.

### What rc4's data says (heavy Old Yharnam, `snap-20261020-010838`)
- 114.4 fps avg, 1% low 81.9; bb-cp0 880-970 ms/s, bb-record 830-940, heliasdream.bin ~650,
  bb-submit ~420. cp-backlog at its cap (10).
- The CP: `exec=2279 draw=1809 bind=1252 prefetch=228 pipeline=24` ms per 2.6 s with
  438k draws: ~5.2 us exec and ~2.9 us bind per draw. That is the CPU limit.
- bb-record: `recorder spin 1656 ms, replay 611 ms` a window: 72% spin.
- GPU idle with work queued 75 ms/s: ~50 ms/s directly after each game submission
  (b33f77 = host_gpu_submit), ~45-55 us each whatever the span before: a per-command-
  buffer boundary cost (RADV's end-of-cmdbuf flushes), ~0.5 ms/frame. Not addressed here
  (next list, #4).
- Pacing: every sleep ~55 us late (Linux's 50 us timer slack); limiter spin 0.17 ms/frame.
- blit_ride `rode 29487 alone 7322 missed 1553`: as designed (alone = the frame's
  submission had already gone; missed = a race with a 344 us vkQueueSubmit).
- The menu's "bb-cp0=2885 ms/s": TableMask (x4) and LeanBuilder (x2) workers, started
  lazily from bb-cp0, inherited its name (perf_capture sums CPU by name). Accounting only.

### Landed
1. **Recorder wait: MONITORX/MWAITX** (CPU audit M3). recorder.cpp `mwaitx_until`: monitor
   on `g_published`, re-check, `mwaitx` with the TSC timer (hints 0xF0: C0, ECX bit 1:
   timer) for the length the 4,000 PAUSEs took (best of 3 at start, at least 1,000
   ticks); then the futex as before. Gated on CPUID 8000_0001h ECX[29].
   Expected: ~0.4-0.6 core freed.
2. **Placement** (M1). core/host_clock.cpp: L3 domains from sysfs, hot = larger L3, then
   prefcore ranking / CPPC highest_perf / max frequency, then lowest CPU; only with ≥2
   domains of ≥6 CPUs; whole-domain masks. Hot: `host_thread_set_class` callers (bb-cp0,
   bb-record, bb-submit, bb-present, bb-vblank), the game's main thread (the SprjFlipper
   hook, first call) and GXRenderThread (pthread.cpp `thread_tramp`). Cold: bb-compile in
   game (re-placed per job, the whole mask while booting/loading), bb-cs-optimize,
   bb-tablemask, bb-lean. Everything else is reset to the whole mask, because a thread
   inherits its creator's. Expected: 0-0.7 ms/frame of cp0/record.
3. **Timer slack** (M2): `prctl(PR_SET_TIMERSLACK, 1)` on the same timing threads; with it,
   the limiter's margin `clamp(second + 100, 150, 2000)` (was +200, 300) and the presenter's
   spin 150 us (was 300). Expected ~0.3 ms/frame less spinning on two threads.
4. **ThinLTO** (CMakeLists.txt, `HELIAS_THINLTO`, ON with clang): `-flto=thin` for the Helia's Dream
   target, `--thinlto-jobs=all`, `-fno-lto` on the decomp sources. The 15 decomp objects
   built with and without the LTO flags have identical code (objdump). Binary 93.7 MB
   (rc4 96.3). Expected 2-8% CPU in the CP and recorder.
5. **Page tables in ReBAR VRAM** (GPU audit #3): gpu.cpp `create_table_buffer`. RADV's
   per-submission preamble invalidates L2/vector/scalar caches, so CPU writes made before
   a submission are seen exactly as with system memory; the CPU never reads them except
   the draw capture. Expected 0.02-0.10 ms/frame GPU.
6. Worker names `bb-tablemask`, `bb-lean`; C3 (the motion-blur name search per draw).
7. **[cpu]** snapshot section: placement, timer slack, recorder wait, CP cost per draw.

### Left out (reasons in PLAN.md)
GPU #1 compute constant buffers (hazard rules, needs a game test), #2/#10 (translator →
cache rebuild), #6/#7 (corruption risk without an image comparison), async FSR, submit
batching, blit ride changes, M6, M5, C1/C2 (need the profile), -march, frame pointers, PGO.
PS wave32 and params WC stay env A/Bs.

### Next build, ranked by measured cost
1. The CP's bind section (2.9 us/draw ≈ 4 ms/frame): profile with `tools/cpuprofile.sh`,
   then exact-key caching of per-stage descriptor resolution and set lookups across draws.
2. Splitting bb-cp0's draw work across threads (per pass) or parallel recording.
3. GPU #1 compute constant buffers (0.15-0.30 ms).
4. The per-command-buffer boundary (~0.5 ms/frame GPU idle).
5. PGO. 6. Tess walks (with other translator work). 7. Merging small post passes.

### Verified here (box, lavapipe; no MONITORX on this Xeon, one L3 domain)
- Clean CI build (CI_PORTABLE=1, HELIAS_RELEASE=ON, ThinLTO on): 53/53 ctest (19 skipped as
  before), also with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`.
- `--stream-selftest`, `=2` with HELIAS_FSR4_FEATURES=1, and `=20`, with the validation
  layer and synchronization validation: pass, 0 differences, the same 10 upstream
  SYNC-HAZARD lines; the known `UNASSIGNED-Threading-MultipleThreads` fence message as
  before. `=2` again with `HELIAS_TABLES_VRAM=0 HELIAS_TIMER_SLACK=0 HELIAS_AFFINITY=0`:
  pass.
- The page tables went into lavapipe's device-local host-visible heap (`page tables: in
  video memory`), so that path ran; the MWAITX path and the placement could not run here
  (PAUSE fallback, `placement: off: one L3 domain`).
- Translator fingerprint unchanged: cbb93a8ae24af3169f3cf6d437c1ac69.

## 2026-10-20 (Tue), vk.1-rc5b ("build 5"): compute constant buffers, the CP split timers, the cp0 split design

On top of rc5 (df31b8b), the same night. The three items, in the order they were asked for.
Plan with the ranking: docs/vk/RC5_PLAN.md ("rc5b").

### 1. bb-cp0's per-draw bind work (~2.9 us/draw)
Read the bind path (draw_impl from the pipeline lookup to the record) for work that can go
without changing a byte of output. Most of what such a pass usually finds is gone already:
the counters are plain stores under `g.mu` (`bump`), samplers have a 64-entry memo, sets
are cached by content, dynamic state is dirty-tracked (`g_recorded`), the lazy-barrier
hazard sets are off by default, and the table-rebuild vectors are only built on that rare
branch. What remained:
- `stage_final_views` ran `strstr(pipeline_name, "+e0305cef")` for every stage of every
  draw, for a log of 8 lines. Now only while those lines are still to come (identical
  output: the flag was already false after 8).
- No profile yet says which section of the 2.9 us dominates, and caching resolution
  across draws needs that (a wrong key is a wrong pipeline or binding). So the main
  deliverable is the measurement: **`cp_split`**. RenderCostStamp's 20 sections (split by
  HELIAS_GX_COST before, with steady_clock on every draw) now also book TSC ticks on the
  draws the phase timers sample (1 in 16): ~23 rdtsc and plain adds per sampled draw,
  ~10-20 ns a draw on average. [cpu] `cp_split:` (since the previous snapshot) and
  `cp_split_total:`; log `cp split:` every 100,000 sampled draws. The TSC is converted with
  its ratio to steady_clock over the run. `HELIAS_CP_SPLIT=0`.
- Output identity: neither change touches what is recorded. The selftests (0 differences)
  and ctest cover the recording paths; draw_impl itself does not run on the box (no
  game), so the new fields are first seen on the PC.

### 2. Splitting bb-cp0 (assessed; design only)
Not implemented: it cannot be shown correct tonight. draw_impl holds `g.mu` for all of
its ~4.1 us and reads/writes nearly every piece of renderer state: the pass, set caches,
params ring, copy versions, uploads, texture cache, the draw windows and Scaleform
snapshots (CPU copies taken at record time), and guest memory (V#s, T#s, tables) read at
the moment the CP reaches the draw. The design, for a later build:
- **Stage A (bb-cp0):** PM4, the register shadow, and per draw a *draw snapshot*: the
  registers draw_impl reads (context, SH user data, GX record pointer, indirect/index
  state; ~1-2 KB) plus anything that copies CPU memory at record time today (YEBIS
  windows, SF snapshots). Into an SPSC ring.
- **Stage B (bb-bind, new):** everything under `g.mu` for draws and dispatches, in ring
  order; it becomes the only owner of the renderer state.
- **Ordering:** labels, EOP/fence writes, WAIT_REG_MEM, flips, DMA and CPU-visible copies
  stay on cp0 but must wait until stage B has consumed everything before them (the game
  may rewrite a V#/T#/table once it sees a label). That wait is where the gain can vanish
  (YEBIS waits per pass).
- **Proof needed first:** a selftest replaying recorded PM4 streams (draws, dispatches,
  labels, flips, DMA) in place and split, comparing the packet streams byte for byte, as
  the stream selftest does for bb-record.
- Gain if it holds: cp0 keeps ~1.5 of ~5.2 us per draw; stage B ~3.5-4 us becomes the
  limit: ~25% more draws per second. Effort L (days), risk medium-high. cp_split decides
  first whether a cheaper exact-key cache in stage B's biggest section gets most of it.
- Per-pass parallel recording is not the lever: bb-record already records off cp0, and
  after rc5's MWAITX it is mostly waiting.

### 3. Compute constant buffers (GPU audit #1)
`compute_options` never set `cb_ssbo`: 41 of 42 cached CS walked every constant ("s_buffer_load
with constant buffers off"), the top kernels with 1-1.5k dependent 64-bit loads; game
compute is ~0.48 ms of RGP trace in heavy Old Yharnam. Now:
- `compute_options` sets `cb_ssbo` (`compute_cb_on()`: HELIAS_CB_SSBO not 0 and
  HELIAS_CS_CB not 0). The translator puts the bindings in the stage's set (0) from 112
  and keeps the walk as each buffer's fallback (no lean variant). The cache keys include
  the options (hash_options), so compute stages translate again once; the fingerprint is
  unchanged.
- With the push set (cb_push_on) the shared layout has no storage-buffer bindings, so the
  game's compute pipelines get `g.cs_set_layout`/`g.cs_pipe_layout` (the shared bindings
  plus 112-127). `create_compute`/`first_compute_pipeline`/the bb-cs-optimize job take the
  layout, so a stand-in and its optimized pipeline agree. The tessellation LS pass and
  every graphics pipeline keep their layouts (and their driver cache entries).
- `resolve_compute_buffers_locked` (host_gpu_dispatch, after the copy-version flush,
  before the params slot) binds a buffer only when the walk would read the same bytes:
  - resolved, non-null base, dword aligned; **not indexed** (an indexed read past the
    records differs: GCN returns zeros, the binding clamps, the walk reads memory);
  - not stored to by this dispatch (its store V#s resolved first; an unresolved or
    untraced store walks every buffer of the dispatch);
  - **not written by the GPU earlier in this command buffer**, nor the tables its V# was
    read from: `gpu_write_note_locked` collects, per `record_serial`, every range the GPU
    writes into guest memory (all of shadow_written_locked's GPU writes: compute stores,
    fills/copies, DMA, labels and fences, copy-backs, occlusion; the vertex shaders'
    stores; the tessellation ring). Ranges are compared as physical direct memory, so a
    CPU address and its GPU alias meet. A write of unknown extent, or more than 512
    ranges, makes the whole command buffer count as written;
  - no copy-back in flight over it; imported as one buffer (`locate`), the range within
    it and `maxStorageBufferRange`; base on a vec4 of its binding (the graphics
    stages' menu-flash rule: no realignment copy, no snapshot, no windows for compute).
  Everything else: the dummy buffer and the walk, as rc5.
- The binding reads the same memory the page table maps, when the dispatch runs, as the
  walk does; the end-of-dispatch barrier (SHADER_WRITE → SHADER_READ) and the pass-end
  barriers cover both alike.
- Counters: [walks] `compute_cb: on= pipelines= buffers= pipelines_without= dispatches=
  all_bound= bound= walk_no_base= walk_indexed= walk_unaligned= walk_gpu_written=
  walk_copy_version= walk_unmapped= walk_too_long=`; log `gpu: compute cb:` at start, for
  the first 24 pipelines with buffers, and every 20,000 dispatches.
- Cost on cp0: ~1-3 us a dispatch (resolves, ~17 dispatches a frame in rc4's log:
  456,118 dispatches for 72 M draws), ~0.03-0.05 ms a frame.

Checked here: `gcn_cs_cb_test` (new): s_buffer_load from a V# and s_load from a pointer
in user data bind at set 0 binding 112, not indexed, max_dw 4, valid SPIR-V; nothing bound
with cb_ssbo off. Locally only (nothing committed): the user's stage manifest's 6 compute
programs translate with and without cb_ssbo (no regressions), all valid SPIR-V, 13
buffers (3 indexed, 4 pointers), every binding in 112-127. The dispatch path itself does
not run on the box (no game): the layouts are created on lavapipe (`gpu: compute cb: …
bound as storage buffers`), the selftests pass with it on and with HELIAS_CS_CB=0.

---

## 2026-10-20 (Tue), profiling kit for the next build: debug instrumentation (on rc5b)

Goal: one guided ~30 min session on the PC (`bbprofile.sh`) whose data decides the next build's
items by measurement (rc5plain ANALYSIS §6): the GPU idle with work queued (A/Bs), the periodic
~12 ms frame (M4), where bb-cp0's ~5 µs/draw goes (DWARF profile), and PGO. Rule: debug-only,
off unless its variable is set; no default, picture or snapshot-format change.

### Checked first (at 7b65cd0)
`HELIAS_TABLES_VRAM`, `HELIAS_PARAMS_WC`, `HELIAS_PS_WAVE32` (=32 requires wave32),
`HELIAS_FRAME_STATS`, `HELIAS_FRAME_DETAIL`, `HELIAS_WAIT_LOG`, `HELIAS_WAIT_LOG_MS`,
`HELIAS_HLE_COUNT`, `HELIAS_SNAP_DIR` (+ `snap.request`), `HELIAS_VK_CAPTURE(_DIR)`
(+ `capture.request`) all exist. Missing: a params ring in VRAM, per-flip timestamps, a
dump on frames over 11 ms (main_wait logged only frames over 33 ms), and any PGO build.
Helia's Dream leaves through `_exit` on the window close, SIGTERM and the exit flip, so the profile
runtime's atexit write would never run.

### Added
- `src/host/flip_trace.{h,cpp}` (`HELIAS_FLIP_TRACE=1`, `HELIAS_FLIP_TRACE_MS`): hooks in
  `hle_gnm_submit_flip` (game thread: submit, back-pressure; the flip's done callback on the
  CP: before/after `hle_video_finish_flip`), `cp_thread` (queue 0: job taken / walked and
  submitted, so CP busy vs idle between flips), `note_main_frame_work` (the main loop's frame)
  and the presenter (matched by the flip's arrival: the newest flip whose CP end is not after
  it). The presenter reads the hot threads' `schedstat` (7 preads a present) for run and
  run-queue time per interval. A ring of 8192 flips under one mutex; ~4 short critical
  sections a flip on the game/CP threads when on, one constant load and branch when off.
  Snapshot `[flip_trace]` only when on.
- `main_wait.cpp`: `HELIAS_FRAME_SLOW_MS` (default 33 = the old constant).
- `gpu.cpp`: `HELIAS_PARAMS_VRAM=1` picks a DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT type on a
  heap over 1 GiB for the params ring (the CPU only memcpys into it: alloc/rewrite_params_slot).
  The default and `HELIAS_PARAMS_WC=1` paths choose exactly as before.
- PGO: `-DHELIAS_PGO_GENERATE=ON` (IR PGO, `-fprofile-generate`, compatible with the ThinLTO
  build) compiles `src/host/pgo_dump.cpp`: a `bb-pgo` thread writes the counters every 30 s
  through `.tmp` + rename and once more from `exit_reports()` / the window-close path
  (`pgo_dump_final`, signal-safe: a flag and nanosleep, ≤5 s). `-DHELIAS_PGO_USE=<profdata>`
  for the optimised build. The decomp sources get `-fno-profile-generate` /
  `-fno-profile-instr-use` (as they get `-fno-lto`). Not CS-IRPGO: that needs a second
  instrumented session on top of the first profile; possible later with `-fcs-profile-generate`.
- `tests/flip_trace_test.cpp`: 60 simulated flips, one 13 ms late: one slow-frame log with
  the flip before it, 60 CSV rows, the late row's interval and CP walk.

### Verified here
ctest 55/55 (19 skipped as before, no game files); `--stream-selftest` passes with and
without `HELIAS_PARAMS_VRAM=1` (lavapipe: `params ring: … in video memory the CPU maps`,
0 differences). Build warnings: none new.

### The kit itself (tools/bbprofile/)
- `bbprofile.sh`: the guided driver (steps sys, a1 plain + CPU profile, a2 TABLES_VRAM=0,
  a3 PARAMS_WC=1, a4 PARAMS_VRAM=1, m4, pgo, a5 PS_WAVE32=32, a6 plain again; ~28-30 min).
  It swaps the kit binary in as the game folder's `heliasdream.bin` (copy + rename; the original
  kept as `heliasdream.bin.bbprofile-orig`, restored and checked on exit, Ctrl-C included, and by
  the next run after a crash), launches `./heliasdream` with the folder's `heliasdream.toml` as
  run-heliasdream.sh does but logs into the session folder (run-heliasdream.sh prunes `logs/`), and
  takes the A/B windows by itself: F11 at the spot marks t=0, `snap.request` at +35 s and
  +65 s gives two back-to-back 30 s windows. perf runs during the stand; `perf sched record
  -k CLOCK_MONOTONIC` lines up with heliasdream's steady clock (`fliptrace: … at <s> monotonic`,
  `[flip_trace]` `base_steady_ns`). sysctls saved to `~/bbprofile-work/.sysctl-orig` and
  restored (the file survives a crash, so the next run restores the true originals).
- `bbprofile_analyze.py`: box side; the A/B table against a1 and the a6 drift, M4's stage
  excess (slow vs normal medians per stage and per thread run/run-queue), the perf sched
  windows around each slow frame, bb-cp0 / hot-chain folded stacks with an inline view from
  the unstripped binary (llvm-symbolizer), sysmon, `pgo-merge` (llvm-profdata 19).
- `test/fakegame.{sh,py}`: a stand-in game (named threads, snapshot/capture requests, PGO
  dumps) for dry runs here: `BBPROFILE_LAUNCH=tools/bbprofile/test/fakegame.sh
  BBPROFILE_AUTO=1 BBPROFILE_QUICK=1`. Verified here: full sessions, Ctrl-C mid-step
  (binary and sysctls restored, exit 130), `--resume`, recovery from a killed session,
  bundle + analysis + pgo-merge; the PGO binary writes its profile periodically and on a
  normal exit, merges with llvm-profdata 19, and a `-DHELIAS_PGO_USE` build of it passes the
  stream selftest. Not verifiable here: the game itself (no game files, lavapipe).

## 2026-10-20 (Tue), vk.1-rc6: the 12 ms frame, asynchronous logging, cheaper submissions, PGO

From the rc5 profiling session's plan (`REPORT.md` of the 11:25 session), on top of the kit
commits. Translator unchanged (cbb93a8a…): the shader caches stay.

### The periodic ~12 ms frame
`hle_gx_native_token` ran a ~220-line report inline on bb-cp0 every 100,000 token draws:
~30 `host_log` lines (each a synchronous write) and a `/proc/self/maps` read to name the
slow guest reads' megabytes (3,158 mappings in the session). The rc5 data put 74 of 92
frames of 11 ms or more within -5…+25 ms of one. The block is now `native_periodic_report`
(gx_trace.cpp); the CP stores the due token's id and returns, and `bb-gx-report` (started on
the first report, polls every 100 ms) writes it. The map naming runs at exit only
(`hle_gx_scaleform_report` calls the report once more with `name_regions`; the stale-calls
part takes `g_native_mu` with try_lock there, as the exit can run on a thread that holds it)
or with `HELIAS_GX_REPORT_MAPS=1`. The counters it prints are atomics and the one map it
reads is under `g_native_mu` as before, so another thread may print them; the numbers are a
moment later than the CP's, nothing else. `report_native` (shadow mode) goes the same way.

### Asynchronous logging
`host_log_line` (log.h) keeps its old shape - tap, then a loop around one write - but the
write is `host_log_write2`, out of line, which hands the whole line to the sink when one is
installed (the shape matters: PGO's function hashes are taken after early inlining, and the
first version, which changed the inline code, made 640 functions' profiles mismatch, 52 of
the 300 hottest among them: draw_impl, cp_thread, gx_hook, prefetch_stage_images…; with the
shape kept, 10 do - the functions this build changed).
The sink (core/log_async.cpp): 16,384 slots of 244 bytes; a line takes 1-5 consecutive
tickets, reserved with one CAS on `head` when they fit, copied, and published per slot with
a release store of ticket+1. One writer thread drains in ticket order into a batch, writes
it when it reaches 4096 - 5 × 244 bytes at a line end (≤ PIPE_BUF: a pipe write that size is
atomic, so tee's input never has another process's bytes inside a line), waits up to 2 ms for
a producer mid-copy, and sleeps on a futex (10 ms timeout; a producer wakes it only if its
`sleeping` flag is set, after a seq_cst fence). A full ring: the producer takes the drain
lock and drains it itself (order kept; what a synchronous write would have cost), or, while
the writer holds it (inside a write to a pipe nobody reads), yields and then sleeps 200 us
at a time - never a direct write, which would reorder lines and (found by the test) let a
large batch write interleave inside a line. Producers use no thread-locals, locks or
errno-setting calls (raw syscalls for the futex wake, yield and sleep; no stack protector),
as guest threads and the fault handler log. `host_log_sync_mode()` (sink off, then a bounded
flush: 500 ms for the lock, as long for producers mid-copy) runs at every exit: the window
closing, `HELIAS_EXIT_FLIP/SECONDS`, the device-lost exit, SIGTERM (`on_term`), the fault
handler after `hle_fs_host()`, a new SIGABRT handler (SA_RESETHAND; re-raises, so the core
dump is as before), `atexit` (guest `exit()`), the stream selftest. A SIGKILL or the OOM
killer loses at most what the writer had not written yet (milliseconds). The F11 tap is
called by the logging thread as before, so the snapshot's log excerpt and its times are
unchanged. Box: 376-426 ns a line synchronously to a file, 147-175 ns queued (the
producer's side); the gain in the game is the stalls on a slow file, not the average.
`log_async_test`: 8 threads × 20,000 lines of 0-899 bytes of padding (1-4 slots) into a
pipe whose reader starts 300 ms late (the ring fills; producers drain and wait): every line
arrives once, byte-exact, each thread's lines in order, the lines logged after the joins
and after sync mode last; also clean under ThreadSanitizer (built by hand).

### Submissions
The one userptr left on AMD/Linux was heliasdream's own GX table ring (`hle_kernel_map_host`,
0xf000000000, 128 MiB; the game's flexible mappings in the session had no GPU bits and
were not imported). amdgpu revalidates a userptr at every CS (`hmm_range_fault`, 22.6% of
bb-submit). Where direct memory goes in as dma-bufs, `choose_dmem_import` now probes a
1 MiB sealed memfd of its own through /dev/udmabuf; if that imports (and the ring fits a
piece or sparse binding joins pieces), host mappings are made as sealed memfds
(`MAP_SHARED | MAP_FIXED_NOREPLACE` at the same addresses) and `import_anon` imports them
through `dmabuf_span` with that memfd. A failure there logs and falls back to the host
pointer. Not testable on the box (lavapipe takes host pointers; no /dev/udmabuf): the
user's log lines in TESTING.md's rc6 checks say whether it took.
dma-buf pieces: `size_limit_mb` of the udmabuf module (read at start; default 64) decides
the piece size, up to 1 GiB; each piece was one more BO in every submission's list (RADV's
global BO list: every allocation is in every CS). `HELIAS_SUBMIT_EVERY_JOB=0` exists and
was rejected in test4 (flashing effects): no new A/B.

### Small items
- Recorder wait: RDTSC for its own timing where CPUID 0x80000007 EDX[8] (invariant TSC)
  and the kernel's clocksource is `tsc`; ticks become ns at the reports from the TSC rate
  measured against CLOCK_MONOTONIC since the thread started. `HELIAS_RECORDER_TSC=2`
  forces it (a test: the box's clocksource is not the TSC); the selftest passes either way.
- `host_memmem`: AVX2 (runtime check) or SSE2 first/last-byte filter, then memcmp; the
  byte loop for the tail. `memmem_test`: 197,765 cases against the byte loop (alphabets of
  1-4 letters, needles 1-40, every offset 0-16 and three end cuts), 0 differences. 512 MiB
  of varied bytes: 197-320 ms byte loop, 83-98 ms new (noisy box).
- Kit: game.log on tmpfs (symlinked from the step folder while the game runs, moved there
  by `log_settle` at the exit and in the cleanup), `gpuq_record_bg` (gpu_scheduler +
  amdgpu CS tracepoints, 8 s) in every step's second window, and a draws-per-frame column
  with a >5% warning in the analysis (it flags the rc5 session's a1 and a2: 1008 and 1432
  draws a frame against ~1500-1600 for a3-a5 and m4). Dry runs with the fake game: a1, m4
  and a6, Ctrl-C, the tarball.

### PGO
`packaging/vk/build.sh` with `PGO_PROFILE=heliasdream.profdata` builds heliasdream.bin with
`-fprofile-use` (ThinLTO stays on) and `heliasdream.bin.nopgo` in build-ci/build-nopgo, both
through ci_build.sh's ctest. CMake: `-DHELIAS_PGO=OFF` ignores the profile;
`-DHELIAS_PGO_WARN=ON` reports mismatches. The profile came from the ea885ba-era kit binary;
against this commit 10 of its 6,696 functions no longer match (their counts are discarded,
they are built as without PGO): `host_memmem`, `hle_gx_trace_draw`, `hle_gx_native_token`,
`hle_kernel_map_host`, `hle_gx_scaleform_report`, `gpu::cpu_section`, `recorder_thread`,
`choose_dmem_import`, `rebuild_page_tables`, `main`. Of the profile's 300 hottest functions,
4 are among them (`host_memmem`, `hle_gx_native_token`, `rebuild_page_tables`,
`recorder_thread`). New functions (the log writer, the reporter) have no profile.

### Not in this build: the per-stage exact-key cache
`resolve_key_stage` reads V#/T#/S# words through user-data pointers into guest memory
(`resolve_via_plan` → `resolve_resource_impl`) unless every binding comes from the GX
records; `prefetch_stage_images` turns words into views through the texture cache, which
re-uploads on CPU writes it detects inside `texture_view`; `resolve_stage_buffers` updates
the buffer shadows, copy versions and the temporal observer. A cache hit would have to
prove the guest words unchanged - nothing tracks writes to those tables, and write-
protecting them would fault every frame - and would skip side effects the texture cache and
shadows rely on. Nothing on the build box runs the draw path (no game), so "byte-identical
with the cache on and off" cannot be shown here. For build 6: a draw-path replay harness
(recorded GX streams through draw_impl with the cache on/off, packets compared), then a
cache limited to `GxStagePlan::all_records` stages (records copied at the call, keyed by
their bytes) as the first, provable step.

## 2026-10-20 (Tue), vk.1-rc7 ("build 6"): the stage memo for the key words

Start: the tree was clean at bddd030 (no partial per-stage cache work in the checkout, its
stash or the other worktrees), and origin/phase-5-test-release already pointed at bddd030.

### What the per-stage cache can be, exactly
Of the three per-stage resolutions in the draw path, only `resolve_key_stage` is a pure
function of what a draw hands it: the translation's binding paths, the plan
(`stage_plan`: translation, descriptor fingerprint, plan generation), and per binding
either the GX record's bytes (`resolve_planned`) or a user-data/guest-memory walk
(`resolve_resource_impl`). The other two have to run every draw:
- `prefetch_stage_images` calls `texture_view`, which is where the texture cache notices
  CPU writes and re-uploads; skipping it on a "same words" hit would show stale textures.
  It also feeds the post-window and motion-blur-skip watchers.
- `resolve_stage_buffers` updates the buffer shadows (per-recording page state, write
  watches), looks up copy versions and YEBIS draw windows, snapshots unaligned constant
  buffers into the scratch ring (the bytes as of this draw) and feeds the temporal observer.
  A hit would need generations for all of that plus every guest word the walks read; the
  game rewrites its tables every frame and nothing tracks those writes cheaply (rc6 DEVLOG).
So rc7 memoizes only the key words, and only where the inputs are bytes in hand.

### The memo (render.cpp `resolve_key_stage`)
`KeyStage` (thread-local per stage in `draw_impl`) keeps `memo_paths`, `memo_plan` and
`memo_ok`. `memo_ok` is set after a full resolution when a plan was used (plans on, mode 1)
and every image and sampler binding found its record (`planned_record`: the plan's source
and slot, the object bit - for the sampler cache `obj_smp_set | obj_smp_default` - as
`resolve_planned` decides). Then each word list is exactly that record's first 4 or 8
dwords. At the next draw of the stage, with the same plan pointer (so the same translation,
descriptors and plan generation; plans are never freed) and the same paths, each binding's
record must again exist and `memcmp` equal to the kept words (16 bytes for an r128 image or
a sampler, 32 otherwise). Then the resolution would copy the same bytes, and `dims`/`modes`
are functions of the words and the bindings (`image_dim_of`, bit 15), so the lists are left
as they are. The `g_gx_resolves`/`g_gx_token_resolves` record counters are bumped as the
resolution would have. Anything else - a fallback binding, a missing object, different
bytes, another plan, `HELIAS_BINDING_PLANS` 0 or 2 - resolves in full, as rc6.
`draw_impl` skips rebuilding the key's shape list when both stages kept theirs (it is built
from those lists only; the fnv1a over it still runs, seeded by the new pipeline key).
A stage without a program counts as kept when the last call was also programless.
`HELIAS_CP_STAGE_CACHE=0` turns it off (`HELIAS_STAGE_CACHE` was already taken by the
translation cache, which must stay on); `=2` resolves again on every hit into a scratch
`KeyStage`, compares words, resolved flags, dims and modes, logs the first 8
disagreements and uses the fresh resolution. Exit line in `render_report`.
Cost of a hit: the plan lookup (as before), one bit test and a 16/32-byte compare a binding.
Saved: the vector rebuilds, `image_dim_of` and the per-binding counters, and for both
stages kept the shape list. The hit rate is not known (no game on the box); the exit line
gives it. Estimate 0.1-0.3 ms of bb-cp0 a frame at ~1,400 draws - well under the 1.1-1.9 ms
the rc5 report hoped for from all three resolutions, because the other two cannot be
skipped exactly.

### The other items, checked and left
- bb-submit's `hmm_range_fault`: the only userptr import was the GX table ring (128 MiB at
  0xf000000000; the game's flexible mappings had no GPU bits). rc6 already imports it as a
  memfd dma-buf on AMD/Linux (its start-up lines in TESTING's rc6 checks say whether that
  took on the PC). The BO-list sort is the kernel's, over RADV's global BO list; rc6's large
  dma-buf pieces (with udmabuf `size_limit_mb=1024`) are the lever left without
  sub-allocating images. Nothing new here until the rc6 measurements.
- Indexed compute constant buffers: binding the whole range differs from the walk for an
  index past `num_records` (GCN returns zeros, the walk reads memory); exactness needs a
  bounds check in the translated shader - a translator change (shader-cache rebuild) that
  the box cannot verify against the game. Game compute is ~0.48 ms of GPU in heavy Old
  Yharnam, so the gain is small. Left.
- bb-bind thread (splitting bb-cp0): L effort, needs the PM4 replay harness first. Left.
- Post chain (bloom, tonemap, grain) before FSR: changes pixels. Skipped by the rules.

Checked here: render.cpp compiles in both build trees, the full build (PGO and nopgo)
passes ctest; the draw path itself does not run on the box (no game), hence `=2`.

