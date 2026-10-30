# heliasdream-vk vk.1-rc5 plan (from rc4 = 1f0a403, "build 3")

Written 2026-10-20 (America/Chicago), the night of the pre-authorised build. The rules: only items that
are clearly safe and measurable; no loss of picture or function; no menu or launcher toggles (debug env
reverts only); no change to the translator (the shader cache stays valid: fingerprint
cbb93a8ae24af3169f3cf6d437c1ac69 before and after); no HDR, CA, online or MangoHud changes; no game, AMD
or proprietary files.

## What limits heavy Old Yharnam outdoors in rc4 (the evidence)

From `rc4plain/full`, snapshot `snap-20261020-010838-m23` and the 300-flip windows around it:

| Measure | Value | Meaning |
|---|---|---|
| fps | 114.4 avg, 1% low 81.9, 1675 of 3430 frames over 8.5 ms | both chains are at the limit |
| bb-cp0 | 880-970 ms/s | the command processor is ~91% of a core |
| cp-thread split (per 2.6 s, 438k draws) | exec 2279 ms (5.2 us/draw), draw 1809, bind 1252 (2.86 us/draw), prefetch 228, pipeline 24 | the CP's per-draw cost is the CPU limit |
| bb-record | 830-940 ms/s, of which `recorder spin 1656 ms, replay 611 ms` a window | 72% of the recorder is PAUSE spinning |
| cp-backlog | 10 (the cap) | the game waits on the CP |
| GPU idle with work queued | 75 ms/s (1226 gaps/s), ~50 ms/s after every game submission (b33f77 = host_gpu_submit) at ~45-55 us each, whatever the span | a per-command-buffer boundary cost, ~0.5 ms/frame |
| pacing | limiter `late avg 54 us p99 95 us`, `spin 0.17 ms a frame`; `vblank woke late avg 55 us` | Linux's 50 us timer slack, on every sleep |
| placement | no affinity anywhere | the hot chain can be split across the two CCDs (~600 vs ~100 ns per cache-line hand-off) |
| menu "bb-cp0=2885 ms/s" | the lazily started TableMask (x4) and LeanBuilder (x2) workers inherit the name `bb-cp0` | an accounting artefact, not the CP |
| blit_ride | rode 29487, alone 7322, missed 1553 | works as designed (alone = the frame's submission had already gone; missed = a race with a 344 us vkQueueSubmit). Nothing to gain |

## Items for rc5

Saving: what it frees, where the snapshot shows it. "Risk" is to picture/function. All are on by default,
chosen automatically, and each has a debug env revert.

| # | Item | Expected saving | Risk | Effort | Evidence | Measure in rc5 (log label / F11 field) |
|---|---|---|---|---|---|---|
| 1 | **Recorder waits with MONITORX/MWAITX** instead of 4,000 PAUSEs (CPU audit M3). Monitor on g_published's line; the timer = the spin's own length in TSC ticks (calibrated at start); then the futex as before. Gated on CPUID 8000_0001h ECX[29]; the PAUSE loop stays where it is absent. | ~0.4-0.6 core freed (bb-record ~870 → ~300 ms/s); the SMT sibling's issue slots and package power for boost on the CCD that runs bb-cp0 | None to the picture (the same blocks, same order; the wake is a store to the monitored line). Function: a missed wake falls through to the futex as before | S | `recorder spin 1656 ms, replay 611 ms` per window | log `recorder: wait for the next block with MONITORX/MWAITX`; the 300-flip line `rc5 wait mwaitx: mwaitx N (M ended by a block)`; [cpu] `recorder_wait:`; [seconds] bb-record. Revert `HELIAS_RECORDER_MWAITX=0` |
| 2 | **CCD-aware placement** (M1). With ≥2 L3 domains of ≥6 CPUs: the hot chain (main thread, GXRenderThread, bb-cp0, bb-record, bb-submit, bb-present, bb-vblank) gets one whole CCD (larger L3, then prefcore ranking / CPPC / max freq); bb-compile (in game), bb-cs-optimize, bb-tablemask and bb-lean get the other; every other thread keeps the whole mask (explicitly reset, since threads inherit their creator's mask). | 0-0.7 ms/frame of bb-cp0 + bb-record (cache-line hand-offs: ~1,400 packets a frame plus the CP ↔ game ring); compiles stop evicting the hot L3 | None to the picture. Function: whole-CCD masks only (16 threads for 7 hot ones), so a pinned thread is never stuck behind one busy core; compiles get the whole mask back while booting/loading | S | No affinity in the code (`scePthreadSetaffinity` → 0, `host_thread_set_class` no-op on Linux); the recorder's 600 vs 100 ns note | log `placement:`; [cpu] `placement:` (hot/cold lists, threads per class, failures). Revert `HELIAS_AFFINITY=0` |
| 3 | **Timer slack 1 ns + lower spin floors** (M2): PR_SET_TIMERSLACK on the timing threads; the limiter's learnt margin floor 300 → 150 us (+100 instead of +200), the presenter's spin 300 → 150 us, only when the slack was set | ~0.15 ms/frame less main-thread spin, ~0.15 ms/frame less bb-present spin; wakes ~50 us more precise | None to the picture. Function: a late wake past the smaller margin shows as `late` in `pacing:` and the margin re-learns itself upward (it is clamped at 2 ms) | XS | `late avg 54-55 us` = the default 50 us slack, in every window | log `timer slack:`; [cpu] `timer_slack:`; `pacing:` late avg and spin. Revert `HELIAS_TIMER_SLACK=0` |
| 4 | **ThinLTO** for the Helia's Dream executable (clang + lld); the decomp sources stay out (`-fno-lto`), their objects are byte-identical to rc4's flags (checked, 15 of 15) | 2-8% CPU on bb-cp0 / bb-record (render.cpp ↔ gpu.cpp ↔ recorder.cpp calls inline) | None: same semantics; the decomp's bit-exact maths is in non-LTO objects; the translator library is not LTO'd. Risk of a compiler bug, covered by ctest + selftests | XS | CPU audit §4 | [cpu] `cp_cost:` exec/draw/bind ns per draw (rc4: ~5,200 / ~4,100 / ~2,860). Build option `-DHELIAS_THINLTO=OFF` |
| 5 | **Page tables in ReBAR video memory** (GPU audit #3): the L1, the sink L2 and every L2 table in DEVICE_LOCAL\|HOST_VISIBLE memory when that heap is > 1 GiB; else system memory as before | 0.02-0.10 ms/frame of GPU (every remaining walk's two dependent loads stop crossing PCIe after each submission's cache invalidation) | None to the picture (the same bytes). The CPU only writes them (write-combined as before); RADV invalidates L2/vector/scalar caches at the start of every submission, so visibility is the same as now | XS | 142 of 712 VS code objects, 73 of 119 CS and all HS still walk | log `page tables:`; [walks] `page_tables: vram= system=`; [pass_stats] scene passes. Revert `HELIAS_TABLES_VRAM=0` |
| 6 | **Name the inherited workers**: `bb-tablemask`, `bb-lean` (they inherited `bb-cp0`) | Accounting only: [seconds] bb-cp0 is the CP again (the menu's 2885 ms/s) | None | XS | snapshot 011151 | [seconds] |
| 7 | **C3: the motion-blur name search** stops after its 8 log lines instead of running on every draw with a colour target | ~20-40 ns/draw (~0.03-0.05 ms/frame) | None (diagnostic only) | XS | CPU audit C3 | `cp_cost:` |
| 8 | **[cpu] snapshot section**: placement, timer slack, recorder wait, CP cost per draw since start and since the previous snapshot | Measurement for 1-7 | None | XS | — | F11 `[cpu]` |

Combined expectation for heavy Old Yharnam (an estimate, to be measured): bb-cp0's per-draw cost −3-10%
(ThinLTO, placement, the freed SMT sibling), bb-record −0.5 core, ~0.3 ms/frame less spinning on the
main and present threads, and a small GPU saving from #5. If the CP is the limit in that scene, that is
roughly +3-8 fps average and a better 1% low; if the GPU is, mostly the 1% lows and the latency.

## Considered and left out tonight (with reasons)

| Item | Why not now |
|---|---|
| GPU #1 compute constant buffers (cb_ssbo for CS) | 0.15-0.30 ms GPU, but the copy-version/window hazard rules of `resolve_stage_buffers` and YEBIS's self-writes need a game test; M-L effort. **First on the next list** |
| GPU #2 tess LS/HS walks, #10 translator ALU ops | change the translator → the fingerprint → the user's shader cache rebuilds; need an image comparison |
| GPU #6 pass-end barrier narrowing, #7 RT-copy elision | corruption risk; no image comparison possible on the box |
| GPU #5 async-compute FSR | rejected before (sync and pacing risk) |
| Submit batching | rejected in t9 (queued gaps only 66 → 53 ms/s) |
| Blit ride changes | works as designed (see above) |
| M6 vkQueueSubmit BO list | udmabuf's 64 MiB size_limit forces the 95 pieces |
| M5 lower CP backlog | latency, not fps; needs the A/B (`HELIAS_CP_BACKLOG`) |
| C2 key-shape memo, C1 bind-section work | need the profile to say which part of the 2.86 us/draw dominates; a wrong key = wrong pipeline |
| `-march=x86-64-v3` / znver3 | the release must stay portable; FMA would need global `-ffp-contract=off` |
| Frame pointers | ~1% cost; DWARF unwinding already works with perf |
| PGO | needs a profile run on the PC; next build |
| PS wave32 (`HELIAS_PS_WAVE32=32`), params WC (`HELIAS_PARAMS_WC=1`) | unknown sign: stay env A/Bs (TESTING.md) |

## Ranked candidates for the next build (by measured cost)

1. **CP draw path, the bind section: 2.86 us/draw ≈ 4 ms/frame of bb-cp0** (and the draw section's ~4 us).
   Profile with the bundled `cpuprofile.sh` (dwarf-bb-cp0.txt) first, then: cache per-stage descriptor
   resolution across draws when the stage's program, user data and T#/S# words are unchanged (C2 memo,
   exact-key only), drop the per-draw `DrawPacket` vector churn, move set lookup by content to a
   per-pipeline last-hit cache. Expected 0.5-1.5 ms/frame of CP.
2. **Split bb-cp0's draw work** (resolve on cp0, a second thread builds the packets/descriptor writes per
   pass), or record command buffers per pass in parallel on bb-record workers. Large redesign (`g.mu`
   covers all of draw_impl today); the gain is up to ~half the CP's 91% if the split is clean.
3. **GPU #1: compute constant buffers** (0.15-0.30 ms GPU).
4. **The per-command-buffer boundary (~0.5 ms/frame GPU idle with work queued, ~45-55 us per game
   submission)**: RADV's end-of-cmdbuf flushes (the busy meter's timestamps add PS/CS partial flush +
   INV_L2) — measure with the meter's timestamps off (an A/B), then fewer game command buffers per frame.
5. **PGO** (instrumented, Old Yharnam + Dream): 10-20% of the branchy CP code.
6. **GPU #2 tess walks** (0.10-0.20 ms), together with any other translator change, so the cache rebuilds
   once.
7. **Merging the small post passes** (needs replacement shaders: out of scope until then).
8. M4 (the ~5 ms hitch every 1-2 s) from the cpuprofile sched traces.

## Validation plan

- Clean CI build (`tools/ci_build.sh`, CI_PORTABLE=1, HELIAS_RELEASE=ON), ctest 53/53 (also under the
  validation layer).
- `--stream-selftest`, `=2` with HELIAS_FSR4_FEATURES=1, `=20`, with the validation layer and sync
  validation: pass, the same 10 upstream SYNC-HAZARD lines; the same with every rc5 revert set.
- The decomp objects with and without the LTO flags: byte-identical `.text`.
- The translator fingerprint unchanged.
- On the PC (TESTING.md "rc5 checks"): the new log lines, [cpu] fields, the same Old Yharnam spot as
  rc4 `010838`, and the env reverts as A/Bs.

## rc5b ("build 5", on top of df31b8b): the two biggest costs

Asked the same night: the two deferred costs, in order, each with a debug `HELIAS_*=0` revert, its own
log label and F11 fields. Barrier and copy elision stay out (they need an image comparison).

| # | Item | Expected saving | Risk | Effort | Evidence | Measure (log / F11) |
|---|---|---|---|---|---|---|
| b1 | **CP bind path, waste removal + sub-stage timers.** Code reading found little left (lock-free counters, sampler memo, set cache by content, dirty-tracked dynamic state, lazy-barrier sets off). Removed: the per-stage `strstr` of the motion-blur log in `stage_final_views` (C3b). Added: `cp_split`, RenderCostStamp's 20 sections timed with the TSC on the 1-in-16 sampled draws | C3b ~10-30 ns/draw. cp_split costs ~10-20 ns/draw; it is the measurement the exact-key caches need (a wrong key = a wrong pipeline/binding, so no guessing) | None: no recorded byte changes; selftests 0 differences | S | rc4 `cp-thread split`: bind 2.86 us/draw, draw 4.1 us/draw | log `cp split:`; [cpu] `cp_split:`, `cp_split_total:`. `HELIAS_CP_SPLIT=0` |
| b2 | **Splitting bb-cp0** | — (design only; see below) | — | L | draw_impl holds `g.mu` over all renderer state | — |
| b3 | **Compute constant buffers** (GPU audit #1): `cb_ssbo` for the game's compute shaders; a compute layout with bindings 112-127; a conservative resolver that binds only what provably reads the walk's bytes (constant offsets, not indexed, imported as one buffer, no copy-back, vec4 base, not stored by the dispatch, not GPU-written earlier in the command buffer, nor the tables the V# came from). Walk otherwise, as before | GPU 0.1-0.3 ms/frame in heavy Old Yharnam (audit: 0.15-0.30 for all buffers; indexed ones stay on the walk). CP +0.03-0.05 ms/frame (~17 dispatches a frame) | Low-medium: compute stages translate again once (first start: longer precompile, possible first-sight hitches); the hazard rules are stricter than the graphics stages', but the dispatch path could not run on the box | M | Cache: 41 of 42 CS walk; ISA: 73 of 119 CS; top kernels 1-1.5k dependent loads; RGP game compute ≈ 0.48 ms | log `gpu: compute cb:`; [walks] `compute_cb:`. `HELIAS_CS_CB=0` |

Validation for rc5b: ctest 54/54 (new `gcn_cs_cb_test`), the three selftests with the validation layer and
sync validation (0 differences, the same 10 upstream SYNC-HAZARD lines), the same with `HELIAS_CS_CB=0
HELIAS_CP_SPLIT=0`, the translator fingerprint unchanged, and locally only (never committed) the user's
stage-manifest compute programs translated with and without `cb_ssbo`: 6/6, no regressions, valid SPIR-V,
bindings 112-127.

### Splitting bb-cp0: design (not built tonight)
- **Why not tonight:** draw_impl holds `g.mu` for ~4.1 us/draw over the pass, set caches, params ring,
  copy versions, uploads, texture cache, the YEBIS draw windows and Scaleform snapshots (CPU copies at
  record time), and reads guest memory (V#/T#/tables) at the moment the CP reaches the draw. Nothing in
  the selftests can show ordering against labels, flips and DMA for a split today.
- **Stage A (bb-cp0):** PM4, register shadow, and a per-draw snapshot (the registers draw_impl reads,
  user SGPRs, GX record pointer, index/indirect state, ~1-2 KB) plus the CPU copies taken at record time
  now (windows, SF snapshots), into an SPSC ring.
- **Stage B (bb-bind):** the sole owner of the renderer state; draws and dispatches in ring order.
- **Ordering rule:** labels, EOP/fence writes, WAIT_REG_MEM, flips, DMA and CPU-visible copies stay on
  cp0 and wait for stage B to drain what is before them (the game may rewrite descriptors once it sees a
  label). The per-pass YEBIS waits are where the gain could vanish.
- **Proof first:** a selftest that replays recorded PM4 streams in place and split and compares the
  packet streams byte for byte (the pattern of the stream selftest for bb-record).
- **Gain if it holds:** cp0 ~1.5 of ~5.2 us/draw; stage B ~3.5-4 us becomes the limit: ~+25% draws/s.
- Per-pass parallel recording is not the lever: bb-record already records off cp0 and mostly waits
  after rc5's MWAITX.

### Ranked next (after rc5b, by measured cost)
1. **The biggest `cp_split` section** from the PC run: an exact-key cache there (C2 memo for
   `stage_user`/`buffers`/`images` when the stage's program, user data and descriptor words are
   unchanged; a per-pipeline last-hit for `lookup`/`sets`). Expected 0.5-1.5 ms/frame of CP.
2. **The cp0 split** above, if (1) leaves the CP the limit; selftest first.
3. **Indexed compute constant buffers** (bind with the walk's semantics past the records, or check the
   records at run time): the rest of GPU #1, if `compute_cb:` shows `walk_indexed` large.
4. The per-command-buffer boundary (~0.5 ms/frame GPU idle), PGO, tess walks with other translator work,
   merging small post passes — as in the list above.
