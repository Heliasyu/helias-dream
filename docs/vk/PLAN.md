# heliasdream-vk: development plan (revision 2, 2026-10-21)

**heliasdream-vk** is a GPLv3 fork of droogie's Helia's Dream (v0.2.18 + the 120 fps patch) with temporal FSR 3.1 and FSR 4.1.1, a sharp native-resolution UI, and a better launcher, for an RX 9070 XT + Ryzen 9 5950X at 4K.

**End goal:** 1920x1080 render → 3840x2160 output with FSR 4.1.1 Performance, a **locked 120 fps** on the 165 Hz VRR monitor, including the heavy areas (Old Yharnam smoke, Forbidden Woods, Yahar'gul). Quality or Balanced at 120 is a stretch goal, not a requirement.

This revision replaces the 2026-10-18 plan. What changed and why:
- Phases 0-4 are done, so they are reduced to a status table.
- Phase 5 targeted the wrong bottleneck. The rc2-rc7 data shows the limit is the single command-processor thread (`bb-cp0`), not FSR or motion vectors. Phase 5 is rewritten around that, with an exit rule so it can't run forever.
- The launcher overhaul (L) and the release (6) were skipped while Phase 5 looped. They now have fixed slots.
- A short "release hygiene" phase comes first: until 2026-10-21 nothing had been merged to `main`, and there are no GitHub Releases yet.
- The testing section now describes how sessions actually run.

---

## Hard rule: online play (paramount)
Online play must work exactly as in upstream heliasdream. Helia's Dream uses thehuntersdream.com (partnered with Helia's Dream), and heliasdream-vk uses the same servers, the same auth server, the same account/sign-in flow (Discord, recovery code, website code) and the same networking code, unchanged.
- No phase may modify `src/net/`, the HLE network/HTTP/NP code, the account flow, or the server defaults (`online.host`, `auth_server`, STUN), except by merging upstream Helia's Dream changes.
- The client keeps upstream's user agent format (`heliasdream/<version>`); heliasdream-vk versions keep the upstream base version first (e.g. `v0.2.18-vk.N`) so any server-side version handling sees the same Helia's Dream release.
- When upstream Helia's Dream updates its online code or server requirements, heliasdream-vk merges that before anything else.
- Every test session checks sign-in and an online action (messages/bloodstains visible, summon sign or bell) before the session passes.
- All rendering, upscaling, launcher and extras work is client-side only.

---

## Where things stand (build `v0.2.18-vk.1-18-gb369d66`, rc7)

| Phase | Status | Left over |
|---|---|---|
| 0 Groundwork | Done | - |
| 1 Scene/UI split | Done | A start at one resolution followed by a live change to another leaves the display buffers at 5120x2160, so the split refuses and the game renders at native 4K (log: `scene-split: not split at 3840x2160 - the display buffers are 5120x2160`). Fixed in R2. |
| 2 Motion vectors, jitter | Done | - |
| 3 FSR 3.1 | Done | - |
| 4 FSR 4.1.1 | Done: FP8 replay on RDNA4 (`FP8 t2160_m0`), INT8 and emulated-FP8 variants in the code, a build button in the current launcher, fallback to FSR 3.1 | The plan's "output matches the DLL" check was never recorded. Done in R3. |
| 5 Optimization | rc2-rc7: frame pacing, post at render size, motion-blur skip, PGO, async logging, stage memo | Replaced by the new Phase 5 below. |
| L Launcher overhaul | Mockups only (`docs/vk/mock/launcher-*.png`, ten tabs). The real launcher still has the old tabs. | All of it. |
| 6 Polish and release | Not started. (`phase-5-test-release` was merged into `main` on 2026-10-21, `15564b9`.) | All of it. |
| H1 DualSense haptics | Done in vk.2 (2026-10-22): HD haptics over USB (rumble and sound effects, music filtered), menu taps, animation-driven adaptive triggers (R2 charge, L2 break slack at 0 bullets), F10 > CONTROLLER | Other weapons' charge and firearm animations; in-game menu rows for the settings; more trigger effects (L1 transform, rolls). |

