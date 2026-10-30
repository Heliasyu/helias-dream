# Known issues (heliasdream-vk)

## DualSense HD haptics: nothing is felt
- **On Bluetooth with the cable in:** a DualSense already connected over Bluetooth stays on
  Bluetooth when the USB cable goes in (it only charges), and ignores the sound card Linux
  shows for it. Disconnect it in the Bluetooth settings and replug the cable. Helia's Dream shows a
  message when a DualSense is on Bluetooth.
- **Pro Audio profile:** in PipeWire's Pro Audio profile the card's channels are AUX0..AUX3
  and a 4-channel stream is mixed down onto the speaker's two. Set the card to **Direct**
  (KDE: System Settings > Sound). The log says when the card is in Pro Audio.

## Adaptive triggers on other weapons
The R2 charge (032600 / 032610 / 032630) and the gun's shot (117051000) and empty pull
(117054500) are the animations of the weapon and pistol they were tuned on. Weapons whose
charged attacks or firearms use other animation numbers get the resting wall and break only;
`HELIAS_HAPTICS_ANIM_LOG=1` logs the numbers to add.

## Vicar Amelia's textures come out broken, as rainbow noise
- **Seen on:** rc7 (`v0.2.18-vk.1-18-gb369d66`), the Vicar Amelia fight, FSR 4.1.1 at Native AA
  (reported 2026-10-21). Not yet known whether stock Helia's Dream shows it.
- **What it looks like:** the boss's textures are rainbow noise: a texture sampled from memory
  that holds something else.
- **Likely family:** upstream fixed two of these before, both on the blood layers characters
  get when hit. One was a 512x512 blood mask whose mip levels were lost when its surface was
  replaced (host/textures.cpp `carry_surface_locked`). The other was a 256x256 blood layer
  placed inside an older render target, whose clear wiped the wrong image (host/render.cpp
  `fill_starts_inside`). Amelia is a large, heavily bloodied character, which fits.
- **heliasdream-vk changes that could be involved:** each can be turned off without a rebuild. All
  of them change how shaders read memory, and none touches the texture cache:
  - `HELIAS_CS_CB` (compute constants through bindings, rc5b);
  - `HELIAS_FETCH_INLINE` and `HELIAS_SOFFSET_BOUND` (rc4);
  - `HELIAS_TABLES_VRAM` (rc5);
  - `HELIAS_CP_STAGE_CACHE` (rc7);
  - `HELIAS_MOTION_BLUR_SKIP`.

  The scene split and post-processing at render size were not active (Native AA).
- **To narrow it down:**
  1. While it's on screen, press **F12**. Stock Helia's Dream writes the frame's render targets and
     its draw list to `logs/f12-*`.
  2. Run with all of the above off:
     `HELIAS_CS_CB=0 HELIAS_FETCH_INLINE=0 HELIAS_SOFFSET_BOUND=0 HELIAS_TABLES_VRAM=0
     HELIAS_CP_STAGE_CACHE=0 HELIAS_MOTION_BLUR_SKIP=0 ./bbtest start amelia-off`.
     - If it's gone, halve the list until one switch is left.
     - If it stays, try stock Helia's Dream (`heliasdream.bin.official`). If stock shows it too, it's
       upstream's, and it should be reported there with the F12 dump.

## Menus stop responding after a resolution change in the game's own Settings menu
- **Seen on:** upstream Helia's Dream too, before heliasdream-vk's changes (reported 2026-10-18).
- **Confirmed by the user (2026-10-18):** it happens **only** through the game's own
  Settings menu Resolution pick list; F10 resolution changes always worked. That matches
  cause 1 below (the pick list still up when the resize runs).
- **What happens:** in Bloodborne's own in-game Settings menu, with heliasdream's added PC entries
  (System > PC Graphics > Resolution), you pick a new resolution and the "keep this
  resolution?" yes/no question appears. From then on the menus take no input, while the game
  keeps running behind them. This is the game's menu, not the F10 overlay.
- **How it works (src/engine/live_resolution.cpp):** the change waits 1.5 s for the pick list to
  close (`kMenuSettleNs`), then resizes at the present step's quiet point. That rebuilds the
  scene targets, restarts YEBIS and sets every live Scaleform movie's viewport again. After
  that, `confirm_tick` opens the question modally on PC Graphics' own stack
  (`option_menu_confirm_ask`, engine/option_menu.cpp). If there is no answer within 10 s it
  answers NO itself, and 3 s later the change goes back regardless.
- **Code review (2026-10-18, static, without the game):** the menu path is
  unchanged from upstream. heliasdream-vk only added the scene-size step inside the
  same resize and the PC Upscaling screens' `live_resolution_scale_changed`.
  **Fixed in heliasdream-vk (branch `phase-5-test-release`, needs the game to verify):**
  1. *Resize under a pick list still on the stack, the most likely cause.* The
     change waited a fixed 1.5 s of wall time. A slow frame (a shader compile)
     or a held key kept the Resolution pick list up longer, so the resize
     rebuilt the targets and Scaleform viewports under it and left it open,
     taking the keys. The question then never went up either: it only goes up
     once the stack is back to its base (option_menu.cpp:1523). Now a change
     from the game's menus also waits until no popup is up on PC Graphics,
     PC Upscaling or Upscaling Quality, for at most 10 s after the request
     (live_resolution.cpp:~1000-1012, `option_menu_popup_open`). Only requests
     made by the System menu itself wait (it brackets its set with
     `live_resolution_from_game_menu(true/false)`); F10, the setup window and
     going back are exactly as before.
  2. *A question that was never shown came back later.* When PC Graphics
     closed, or the pick list stayed up, before the question could go up,
     its request stayed pending. The 10 s timeout ignored it
     (option_menu.cpp `option_menu_confirm_close` returned early), and the
     next visit to PC Graphics put up a question nobody was waiting for. The
     timeout now withdraws it.
  3. `g_confirm_open` was a plain bool written on the menu thread and read
     from the present step. It is now atomic.
  **Suspected, not changed (they need the game to confirm):**
  - live_resolution.cpp:904 / option_menu.cpp:2067. At 10 s the question is
    closed by tapping the menu's decide button. If the question does not
    have focus, the tap goes to the row under it (it can reopen the pick
    list), and the popup stays on PC Graphics' stack. 3 s later the change
    goes back anyway (live_resolution.cpp:908). From then on, option_menu
    believes its question is still open, and later questions are never put
    up until that popup closes.
  - option_menu.cpp:1518. If PC Graphics is left before the question goes
    up, there is no question, and the size goes back silently after about
    13 s (now without the stale question afterwards).
  - live_resolution.cpp:925 (the standalone MenuMan dialog used when PC
    Graphics is not open, e.g. a change from F10 that asks to be kept). The
    source itself notes that this dialog is not modal: the rows under it
    keep taking keys.
  - Scaleform movies get new viewports in the resize (`swf_viewports`). A
    movie built in the same frame as the resize may keep the old size, so
    mouse hit-testing on the question could be off while keys still work.
  - Threads: the present step reads the menu's popup stack and dialog
    vtables without a lock (as upstream's `option_menu_confirm_possible`
    did). A stale read only delays the change by a frame; nothing here
    blocks or deadlocks. The waits in the resize are bounded: 500 ms for
    two flips, then the command processor drain and GPU flush.
- **Not likely heliasdream-vk's:** the scene/UI split (Phase 1) only changes the scene's size in the
  same resize; upstream shows the bug without it. TESTING.md part G checks with and without
  `HELIAS_SCENE_SPLIT=0`. The log now says "waiting for the menu's pick list to close before
  the change" when fix 1 holds a change back.
- **Workaround:** change the resolution with F10 > Resolution or in the setup window, or wait
  about 13 s for the question to answer itself (NO) and close.