**Measured today** (F11 snapshots, Old Yharnam, FSR 4.1.1 Performance, 4K, 120 cap):
- **Fps:** 114-122 fps average, 1% lows 82-111.
- **CPU:** `bb-cp0` is busy 880-970 ms out of every second. At about 5 µs per draw and ~1,450 draws a frame, that's ~7.2 ms of an 8.33 ms frame.
- **GPU:** busy 81-93%:
  - the game's passes take ~5.0 ms, of which ~1.1 ms is post-processing that still runs at full 4K;
  - FSR 4.1.1 takes ~2.0 ms;
  - the sharpen pass takes ~0.3 ms.
- **Idle:** the GPU sits idle ~0.4-0.5 ms a frame while work is already queued.
- **Conclusion:** the CPU and GPU are both near their ceiling. Smoke adds draws (CPU) and overdraw (GPU), and either one alone tips the frame over 8.33 ms.

---

## Work order
1. **R: Release hygiene** (1-2 days), first because it's quick and unblocks the updater
2. **Phase 5: Performance, retargeted** (2-3 weeks, with an exit rule)
3. **Phase L: Launcher overhaul** (5-8 days; can start alongside 5 once R is done)
4. **Phase 6: Polish and release `v0.2.18-vk.2`** (3-5 days), the first "finished" release
5. **Phase 7: More upscalers for non-RDNA4 GPUs** (after vk.2)
6. **Phase 8: Windows** (after 7)
7. **Phase H: maybes**, decided after vk.2

Rules that apply to every phase:
- **One logical change per commit.** No more "rc6 (4/7)" bundles; a bisect has to land on one change.
- **Every playable build is merged to `main` and published.** It gets a tag and a GitHub Release (Linux binary + wrapper + SHA256SUMS, never game files or FSR 4.1.1 assets), so `main` really is the playable build and the updater works.
- **Every optimization reports its measured gain.** If an F11 A/B on the user's PC shows no gain, the change is reverted or left off by default. Estimates don't count.

---

## R: Release hygiene (1-2 days)
1. ~~Merge `phase-5-test-release` into `main`~~ (done 2026-10-21, `15564b9`; the stacked phase PRs #1-#6 are merged or closed). Publish the `v0.2.18-vk.1` tag (`3ef28ff`) as a GitHub Release with its package and a SHA256SUMS, so the in-game updater has something to find. rc7 is newer than that tag; it ships as `vk.2` from Phase 6, not under the `vk.1` name.
2. **Fix the 5120x2160 display-buffer bug.** Reproduce it by starting the game at the size in `heliasdream.toml` (`width = 1920`, `height = 1080`, fullscreen on the 4K monitor, which uses 150% scaling) and then switching to 3840x2160. Find where the display buffers get 5120 wide; the suspect is the logical width of both monitors combined (2560 + 2560). Make the fullscreen start size follow `heliasdream-options.toml`'s `resolution`, not the old `[window]` width and height.
3. **Make the frame cap come from one place.** `heliasdream.toml` has `fps_cap = 30` and `heliasdream-options.toml` has `frame_cap = "90"`. Log which one wins, and have the launcher write only one of them.
4. Bring the README's "`main` is the playable build" in line with reality. Keep `upstream-sync` (droogie is still at v0.2.18; nothing to merge yet).
- **Done when:** a fresh start at 4K Performance logs `scene-split: ... split` (not refused), the updater finds `vk.1`, and `main` = the published build.
- **User test (5 min):** start, check the log line, open F10 > Upscaling, quit.

---

## Phase 5: Performance, retargeted (2-3 weeks)

### Exit rule
Phase 5 ends at the **first** of these:
- **Target met:** at FSR 4.1.1 Performance, 4K, 120 cap, along fixed routes in Old Yharnam (smoke), the Forbidden Woods and Yahar'gul, the 1% low is at least **115** and under **2%** of frames are over 8.5 ms.
- **Diminishing returns:** two builds in a row each gain less than 0.3 ms a frame on those routes. Ship what's there.
- **Out of time:** 3 weeks have passed. Ship what's there and record what's left.

### Step 5.0: Diagnose the smoke drops (1 session)
- **What to capture:** with `bbtest start p5`, stand in Old Yharnam's smoke and press F11 *during* a drop at Performance, then again at Ultra Performance.
- **How to read it:**
  - **CPU-bound:** fps barely moves at Ultra Performance and `bb-cp0` is ≥ 950 ms/s. Steps 5.2-5.3 matter most.
  - **GPU-bound:** fps goes up at Ultra Performance. Step 5.1 and 5.4 matter most.
- **Record the result here.** It decides the order of the steps below.

### Step 5.1: Cheap GPU tests (2-3 days, no new code, environment switches only)
1. **Lazy pass barriers on RDNA4** (`HELIAS_PASS_BARRIERS=lazy`).
   - **Today:** every render-pass end (~37 a frame) inserts a full ALL_GRAPHICS → ALL_COMMANDS memory barrier (`render.cpp` `record_pass_barrier`). The lazy mode was turned off only because of corruption on a Steam Deck with compressed targets, and was never tested on RDNA4.
   - **Test:** A/B it.
   - **If you see white blocks:** retry with `HELIAS_RT_DCC=0` as well. If it's clean and faster, turn it on by default for RDNA4 only.
2. **Glare pass `8747a367+d92266b9`:** 186-286 µs per draw for 2-71 µs of work, which means the GPU is waiting, not shading. Find which barrier or layout change between its draws causes it, with an RGP capture (`bbtest profile`).
3. **FSR 4.1.1's 29 dispatches** each end with a compute→compute barrier, including the small `*_post` border clears. Leave out the barriers between passes that don't depend on each other (from the replay's resource names), then check the output is unchanged against the current one with `HELIAS_FSR_DUMP`.

### Step 5.2: A draw-path replay harness (4-6 days)
The prerequisite for touching `bb-cp0` safely; the rc6/rc7 DEVLOG already calls for it.
- **Record:** the GX/PM4 streams of a few hundred frames (the Old Yharnam smoke, a boss fight, the Dream).
- **Replay:** run them through `draw_impl` off-line.
- **Compare:** the recorded Vulkan packets, byte for byte, between builds.
- **Result:** CPU-side changes can be proven identical on the build box without the game.

### Step 5.3: Split `bb-cp0` (1-2 weeks), the main lever
- **Goal:** `bb-cp0` under ~5 ms a frame (≤ 600 ms/s at 120), leaving room for smoke.
- **Today's per-draw cost** (`cp split`, ns): buffers ~450-770, prefetch ~500-620, programs ~360-480, targets ~300, key ~290, sets ~200, update ~170-200.
- **The plan:** keep the guest-ordered part (registers, targets, programs, key) on `bb-cp0`, and hand the resolved draw to a second thread, `bb-bind`, which builds the descriptor sets, updates the buffer shadows and records the packet. Use a lock-free queue with order kept by sequence number.
- **Check:** byte-identical output in the 5.2 harness, then F11 A/B on the user's PC.
- **Smaller wins to take on the way:**
  - The small GPU memory writes are recorded as one `vkCmdFillBuffer` per 4 bytes (`gpu.cpp` around 6859). Merge neighbouring dwords into one `vkCmdUpdateBuffer`.
  - The pass-hazard bookkeeping uses `std::unordered_set`. Replace it with a small fixed array, since a pass has at most 8 targets.

### Step 5.4: The GPU frame (if 5.0 says GPU-bound)
- **GPU idle between submissions:** ~0.4-0.5 ms a frame, mostly after occlusion-query resolves and presents. Measure whether the resolve can move to the end of the frame's last submission (the game reads the result a frame later).
- **Already ruled out:** batching submissions (t9), moving chromatic aberration and the glare before FSR (it changes the picture), and async compute for FSR or motion vectors (no overlap possible; motion vectors cost 0.02 ms). Don't redo these.

### Testing
- **Session:** `bbtest start p5` on the three routes, with F11 at fixed points on each route. Same settings every time.
- **Comparison:** the snapshot summaries (`bbtest snap --show`) against the previous build, in a table: avg fps, 1% low, frames over 8.5 ms, `bb-cp0` ms/s, GPU busy %.

---

## Phase L: Launcher overhaul (5-8 days)
Build the mockups in `docs/vk/mock/` in ImGui, in the existing `launcher.cpp`, after the user has looked through them once and signed off (or marked what to change).
- **Tabs across the top:** Play, Display & frame rate, Upscaling, Graphics & effects, Camera & input, PC enhancements, Patches & plugins, Online & account, Game files, Advanced.
- **Upscaling page:**
  - upscaler and quality dropdowns, with the render resolution shown;
  - sharpness;
  - the FSR 4.1.1 DLL card: pick or find the DLL, show its version and hash, Build/Rebuild with live progress, Ready/variant (FP8/INT8) and which DLL it was built from;
  - the fine-tuning and debug toggles.
- **Play page:** a ready check (game files, 1.09, signed in, server reachable, upscaler ready, *scene split will apply at this resolution*) and a summary line of the active settings.
- **Settings in one place:** one source for each setting. This removes the `fps_cap` / `frame_cap` and `width/height` / `resolution` duplicates from R.
- **Style:** the "Paleblood" theme (`#0a0c11`, `#10131a`, `#161a23`, accent `#a9bfe3`, PLAY `#8e1b2a`), a serif heading font, and correct sizing on both monitors (100% and 150%; keep the wrapper).
- **Done when:** every current option is reachable, saves correctly, and F10 shows the same values. The old launcher code is removed.
- **User test (10 min):** `bbtest start pL`. Open every page on both monitors, change a few options, launch, compare with F10.

---

## Phase 6: Polish and release `v0.2.18-vk.2` (3-5 days)
- **FSR 4.1.1 check (Phase 4's open item):** compare the replay's output with the DLL's on the same captured inputs (tools/fsr4cap already records the D3D12 frames) for a few frames, and record the largest difference per channel. "Close enough to be invisible" is acceptable if it's measured and written down.
- **Edge cases:** debug camera, cutscenes, loading screens, alt-tab, resolution changes in game (KNOWN_ISSUES' Settings-menu bug, fixed on the branch but not yet confirmed in game), summons and invasions, Chalice Dungeons.
- **Upscaler off** behaves exactly like stock heliasdream.
- **Docs:** README, CHANGELOG, KNOWN_ISSUES, release notes; DEVLOG trimmed to decisions and measurements.
- **Package:** a patch set that can be reapplied to a new Helia's Dream release, and the Release on GitHub.
- **Done when:** a normal one-hour session (online, a summon, a Chalice Dungeon, alt-tab, a resolution change) has no crash or visual bug.
- **User test:** play normally with `bbtest start p6` running; F11 or `bbtest mark` at anything odd.

---

## Phase 7: More upscalers for non-RDNA4 GPUs (after vk.2)
All reuse the Phase 1-2 inputs, so each is mostly an adapter behind the upscaler interface. The Upscaling tab and F10 list only what the GPU supports; FSR 3.1 is always the fallback.
- **FSR 4 INT8 (RDNA2/3, Steam Deck):** the code already selects INT8 and emulated-FP8 variants. What's left is presets and performance work for slower cards, and testers. AMD's FSR SDK 2.3 officially supports FSR 4.1 on RDNA3 now, with RDNA2 support due in early 2027, so check whether newer DLLs change the capture.
- **DLSS (NVIDIA RTX):** NVIDIA's Vulkan DLSS SDK. Check the licence first; if the runtime can't ship, the user supplies it.
- **XeSS (Intel Arc and others):** Intel's XeSS SDK, Vulkan backend.
- **Never committed:** proprietary runtimes, AMD DLLs, built FSR 4 assets.
- **Done when:** each upscaler is as stable as FSR 3.1 with no new ghosting, and switching in F10 works without a restart.
- **Testing:** the 9070 XT covers INT8 and XeSS; DLSS and the Deck need community testers.

## Phase 8: Windows (after 7)
- **Scope:** upstream Helia's Dream already builds for Windows (`tools/ci_windows.sh`, `.github/workflows/release.yml`). The work is making heliasdream-vk's additions build and run there: the upscalers, the FSR 4.1.1 builder (the capture needs vkd3d-proton on Linux; on Windows it may run the DLL's D3D12 passes natively), the launcher, and a Windows equivalent of `bbtest`.
- **CI:** a Windows build in CI and a Windows test pass.
- **Done when:** the Windows build matches Linux in features, image quality and performance.

---

## Phase H (MAYBE, decided after vk.2)
None of these touch rendering or online play; each can slot in any time after Phase 6.

1. **DualSense HD haptics and adaptive triggers.** *Done in vk.2 (see the status table and CHANGELOG).* The PS4 game only has DualShock 4 rumble, so haptics have to be made, not converted:
   - better rumble: rumble values translated into DualSense haptic waveforms (quickest);
   - audio-driven haptics: gunshots, parries, impacts, footsteps and rain routed as waveforms (the "HD haptics" feel). Needs USB on Linux; Bluetooth may not be possible;
   - adaptive triggers: resistance for the pistol, charged attacks and similar, through SDL3.
2. **DualSense lightbar:** follows HP from pale blue `#a9bfe3` to blood red `#8e1b2a`, with flashes on Insight gained and visceral/parry hits. USB and Bluetooth.
3. **Discord Rich Presence:** "Playing Bloodborne (heliasdream-vk)", area or boss, playtime, online/offline, through Discord's local IPC. Nothing is sent to heliasdream's servers.
4. **Trophies:** an in-game unlock popup, a Trophies page in the launcher (earned/missing, dates, platinum progress) and platinum awarding. Upstream already records unlocks in `/data/trophies.txt`. Names and icons need a user-provided trophy key, with names only otherwise.
5. **ReShade compatibility:**
   - (a) document the vkBasalt / native ReShade layer route as it works today;
   - or (b) recommended if pursued: a built-in ReShade .fx pass after FSR and YEBIS but before the HUD, with real scene depth and a launcher page. About 8-15 dev-days.

All are off by default and are ignored for other controllers or when Discord isn't running.

**New candidate (not yet on the list, the user decides):**
6. **FSR Frame Generation 4.0.1** (FSR SDK 2.3). It would turn a locked 120 into ~240 on the 165 Hz monitor, or ease the 120 target. It would need its own capture/replay like FSR 4.1.1, plus pacing work with the present pacer. It adds latency. Only worth considering after Phase 5 shows where the limit ends up.

---

## Testing: how it actually works
- **Sessions:** the user runs `bbtest start <phase>` (it logs the Helia's Dream log, MangoHud CSV, screenshots, optional recording) and `bbtest stop` bundles the session in `~/.local/share/bbtest/sessions/`. The user hands the bundle over; it is analysed afterwards. Nothing is watched live.
- **F11 snapshots:** F11 in the game (or `bbtest snap`) saves the last 30 s: frame times, GPU time per pass, CPU per thread, compiles, settings and the log tail. **These are the main performance evidence.** Take them *during* a drop, not after. `bbtest snap --show` prints their summaries.
- **Other tools:** `bbtest profile` adds an RGP capture of the next frame per F11 and shader statistics.
- **Per phase:** one session per phase, plus short re-runs of just what failed. A phase closes only when its test passes.
- **Keep:** one current `bbtest` in the game folder; old copies (`bbtest.test*`, `bbtest.rc*`) can go.

## Risks
- **The `bb-cp0` split** is the most invasive change since Phase 1. Draw order and resource lifetimes must stay exact; that's why the replay harness comes first.
- **Lazy barriers / DCC:** a missed read shows as corruption (white blocks). It needs a long visual test, not just a frame-time A/B.
- **FSR 4.1.1 assets** must be built from the user's own DLL and are never shared. A newer DLL may need fsr4cap changes.
- **Upstream updates:** each new Helia's Dream release needs the patch set reapplied; online-code changes are merged first.
- **DLSS/XeSS licensing:** checked before any code; the fallback is user-provided runtimes.
- **Windows:** the risk is only in heliasdream-vk's own Linux-only parts.
- **Online play:** unaffected (hard rule above).

## Repo housekeeping
- **History:** keeps droogie's full history; the `upstream` remote points at `droogie/bbhost`; `upstream-sync` takes new Helia's Dream releases first.
- **Versions:** `<upstream version>-vk.<n>`, a tag and a GitHub Release per playable build.
- **`.gitignore`:** keeps out game dumps, saves, built FSR 4.1.1 assets, logs and account files such as recovery codes.
