#!/usr/bin/env bash
# bbprofile.sh - guided profiling session for heliasdream-vk (profiling kit, after rc5b).
#
# Run it from the kit folder in a terminal (Konsole), with the game installed:
#   bash bbprofile.sh                 the whole session (~30 min), step by step
#   bash bbprofile.sh --list          the steps and what each measures
#   bash bbprofile.sh --resume        continue the last unfinished session
#   bash bbprofile.sh --only a2,m4    just these steps (new session)
#   bash bbprofile.sh --skip pgo      all steps but these
#   bash bbprofile.sh --bundle        (re)make the tarball of the last session
#   bash bbprofile.sh --dry-run       check the kit, the game folder and the tools; runs nothing
# Other options: --game DIR (default ~/BloodBorne PC/heliasdream-linux-v0.2.18),
#   --no-sudo (no perf scheduler traces, no dmidecode, sysctls left alone),
#   --keep-data (put the big perf .data files in the tarball too).
#
# What it does: asks for sudo once; installs perf if it is missing
# (sudo pacman -S --needed perf); sets kernel.perf_event_paranoid=1 and
# kernel.kptr_restrict=0 for the session; puts the kit's binaries in the game
# folder as heliasdream.bin one step at a time (the original is kept as
# heliasdream.bin.bbprofile-orig); starts the game with each step's variables; tells
# you what to do in the game; collects the logs, snapshots, perf data and
# profiles; at the end (also on Ctrl-C) puts the original heliasdream.bin and the
# sysctls back and writes ONE tarball, ~/bbprofile-<date>-<time>.tar.gz.
# Nothing is installed besides perf; the game's files are not changed besides
# heliasdream.bin (restored) and a new pgo/ folder.
set -uo pipefail
shopt -s nullglob

KIT=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
GAME=${BBPROFILE_GAME:-"$HOME/BloodBorne PC/heliasdream-linux-v0.2.18"}
WORKROOT=${BBPROFILE_WORK:-"$HOME/bbprofile-work"}
OUTDIR=${BBPROFILE_OUT:-"$HOME"}
# Test hooks (box-side dry runs only): another launcher, auto-answers, short waits.
LAUNCH=${BBPROFILE_LAUNCH:-./heliasdream}
AUTO=${BBPROFILE_AUTO:-0}
QUICK=${BBPROFILE_QUICK:-0}

PROF_BIN=$KIT/heliasdream-prof
PGO_BIN=$KIT/heliasdream-pgogen
RC5B_SHA=f1a7d83a456b822fbbcb3ca2a8f588b24f08ff3b0dd3a1f9aa4f6f05c8f9b21b  # rc5b heliasdream.bin (v0.2.18-vk.1-6-g7b65cd0)

ALL_STEPS=(sys a1 a2 a3 a4 m4 pgo a5 a6)
declare -A TITLE=(
  [sys]="System facts (RAM speed/EXPO, power profile, governor/EPP, GPU, display)"
  [a1]="A1 plain + CPU profile (Old Yharnam spot)"
  [a2]="A2 HELIAS_TABLES_VRAM=0 (Old Yharnam spot)"
  [a3]="A3 HELIAS_PARAMS_WC=1 (Old Yharnam spot)"
  [a4]="A4 HELIAS_PARAMS_VRAM=1 (Old Yharnam spot)"
  [m4]="M4 the periodic ~12 ms frame: flip trace + perf sched (Old Yharnam spot)"
  [pgo]="PGO training run (instrumented binary: Old Yharnam, Hunter's Dream)"
  [a5]="A5 HELIAS_PS_WAVE32=32 (Old Yharnam spot)"
  [a6]="A6 plain again: drift check (Old Yharnam spot, optional)"
)
# Estimated seconds a step takes (starting the game and loading in ~70 s included).
declare -A EST=([sys]=60 [a1]=270 [a2]=150 [a3]=150 [a4]=150 [m4]=210 [pgo]=360 [a5]=150 [a6]=150)
BUNDLE_EST=180
mins() { awk -v s="$1" 'BEGIN{printf "%.1f", s/60}'; }
declare -A DECIDES=(
  [sys]="context for every number (RAM at EXPO speed? power profile, EPP, ReBAR, GPU power cap)"
  [a1]="the baseline for every A/B; where bb-cp0's ~5 us/draw goes (DWARF call graphs) -> the per-stage memo / bb-cp0 split items"
  [a2]="item 1: is the GPU's idle-with-work-queued (~0.5 ms/frame) caused by page tables in VRAM?"
  [a3]="item 1: params ring write-combined vs cached (does WC cut the GPU's reads / queued idle?)"
  [a4]="item 1: params ring in ReBAR VRAM (is moving it worth doing by default?)"
  [m4]="item 2: who makes the ~1/s 12.4 ms frame - game thread, CP walk, back-pressure, or the scheduler"
  [pgo]="item 6: profile for a PGO-optimised build (merged on the box with llvm-profdata 19)"
  [a5]="item 7: wave32 pixel shaders on RDNA4 - faster or slower?"
  [a6]="whether the A/B differences are bigger than the run-to-run drift"
)

die() { echo "bbprofile: $*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }
hr() { printf '%s\n' "------------------------------------------------------------------------"; }
bold() { if [ -t 1 ]; then printf '\033[1m%s\033[0m\n' "$*"; else printf '%s\n' "$*"; fi; }
stamp() { date '+%H:%M:%S'; }
# Wall and CLOCK_MONOTONIC (what heliasdream's steady clock and perf -k CLOCK_MONOTONIC use).
mono() { python3 -c 'import time; print("%.6f" % time.clock_gettime(time.CLOCK_MONOTONIC))' 2> /dev/null || echo "?"; }
event() { [ -n "${SD:-}" ] && printf '%s %s %s\n' "$(date +%s.%N)" "$(mono)" "$*" >> "$SD/events.txt"; }
chime() {
  local s
  for s in /usr/share/sounds/ocean/stereo/completion-success.oga /usr/share/sounds/freedesktop/stereo/complete.oga \
           /usr/share/sounds/ocean/stereo/message-new-instant.oga; do
    if [ -f "$s" ] && command -v paplay > /dev/null; then (paplay "$s" > /dev/null 2>&1 &); return; fi
  done
  printf '\a'
}
ask() {  # ask PROMPT -> sets REPLY (AUTO answers Enter)
  if [ "$AUTO" = 1 ]; then REPLY=""; say "$1 [auto]"; sleep 0.2; return 0; fi
  read -r -p "$1" REPLY || REPLY=q
}
key1() {  # key1: 0 if Enter was pressed within 1 s (sleeps instead when stdin is closed)
  local rc
  read -r -t 1 _; rc=$?
  [ $rc = 0 ] && return 0
  [ $rc -le 128 ] && sleep 1
  return 1
}
secs() { if [ "$QUICK" = 1 ]; then echo $(( ($1 + 9) / 10 )); else echo "$1"; fi; }
sha() { sha256sum "$1" 2> /dev/null | cut -d' ' -f1; }

usage() { sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; }

# ---------------------------------------------------------------- options
MODE=run; ONLY=""; SKIP=""; NOSUDO=0; KEEPDATA=0; RESUME_DIR=""
while [ $# -gt 0 ]; do
  case "$1" in
    --resume) MODE=resume; if [ $# -gt 1 ] && [ -d "$2" ]; then RESUME_DIR=$2; shift; fi ;;
    --only) ONLY=$2; shift ;;
    --skip) SKIP=$2; shift ;;
    --list) MODE=list ;;
    --bundle) MODE=bundle; if [ $# -gt 1 ] && [ -d "$2" ]; then RESUME_DIR=$2; shift; fi ;;
    --dry-run) MODE=dry ;;
    --game) GAME=$2; shift ;;
    --no-sudo) NOSUDO=1 ;;
    --keep-data) KEEPDATA=1 ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option $1 (see --help)" ;;
  esac
  shift
done

list_steps() {
  local s total=0
  for s in "${ALL_STEPS[@]}"; do
    printf '  %-4s ~%s min  %s\n         decides: %s\n' "$s" "$(mins "${EST[$s]}")" "${TITLE[$s]}" "${DECIDES[$s]}"
    total=$((total + EST[$s]))
  done
  printf '  then the tarball: ~%s min (perf reports).  Total ~%s min; ~%s min without the optional a6.\n' \
    "$(mins $BUNDLE_EST)" "$(mins $((total + BUNDLE_EST)))" "$(mins $((total + BUNDLE_EST - EST[a6])))"
}
if [ "$MODE" = list ]; then list_steps; exit 0; fi

wanted() {  # wanted STEP: in --only (if given) and not in --skip
  local s=$1
  if [ -n "$ONLY" ]; then case ",$ONLY," in *",$s,"*) ;; *) return 1 ;; esac; fi
  case ",$SKIP," in *",$s,"*) return 1 ;; esac
  return 0
}

# ---------------------------------------------------------------- checks
check_kit() {
  local ok=1 f
  for f in "$PROF_BIN" "$PGO_BIN" "$KIT/SHA256SUMS"; do [ -f "$f" ] || { say "missing in the kit: $f"; ok=0; }; done
  [ $ok = 1 ] || return 1
  if ! (cd "$KIT" && sha256sum --quiet -c SHA256SUMS 2> /dev/null); then
    say "the kit's files do not match SHA256SUMS (a broken download?):"; (cd "$KIT" && sha256sum -c SHA256SUMS 2>&1 | grep -v ': OK$'); return 1
  fi
  say "kit: binaries verified (SHA256SUMS)"
}
check_game() {
  [ -d "$GAME" ] || { say "game folder not found: $GAME (use --game DIR)"; return 1; }
  [ -f "$GAME/heliasdream.bin" ] || [ -f "$GAME/heliasdream.bin.bbprofile-orig" ] || { say "no heliasdream.bin in $GAME"; return 1; }
  [ -x "$GAME/heliasdream" ] || [ "$LAUNCH" != ./heliasdream ] || { say "no ./heliasdream wrapper in $GAME"; return 1; }
  say "game: $GAME"
}
check_tools() {
  local t miss=""
  for t in sha256sum tar gzip python3 pgrep awk sed; do command -v "$t" > /dev/null || miss="$miss $t"; done
  [ -z "$miss" ] || { say "missing tools:$miss"; return 1; }
  if command -v perf > /dev/null; then say "perf: $(perf --version 2> /dev/null)"; else say "perf: not installed (the session installs it: sudo pacman -S --needed perf)"; fi
}

# ---------------------------------------------------------------- binary swap
ORIG_SHA=""
binary_state() {  # recover from an earlier session that died before restoring
  local cur prof pgo
  prof=$(sha "$PROF_BIN"); pgo=$(sha "$PGO_BIN")
  if [ -f "$GAME/heliasdream.bin.bbprofile-orig" ]; then
    cur=$(sha "$GAME/heliasdream.bin")
    if [ ! -f "$GAME/heliasdream.bin" ] || [ "$cur" = "$prof" ] || [ "$cur" = "$pgo" ]; then
      say "an earlier session left a kit binary in place; putting the original back first"
      mv -f "$GAME/heliasdream.bin.bbprofile-orig" "$GAME/heliasdream.bin" || return 1
    else
      say "note: $GAME/heliasdream.bin.bbprofile-orig is from an earlier session and heliasdream.bin is not a kit binary;"
      say "      keeping the current heliasdream.bin as the original (the old backup becomes heliasdream.bin.bbprofile-orig.old)"
      mv -f "$GAME/heliasdream.bin.bbprofile-orig" "$GAME/heliasdream.bin.bbprofile-orig.old" || return 1
    fi
  fi
  ORIG_SHA=$(sha "$GAME/heliasdream.bin")
  if [ "$ORIG_SHA" = "$RC5B_SHA" ]; then say "installed heliasdream.bin: rc5b (v0.2.18-vk.1-6-g7b65cd0), as the kit expects"
  else say "note: the installed heliasdream.bin is not rc5b's (sha256 $ORIG_SHA); the kit's binaries are rc5b + instrumentation and use the folder's data as is"; fi
}
install_bin() {  # install_bin SRC: SRC becomes the game's heliasdream.bin (original kept once)
  local src=$1
  pgrep -x heliasdream.bin > /dev/null && { say "heliasdream.bin is running: close the game first"; return 1; }
  if [ ! -f "$GAME/heliasdream.bin.bbprofile-orig" ]; then
    cp -p "$GAME/heliasdream.bin" "$GAME/heliasdream.bin.bbprofile-orig" || return 1
  fi
  cp "$src" "$GAME/.heliasdream.bin.bbprofile-tmp" && chmod 755 "$GAME/.heliasdream.bin.bbprofile-tmp" &&
    mv -f "$GAME/.heliasdream.bin.bbprofile-tmp" "$GAME/heliasdream.bin" || return 1
  [ "$(sha "$GAME/heliasdream.bin")" = "$(sha "$src")" ] || { say "copying $src failed"; return 1; }
}
restore_bin() {
  [ -f "$GAME/heliasdream.bin.bbprofile-orig" ] || return 0
  if mv -f "$GAME/heliasdream.bin.bbprofile-orig" "$GAME/heliasdream.bin"; then
    if [ -n "$ORIG_SHA" ] && [ "$(sha "$GAME/heliasdream.bin")" != "$ORIG_SHA" ]; then
      say "WARNING: the restored heliasdream.bin's sha256 differs from the one found at the start"
    else
      say "restored the original heliasdream.bin"
    fi
  else
    say "WARNING: could not restore $GAME/heliasdream.bin from heliasdream.bin.bbprofile-orig: do it by hand:"
    say "  mv -f \"$GAME/heliasdream.bin.bbprofile-orig\" \"$GAME/heliasdream.bin\""
  fi
}

# ---------------------------------------------------------------- sudo, sysctl
SUDO_OK=0; KEEPALIVE_PID=""; SYSCTL_SAVE="$WORKROOT/.sysctl-orig"
sudo_begin() {
  [ "$NOSUDO" = 1 ] && { say "--no-sudo: no scheduler traces, no dmidecode, perf limited to what paranoid=$(cat /proc/sys/kernel/perf_event_paranoid) allows"; return 0; }
  say "sudo is needed for: installing perf if missing, the perf sysctls (restored at the end), dmidecode (RAM speed), perf sched."
  if sudo -v; then
    SUDO_OK=1
    ( while kill -0 $$ 2> /dev/null; do sudo -n -v 2> /dev/null; sleep 50; done ) &
    KEEPALIVE_PID=$!
  else
    say "no sudo: continuing without the parts that need it"
  fi
}
perf_install() {
  command -v perf > /dev/null && return 0
  [ "$SUDO_OK" = 1 ] || { say "perf is missing and there is no sudo: the CPU-profile and perf sched parts are skipped"; return 1; }
  say "installing perf: sudo pacman -S --needed perf"
  sudo pacman -S --needed perf || { say "perf install failed: the perf parts are skipped"; return 1; }
  command -v perf > /dev/null
}
sysctl_begin() {
  [ "$SUDO_OK" = 1 ] || return 0
  mkdir -p "$WORKROOT"
  if [ ! -f "$SYSCTL_SAVE" ]; then  # an earlier session's file means it never restored: keep its originals
    printf '%s %s\n' "$(cat /proc/sys/kernel/perf_event_paranoid)" "$(cat /proc/sys/kernel/kptr_restrict)" > "$SYSCTL_SAVE"
  fi
  sudo -n sysctl -q -w kernel.perf_event_paranoid=1 kernel.kptr_restrict=0 > /dev/null &&
    say "sysctl for this session: kernel.perf_event_paranoid=1 kernel.kptr_restrict=0 (originals $(cat "$SYSCTL_SAVE") are restored at the end)"
}
sysctl_restore() {
  [ -f "$SYSCTL_SAVE" ] || return 0
  local p k
  read -r p k < "$SYSCTL_SAVE"
  if sudo -n sysctl -q -w "kernel.perf_event_paranoid=$p" "kernel.kptr_restrict=$k" > /dev/null 2>&1 ||
     sudo sysctl -q -w "kernel.perf_event_paranoid=$p" "kernel.kptr_restrict=$k" > /dev/null; then
    rm -f "$SYSCTL_SAVE"; say "restored kernel.perf_event_paranoid=$p kernel.kptr_restrict=$k"
  else
    say "WARNING: could not restore the sysctls: sudo sysctl -w kernel.perf_event_paranoid=$p kernel.kptr_restrict=$k"
  fi
}

# ---------------------------------------------------------------- game process
GAME_PID=""; SYSMON_PID=""; BGPERF_PIDS=()
game_alive() { [ -n "$GAME_PID" ] && kill -0 "$GAME_PID" 2> /dev/null; }
launch() {  # launch BIN VAR=VALUE...: start the game with these variables, output to $SD/game.log
  local bin=$1; shift
  install_bin "$bin" || return 1
  mkdir -p "$SD/snaps"
  { echo "binary: $(basename "$bin") sha256 $(sha "$bin")"; printf 'env: %s\n' "$@"; } > "$SD/run.txt"
  # As run-heliasdream.sh: the game folder's heliasdream.toml (or $HELIAS_CONFIG) on top of the per-user config.
  local cfg=${HELIAS_CONFIG:-heliasdream.toml} args=()
  if [ -f "$GAME/$cfg" ] || { [ "${cfg:0:1}" = / ] && [ -f "$cfg" ]; }; then args=(--config "$cfg"); fi
  echo "args: ${args[*]}" >> "$SD/run.txt"
  event "launch $* ${args[*]}"
  # rc6: the log on tmpfs while the game runs (a symlink in the step folder;
  # log_settle moves it there at the exit): a write to game.log on btrfs during
  # writeback held GXRenderThread 3-5 ms in rc5's m4. BBPROFILE_LOGDIR=DIR
  # another folder; BBPROFILE_LOGDIR=- the step folder itself, as before.
  local out=$SD/game.log logdir=${BBPROFILE_LOGDIR:-${XDG_RUNTIME_DIR:-/dev/shm}}
  LOGTMP=""
  if [ "$logdir" != - ] && [ -d "$logdir" ] && [ -w "$logdir" ]; then
    LOGTMP=$logdir/bbprofile-$SESSION-$STEP.log
    rm -f "$SD/game.log"; ln -s "$LOGTMP" "$SD/game.log"; out=$LOGTMP
    echo "log: $LOGTMP while running" >> "$SD/run.txt"
  fi
  ( cd "$GAME" && exec env HELIAS_SNAP_DIR="$SD/snaps" "$@" "$LAUNCH" "${args[@]}" ) > "$out" 2>&1 &
  GAME_PID=$!
  sleep 3
  if ! game_alive; then
    say "the game exited right away; the end of its log:"; tail -15 "$SD/game.log"; GAME_PID=""; return 1
  fi
  sysmon_start
  say "[$(stamp)] game started (pid $GAME_PID, log $SD/game.log)"
}
LOGTMP=""
log_settle() {  # the tmpfs log into the step folder (after the game exited)
  [ -n "${LOGTMP:-}" ] && [ -f "$LOGTMP" ] || return 0
  if [ -L "$SD/game.log" ]; then rm -f "$SD/game.log"; fi
  mv -f "$LOGTMP" "$SD/game.log" 2> /dev/null || cp -f "$LOGTMP" "$SD/game.log"
  LOGTMP=""
}
game_stop() {  # ask the player to close the game, or close it on Enter
  game_alive || { GAME_PID=""; return 0; }
  chime
  bold ">>> Done here. Close the game window now (or press Enter and the script closes it)."
  while game_alive; do
    if [ "$AUTO" = 1 ]; then sleep 1; kill -TERM "$GAME_PID" 2> /dev/null; break; fi
    if key1; then kill -TERM "$GAME_PID" 2> /dev/null; break; fi
  done
  game_wait 25
}
game_wait() {  # wait up to N s for the exit, then kill
  local i
  for ((i = 0; i < $1 * 2; i++)); do game_alive || break; sleep 0.5; done
  if game_alive; then say "the game did not exit; killing it"; kill -KILL "$GAME_PID" 2> /dev/null; sleep 1; fi
  wait "$GAME_PID" 2> /dev/null; echo "exit code $?" >> "$SD/run.txt"
  log_settle
  event "exited"
  GAME_PID=""
  sysmon_stop
}
wait_game_exit() {  # the player closes it themselves (no prompt)
  while game_alive; do sleep 1; done
  game_wait 1
}

# 1 Hz: GPU busy/clocks/power/temps, CPU MHz avg/max, Tctl, the game's CPU% (sysfs reads only).
GPUDEV=""
find_gpu() {
  local d
  for d in /sys/class/drm/card[0-9]*/device; do
    [ "$(cat "$d/vendor" 2> /dev/null)" = 0x1002 ] && [ -e "$d/gpu_busy_percent" ] && { GPUDEV=$d; return 0; }
  done
  return 1
}
sysmon_loop() {
  local out=$1 pid=$2 hw="" k10="" f v t0 ncpu sum max mhz ticks prev_ticks="" prev_t="" now busy sclk mclk pw te tj tm gpct
  [ -n "$GPUDEV" ] && for f in "$GPUDEV"/hwmon/hwmon*; do hw=$f; done
  for f in /sys/class/hwmon/hwmon*; do [ "$(cat "$f/name" 2> /dev/null)" = k10temp ] && k10=$f; done
  local -a st
  echo "epoch,cpu_mhz_avg,cpu_mhz_max,tctl_c,game_cpu_pct,gpu_busy_pct,gpu_sclk_mhz,gpu_mclk_mhz,gpu_power_w,gpu_edge_c,gpu_junction_c,gpu_mem_c" > "$out"
  while kill -0 "$pid" 2> /dev/null; do
    now=$(date +%s.%N); sum=0; max=0; ncpu=0
    for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_cur_freq; do
      read -r v < "$f" 2> /dev/null || continue; sum=$((sum + v)); ncpu=$((ncpu + 1)); [ "$v" -gt "$max" ] && max=$v
    done
    [ $ncpu -gt 0 ] && mhz=$((sum / ncpu / 1000)) || mhz=""
    t0=""; [ -n "$k10" ] && read -r t0 < "$k10/temp1_input" 2> /dev/null && t0=$((t0 / 1000))
    gpct=""
    if read -r -a st < "/proc/$pid/stat" 2> /dev/null; then
      ticks=$((st[13] + st[14]))
      if [ -n "$prev_ticks" ]; then gpct=$(awk -v a="$ticks" -v b="$prev_ticks" -v t="$now" -v p="$prev_t" 'BEGIN{d=t-p; if(d>0) printf "%.0f", (a-b)/d}'); fi
      prev_ticks=$ticks; prev_t=$now
    fi
    busy=""; sclk=""; mclk=""; pw=""; te=""; tj=""; tm=""
    if [ -n "$GPUDEV" ]; then
      read -r busy < "$GPUDEV/gpu_busy_percent" 2> /dev/null
      if [ -n "$hw" ]; then
        read -r v < "$hw/freq1_input" 2> /dev/null && sclk=$((v / 1000000))
        read -r v < "$hw/freq2_input" 2> /dev/null && mclk=$((v / 1000000))
        { read -r v < "$hw/power1_average" || read -r v < "$hw/power1_input"; } 2> /dev/null && pw=$((v / 1000000))
        read -r v < "$hw/temp1_input" 2> /dev/null && te=$((v / 1000))
        read -r v < "$hw/temp2_input" 2> /dev/null && tj=$((v / 1000))
        read -r v < "$hw/temp3_input" 2> /dev/null && tm=$((v / 1000))
      fi
    fi
    echo "$now,$mhz,$((max / 1000)),$t0,$gpct,$busy,$sclk,$mclk,$pw,$te,$tj,$tm" >> "$out"
    sleep 1
  done
}
sysmon_start() { find_gpu; sysmon_loop "$SD/sysmon.csv" "$GAME_PID" & SYSMON_PID=$!; }
sysmon_stop() { [ -n "$SYSMON_PID" ] && { kill "$SYSMON_PID" 2> /dev/null; wait "$SYSMON_PID" 2> /dev/null; }; SYSMON_PID=""; }

# ---------------------------------------------------------------- snapshots
snap_count() { local a=("$SD"/snaps/snap-*.txt); echo ${#a[@]}; }
wait_arrival() {  # wait_arrival [PROMPT]: the player presses F11 (a snapshot) or Enter here
  local n0 i=0
  n0=$(snap_count)
  if [ $# -gt 0 ]; then bold ">>> $1"; else
    bold ">>> When you stand at the spot, press F11 once (in the game). Then do not touch anything."
    say "    (or press Enter here when you are there)"
  fi
  while game_alive; do
    if [ "$(snap_count)" -gt "$n0" ]; then break; fi
    if [ "$AUTO" = 1 ] && [ $i -ge 4 ]; then break; fi
    if key1; then break; fi
    i=$((i + 1))
  done
  game_alive || { say "the game exited"; return 1; }
  event "arrived ${1:-at the spot}"
  if [ $# -gt 0 ]; then say "[$(stamp)] ok"; else say "[$(stamp)] at the spot: stand still (camera too) until told otherwise"; fi
}
snap_request() {  # snap_request NOTE: one snapshot of the last 30 s, waits until it is written
  local n0 i
  n0=$(snap_count)
  echo "$1" > "$SD/snaps/snap.request"
  event "snap.request $1"
  for ((i = 0; i < 30; i++)); do
    [ "$(snap_count)" -gt "$n0" ] && { say "[$(stamp)] snapshot saved ($1)"; return 0; }
    game_alive || return 1
    sleep 0.5
  done
  rm -f "$SD/snaps/snap.request"
  bold ">>> The snapshot request was not picked up: press F11 in the game now."
  for ((i = 0; i < 30; i++)); do [ "$(snap_count)" -gt "$n0" ] && return 0; sleep 0.5; done
  say "no snapshot for $1"; return 1
}
countdown() {  # countdown SECONDS LABEL
  local s=$1 left
  while [ "$s" -gt 0 ]; do
    game_alive || return 1
    left=$s; printf '\r    %s: %3d s left ' "$2" "$left"; sleep 1; s=$((s - 1))
  done
  printf '\r%60s\r' ""
}
measure_two() {  # two back-to-back 30 s windows after a 5 s settle (60 s of frames)
  countdown "$(secs 35)" "first 30 s window" || return 1
  snap_request "$STEP window 1" || true
  gpuq_record_bg "$(secs 8)"  # rc6: in the second window of every step
  countdown "$(secs 30)" "second 30 s window" || { bgperf_wait; return 1; }
  snap_request "$STEP window 2" || true
  bgperf_wait
}

spot_text() {
  say "  Where: Old Yharnam, outdoors - the heavy spot of the rc5 snapshot 100844 (the one you"
  say "  always use there). Same place and the same camera direction every run, no enemies"
  say "  aggroed. Tip: if the game was last saved there it loads you right at the spot."
}

# ---------------------------------------------------------------- perf
PERF_OK=0
tids_of() {  # tids_of NAME...: comma-separated tids of the game's threads with these names
  local t n out="" want
  for t in /proc/"$GAME_PID"/task/*; do
    read -r n < "$t/comm" 2> /dev/null || continue
    for want in "$@"; do [ "$n" = "$want" ] && out="$out,${t##*/}"; done
  done
  echo "${out#,}"
}
threads_dump() {
  local t
  for t in /proc/"$GAME_PID"/task/*; do
    printf '%s %s %s\n' "${t##*/}" "$(cat "$t/comm" 2> /dev/null)" "$(awk '{print "cpu="$39" utime="$14" stime="$15}' "$t/stat" 2> /dev/null)"
  done
}
cpu_profile() {  # the A1 CPU profile (the game keeps running at the spot)
  [ "$PERF_OK" = 1 ] || { say "no perf: CPU profile skipped"; return 0; }
  local P=$SD/perf cp0 hot d
  mkdir -p "$P"
  cp "/proc/$GAME_PID/maps" "$P/maps.txt"; threads_dump > "$P/threads-start.txt"
  taskset -pc "$GAME_PID" > "$P/affinity.txt" 2>&1
  cp0=$(tids_of bb-cp0)
  hot=$(tids_of heliasdream.bin GXRenderThread bb-record bb-submit bb-present bb-cp1)
  say "[$(stamp)] CPU profile: keep standing still (about $(( $(secs 15) + $(secs 30) + $(secs 20) + $(secs 12) + $(secs 8) + 10 )) s)"
  event "perf stat"
  d=$(secs 15); say "  1/5 perf stat per thread, $d s"
  perf stat --per-thread -p "$GAME_PID" \
    -e task-clock,context-switches,cpu-migrations,cycles,instructions,branch-misses,cache-misses,stalled-cycles-frontend,stalled-cycles-backend \
    -- sleep "$d" 2> "$P/stat-per-thread.txt" || true
  event "perf flat"
  d=$(secs 30); say "  2/5 flat profile of the whole process, $d s at 1999 Hz"
  perf record -q -F 1999 -p "$GAME_PID" -o "$P/cpu-flat.data" -- sleep "$d" 2> "$P/record-flat.log" || true
  if [ -n "$cp0" ]; then
    event "perf dwarf bb-cp0"
    d=$(secs 20); say "  3/5 call graphs of bb-cp0 (tid $cp0; DWARF unwinding), $d s at 799 Hz"
    perf record -q -F 799 --call-graph dwarf,16384 -t "$cp0" -o "$P/cp0-dwarf.data" -- sleep "$d" 2> "$P/record-cp0.log" || true
  else
    say "  3/5 no bb-cp0 thread found: skipped"
  fi
  if [ -n "$hot" ]; then
    event "perf dwarf hot chain"
    d=$(secs 12); say "  4/5 call graphs of the hot chain (game thread, GXRenderThread, bb-record, bb-submit, bb-present), $d s at 499 Hz"
    perf record -q -F 499 --call-graph dwarf,12288 -t "$hot" -o "$P/hot-dwarf.data" -- sleep "$d" 2> "$P/record-hot.log" || true
  fi
  if [ "$SUDO_OK" = 1 ]; then
    event "perf sched+gpu tracepoints"
    d=$(secs 8); say "  5/5 scheduler + GPU-scheduler tracepoints, system-wide, $d s (sudo)"
    local sev=(-e sched:sched_switch -e sched:sched_wakeup -e sched:sched_migrate_task)
    # shellcheck disable=SC2024  # the logs are ours on purpose
    sudo -n perf record -q -a -k CLOCK_MONOTONIC -o "$P/sched-gpu.data" "${sev[@]}" \
      -e 'gpu_scheduler:*' -e 'amdgpu:amdgpu_cs_ioctl' -e 'amdgpu:amdgpu_sched_run_job' \
      -- sleep "$d" 2> "$P/record-sched-gpu.log" ||
      { say "    (GPU tracepoints not available: scheduler events only)"
        sudo -n perf record -q -a -k CLOCK_MONOTONIC -o "$P/sched-gpu.data" "${sev[@]}" -- sleep "$d" 2>> "$P/record-sched-gpu.log" || true; }
    sudo -n chown "$(id -u):$(id -g)" "$P/sched-gpu.data" 2> /dev/null || true
  fi
  threads_dump > "$P/threads-end.txt"
  event "perf done"
}
gpuq_record_bg() {  # rc6: gpuq_record_bg SECONDS: the GPU scheduler's queue (job queued/run/done, CS ioctls), every step
  [ "$PERF_OK" = 1 ] && [ "$SUDO_OK" = 1 ] || return 0
  [ -n "$GAME_PID" ] || return 0
  [ -f "$SD/perf/gpu-queue.data" ] && return 0
  mkdir -p "$SD/perf"
  event "perf gpu queue $1 s"
  # shellcheck disable=SC2024  # the log is ours on purpose
  sudo -n perf record -q -a -k CLOCK_MONOTONIC -o "$SD/perf/gpu-queue.data" -e 'gpu_scheduler:*' -e 'amdgpu:amdgpu_cs_ioctl' \
    -e 'amdgpu:amdgpu_sched_run_job' -- sleep "$1" > "$SD/perf/record-gpu-queue.log" 2>&1 &
  BGPERF_PIDS+=($!)
}
sched_record_bg() {  # sched_record_bg SECONDS: system-wide perf sched record in the background
  if [ "$PERF_OK" != 1 ] || [ "$SUDO_OK" != 1 ]; then say "  (no perf/sudo: no perf sched trace)"; return 0; fi
  mkdir -p "$SD/perf"
  cp "/proc/$GAME_PID/maps" "$SD/perf/maps.txt"; threads_dump > "$SD/perf/threads-start.txt"
  event "perf sched record $1 s"
  # shellcheck disable=SC2024  # the log is ours on purpose
  sudo -n perf sched record -k CLOCK_MONOTONIC -o "$SD/perf/sched.data" -- sleep "$1" > "$SD/perf/record-sched.log" 2>&1 &
  BGPERF_PIDS+=($!)
}
bgperf_wait() {
  local p
  for p in "${BGPERF_PIDS[@]}"; do wait "$p" 2> /dev/null; done
  BGPERF_PIDS=()
  [ -f "$SD/perf/sched.data" ] && sudo -n chown "$(id -u):$(id -g)" "$SD/perf/sched.data" 2> /dev/null
  [ -f "$SD/perf/gpu-queue.data" ] && sudo -n chown "$(id -u):$(id -g)" "$SD/perf/gpu-queue.data" 2> /dev/null
  return 0
}

# ---------------------------------------------------------------- steps
step_sys() {
  local S=$SD d c
  say "Collecting system facts (about 20 s)..."
  {
    echo "date: $(date)"; uname -a; cat /etc/os-release 2> /dev/null; echo "cmdline: $(cat /proc/cmdline)"
    echo "kit: $(basename "$KIT")"; echo "game heliasdream.bin sha256 (original): $ORIG_SHA"
  } > "$S/os.txt"
  lscpu > "$S/lscpu.txt" 2>&1
  {
    for f in scaling_driver scaling_governor energy_performance_preference scaling_min_freq scaling_max_freq cpuinfo_max_freq; do
      echo "cpu0 $f: $(cat /sys/devices/system/cpu/cpu0/cpufreq/$f 2> /dev/null)"
    done
    echo "governors (all cpus): $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2> /dev/null | sort | uniq -c | tr '\n' ' ')"
    echo "epp (all cpus): $(cat /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference 2> /dev/null | sort | uniq -c | tr '\n' ' ')"
    echo "amd_pstate status: $(cat /sys/devices/system/cpu/amd_pstate/status 2> /dev/null)"
    echo "amd_pstate prefcore: $(cat /sys/devices/system/cpu/amd_pstate/prefcore 2> /dev/null)"
    echo "boost: $(cat /sys/devices/system/cpu/cpufreq/boost 2> /dev/null)"
    echo "platform_profile: $(cat /sys/firmware/acpi/platform_profile 2> /dev/null)"
    echo "power-profiles-daemon: $(powerprofilesctl get 2> /dev/null || echo none)"
    echo "tuned: $(tuned-adm active 2> /dev/null || echo none)"
    echo "thp: $(cat /sys/kernel/mm/transparent_hugepage/enabled) defrag: $(cat /sys/kernel/mm/transparent_hugepage/defrag)"
    echo "sched_ext: $(cat /sys/kernel/sched_ext/state 2> /dev/null) ops: $(cat /sys/kernel/sched_ext/root/ops 2> /dev/null)"
    for u in scx scx_loader ananicy-cpp gamemoded irqbalance power-profiles-daemon tuned; do
      echo "service $u: $(systemctl is-active "$u" 2> /dev/null) / user $(systemctl --user is-active "$u" 2> /dev/null)"
    done
    echo "swappiness: $(cat /proc/sys/vm/swappiness) max_map_count: $(cat /proc/sys/vm/max_map_count)"
    echo "perf_event_paranoid (now): $(cat /proc/sys/kernel/perf_event_paranoid) kptr_restrict: $(cat /proc/sys/kernel/kptr_restrict)"
    for c in /sys/devices/system/cpu/cpu[0-9]*; do
      echo "$(basename "$c") l3=$(cat "$c/cache/index3/shared_cpu_list" 2> /dev/null) smt=$(cat "$c/topology/thread_siblings_list" 2> /dev/null) prefcore=$(cat "$c/cpufreq/amd_pstate_prefcore_ranking" 2> /dev/null || cat "$c/acpi_cppc/highest_perf" 2> /dev/null) max=$(cat "$c/cpufreq/cpuinfo_max_freq" 2> /dev/null)"
    done
  } > "$S/cpu-power.txt" 2>&1
  { free -h; echo; cat /proc/meminfo; echo; zramctl 2> /dev/null; swapon --show 2> /dev/null; } > "$S/memory.txt" 2>&1
  if [ "$SUDO_OK" = 1 ]; then
    # Serial numbers and asset tags removed.
    sudo -n dmidecode -t memory 2> /dev/null | grep -viE 'serial number|asset tag' > "$S/dmidecode-memory.txt"
    sudo -n dmidecode -t bios -t baseboard 2> /dev/null | grep -viE 'serial number|asset tag|uuid' > "$S/dmidecode-board.txt"
  fi
  {
    if find_gpu; then
      echo "gpu sysfs: $GPUDEV"
      for f in power_dpm_force_performance_level pp_power_profile_mode pp_dpm_sclk pp_dpm_mclk pp_dpm_fclk pp_dpm_socclk \
               current_link_speed current_link_width max_link_speed max_link_width mem_info_vram_total mem_info_vis_vram_total \
               mem_info_vram_used vbios_version; do
        echo "== $f"; cat "$GPUDEV/$f" 2> /dev/null
      done
      for d in "$GPUDEV"/hwmon/hwmon*; do
        for f in power1_cap power1_cap_max power1_cap_default power1_average power1_input freq1_input freq2_input temp1_input temp2_input temp3_input fan1_input; do
          [ -e "$d/$f" ] && echo "$f: $(cat "$d/$f" 2> /dev/null)"
        done
      done
      local slot; slot=$(basename "$(readlink -f "$GPUDEV")")
      echo "== lspci $slot"
      if [ "$SUDO_OK" = 1 ]; then sudo -n lspci -vv -s "$slot" 2> /dev/null | grep -E 'VGA|Region|BAR|Resizable|LnkSta|LnkCap|Kernel driver'; else lspci -vv -s "$slot" 2> /dev/null; fi
    else
      echo "no amdgpu card found in /sys/class/drm"
    fi
  } > "$S/gpu.txt" 2>&1
  vulkaninfo --summary > "$S/vulkaninfo-summary.txt" 2>&1 || true
  { kscreen-doctor -o 2> /dev/null | sed 's/\x1b\[[0-9;]*m//g'; echo "active output: $(qdbus6 org.kde.KWin /KWin activeOutputName 2> /dev/null)"; } > "$S/display.txt" 2>&1
  pacman -Q 2> /dev/null | grep -E '^(linux[^ ]*|mesa[^ ]*|vulkan-radeon|lib32-vulkan-radeon|kwin|plasma-workspace|perf|scx[^ ]*|ananicy[^ ]*|power-profiles-daemon|sdl3|ffmpeg|llvm-libs|glibc|pipewire|amd-ucode|linux-firmware[^ ]*) ' > "$S/packages.txt"
  # heliasdream's settings, the [account] section and anything that looks like a credential removed.
  for c in "$HOME/.config/HeliasDream/HeliasDream.toml" "$GAME/heliasdream.toml"; do
    [ -f "$c" ] || continue
    awk 'BEGIN{skip=0} /^\[/{skip=($0 ~ /^\[account/)} !skip && !/(token|password|secret|psn|email)/' "$c" > "$S/$(echo "$c" | sed "s|$HOME|home|; s|/|_|g")"
  done
  ls -la "$GAME" > "$S/game-folder.txt" 2>&1
  say "system facts: $S"
}

step_ab() {  # step_ab VAR=VALUE... (none for plain)
  hr
  say "  What to do: start/continue the game, go to (or load at) the spot below, press F11 once,"
  say "  then stand still ~65 s (two more 'Snapshot saved' banners appear). Then close the game."
  spot_text
  [ "$STEP" = a1 ] && say "  This run also takes the CPU profile: after the snapshots keep standing ~95 s more."
  [ $# -gt 0 ] && say "  Variables this run: $*"
  ask "Press Enter to start the game (s = skip this step, q = stop for now): "
  case "$REPLY" in s|S) return 3 ;; q|Q) return 4 ;; esac
  launch "$PROF_BIN" "$@" || return 1
  wait_arrival || { wait_game_exit; return 1; }
  measure_two || { say "the game exited during the measurement"; wait_game_exit; return 1; }
  [ "$STEP" = a1 ] && cpu_profile
  game_stop
}
step_m4() {
  hr
  say "  The periodic ~12.4 ms frame (about once a second). Same spot, same routine: F11 at the"
  say "  spot, stand still ~75 s. A system-wide 'perf sched' trace runs for $(secs 22) s during the"
  say "  second window, and at the end two screenshots of the presented frame are saved"
  say "  (reference pictures for later image comparisons)."
  spot_text
  ask "Press Enter to start the game (s = skip, q = stop for now): "
  case "$REPLY" in s|S) return 3 ;; q|Q) return 4 ;; esac
  mkdir -p "$SD/captures"
  launch "$PROF_BIN" HELIAS_FLIP_TRACE=1 HELIAS_FLIP_TRACE_MS=11 HELIAS_FRAME_STATS=1 HELIAS_FRAME_DETAIL=1 \
    HELIAS_FRAME_SLOW_MS=11 HELIAS_WAIT_LOG=GXRender HELIAS_WAIT_LOG_MS=8 HELIAS_HLE_COUNT=1 \
    HELIAS_VK_CAPTURE=1 HELIAS_VK_CAPTURE_DIR="$SD/captures" || return 1
  wait_arrival || { wait_game_exit; return 1; }
  countdown "$(secs 35)" "first 30 s window" || { wait_game_exit; return 1; }
  snap_request "m4 window 1" || true
  sched_record_bg "$(secs 22)"
  gpuq_record_bg "$(secs 8)"
  countdown "$(secs 30)" "second 30 s window (perf sched running)" || { bgperf_wait; wait_game_exit; return 1; }
  snap_request "m4 window 2" || true
  bgperf_wait
  [ -d "$SD/perf" ] && threads_dump > "$SD/perf/threads-end.txt"
  say "[$(stamp)] two screenshots (keep still 5 s more)"
  event "captures"
  touch "$SD/captures/capture.request"; sleep 2.5; touch "$SD/captures/capture.request"; sleep 2.5
  game_stop
  local pngs=("$SD"/captures/*.png); say "screenshots saved: ${#pngs[@]}"
}
step_pgo() {
  hr
  say "  PGO training with the instrumented binary (the game runs slower here; that is expected,"
  say "  the numbers of this run are not compared with anything). Play normally:"
  say "   1. $(secs 120) s in Old Yharnam: walk around the area near the spot, fight a little, open"
  say "      the menu once."
  say "   2. Go to a lamp and travel to the Hunter's Dream: $(secs 90) s walking around there."
  say "   3. Travel back to Old Yharnam (so the next steps start at the spot), then close the game."
  say "  The profile is written every 30 s to the game folder's pgo/ and when the game closes."
  ask "Press Enter to start the game (s = skip, q = stop for now): "
  case "$REPLY" in s|S) return 3 ;; q|Q) return 4 ;; esac
  local pdir="$GAME/pgo/$SESSION"
  mkdir -p "$pdir"
  launch "$PGO_BIN" HELIAS_PGO_DIR="$pdir" HELIAS_PGO_DUMP_S=30 || return 1
  wait_arrival "Load into Old Yharnam; press F11 (or Enter here) when you are in control there." || wait_game_exit
  if game_alive; then
    countdown "$(secs 120)" "Old Yharnam: play" || true
    chime
    wait_arrival "Now travel to the Hunter's Dream (a lamp); press F11 (or Enter here) when you arrive." || true
    countdown "$(secs 90)" "Hunter's Dream: walk around" || true
    chime; bold ">>> Travel back to Old Yharnam, then close the game (or press Enter here when loaded)."
    game_stop
  fi
  mkdir -p "$SD/pgo"
  local f nprof=0
  for f in "$pdir"/*.profraw; do cp "$f" "$SD/pgo/"; nprof=$((nprof + 1)); done
  grep -h 'pgo:' "$SD/game.log" | tail -5
  if [ $nprof -gt 0 ]; then say "profiles: $nprof file(s) from $pdir, $(du -sh "$SD/pgo" | cut -f1)"; else say "WARNING: no .profraw in $pdir"; return 1; fi
}

run_step() {  # run_step ID -> 0 done, 3 skipped, 4 quit, other failed
  STEP=$1; SD=$WORK/steps/$1; mkdir -p "$SD"
  echo; hr; bold "[$(stamp)] Step $1: ${TITLE[$1]}  (~$(mins "${EST[$1]}") min)"
  say "  decides: ${DECIDES[$1]}"
  local rc=0
  case "$1" in
    sys) step_sys ;;
    a1) step_ab ;;
    a2) step_ab HELIAS_TABLES_VRAM=0 ;;
    a3) step_ab HELIAS_PARAMS_WC=1 ;;
    a4) step_ab HELIAS_PARAMS_VRAM=1 ;;
    m4) step_m4 ;;
    pgo) step_pgo ;;
    a5) step_ab HELIAS_PS_WAVE32=32 ;;
    a6) step_ab ;;
  esac
  rc=$?
  case $rc in
    0) touch "$SD/.done"; rm -f "$SD/.skipped"; say "[$(stamp)] step $1 done" ;;
    3) touch "$SD/.skipped"; say "step $1 skipped" ;;
    4) ;;
    *) say "[$(stamp)] step $1 did not complete (rc $rc); its files are kept. Retry later with: bash bbprofile.sh --resume" ;;
  esac
  return $rc
}

# ---------------------------------------------------------------- bundle
perf_reports() {  # perf_reports DIR: text reports and scripts from the .data files (box-side analysis reads these)
  local P=$1/perf
  [ -d "$P" ] && command -v perf > /dev/null || return 0
  say "  perf reports in $P"
  if [ -f "$P/cpu-flat.data" ]; then
    perf report -i "$P/cpu-flat.data" --stdio --no-children --sort comm,dso,sym --percent-limit 0.2 > "$P/flat-report.txt" 2> /dev/null
    perf script -i "$P/cpu-flat.data" -F comm,tid,ip,sym,dso 2> /dev/null > "$P/flat.script"
    python3 "$KIT/tools/perf_cpu_summary.py" "$P/flat.script" "$P/maps.txt" > "$P/summary.txt" 2>&1
    gzip -f "$P/flat.script"
  fi
  local d
  for d in cp0 hot; do
    [ -f "$P/$d-dwarf.data" ] || continue
    perf report -i "$P/$d-dwarf.data" --stdio --children --sort comm,sym --percent-limit 0.5 --no-inline > "$P/$d-dwarf-report.txt" 2> /dev/null
    perf report -i "$P/$d-dwarf.data" --stdio --no-children -G --percent-limit 0.5 --no-inline > "$P/$d-dwarf-callers.txt" 2> /dev/null
    perf script -i "$P/$d-dwarf.data" -F comm,tid,time,ip,sym,dso --no-inline 2> /dev/null | gzip > "$P/$d-dwarf.script.gz"
  done
  if [ -f "$P/sched-gpu.data" ]; then
    perf script -i "$P/sched-gpu.data" -F comm,tid,cpu,time,event,trace 2> /dev/null | gzip > "$P/sched-gpu.script.gz"
  fi
  if [ -f "$P/sched.data" ]; then
    perf sched timehist -i "$P/sched.data" -s > "$P/sched-timehist-summary.txt" 2>&1
    perf sched latency -i "$P/sched.data" --sort max > "$P/sched-latency.txt" 2>&1
    { perf sched timehist -i "$P/sched.data" -w -n --state 2> /dev/null || perf sched timehist -i "$P/sched.data" 2> /dev/null; } | gzip > "$P/sched-timehist.txt.gz"
  fi
}
bundle() {
  local s out ex=()
  echo; hr; bold "[$(stamp)] Making the tarball (perf reports first; a few minutes)"
  for s in "$WORK"/steps/*; do perf_reports "$s"; done
  {
    echo "session: $SESSION"; echo "kit: $KIT"; echo "game: $GAME"; echo "made: $(date)"
    for s in "${ALL_STEPS[@]}"; do
      if [ -f "$WORK/steps/$s/.done" ]; then echo "$s: done"; elif [ -f "$WORK/steps/$s/.skipped" ]; then echo "$s: skipped"
      elif [ -d "$WORK/steps/$s" ]; then echo "$s: incomplete"; else echo "$s: not run"; fi
    done
    (cd "$KIT" && cat SHA256SUMS)
  } > "$WORK/session.txt"
  [ "$KEEPDATA" = 1 ] || ex=(--exclude='*.data' --exclude='*.data.old')
  out="$OUTDIR/bbprofile-$SESSION.tar.gz"
  tar -C "$WORKROOT" "${ex[@]}" -czf "$out" "$SESSION" || { say "tar failed"; return 1; }
  touch "$WORK/.bundled"
  echo; hr
  bold "Tarball: $out ($(du -h "$out" | cut -f1))"
  say "Send that one file. (The full perf .data files stay in $WORK; delete the folder when done.)"
}

# ---------------------------------------------------------------- cleanup
CLEANED=0
cleanup() {
  [ "$CLEANED" = 1 ] && return; CLEANED=1
  trap '' INT TERM HUP
  if game_alive; then say; say "closing the game..."; kill -TERM "$GAME_PID" 2> /dev/null; game_wait 15; fi
  sysmon_stop
  log_settle
  local p
  for p in "${BGPERF_PIDS[@]}"; do wait "$p" 2> /dev/null; done
  if [ -n "$GAME" ] && [ -d "$GAME" ]; then restore_bin; fi
  sysctl_restore
  [ -n "$KEEPALIVE_PID" ] && kill "$KEEPALIVE_PID" 2> /dev/null
  rm -f "$GAME/.heliasdream.bin.bbprofile-tmp" 2> /dev/null
}

# ---------------------------------------------------------------- main
latest_session() { local d last=""; for d in "$WORKROOT"/2*; do [ -d "$d" ] && [ ! -f "$d/.bundled" ] && last=$d; done; echo "$last"; }
latest_any() { local d last=""; for d in "$WORKROOT"/2*; do [ -d "$d" ] && last=$d; done; echo "$last"; }

if [ "$MODE" = dry ]; then
  say "dry run: checking the kit, the game folder and the tools (nothing is changed)"
  ok=1
  check_kit || ok=0; check_game || ok=0; check_tools || ok=0
  [ -d "$GAME" ] && say "installed heliasdream.bin sha256: $(sha "$GAME/heliasdream.bin") ($( [ "$(sha "$GAME/heliasdream.bin")" = "$RC5B_SHA" ] && echo rc5b || echo 'not rc5b'))"
  say "perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid) kptr_restrict=$(cat /proc/sys/kernel/kptr_restrict) (the session sets 1 and 0, then restores these)"
  if find_gpu; then say "gpu sysfs: $GPUDEV"; else say "no amdgpu sysfs found (sysmon will log CPU only)"; fi
  say "session folder would be: $WORKROOT/<date>-<time>; tarball: $OUTDIR/bbprofile-<date>-<time>.tar.gz"
  list_steps
  if [ $ok = 1 ]; then say "dry run: OK"; else say "dry run: problems above"; exit 1; fi
  exit 0
fi

if [ "$MODE" = resume ] || [ "$MODE" = bundle ]; then
  if [ -n "$RESUME_DIR" ]; then WORK=$(cd "$RESUME_DIR" && pwd); elif [ "$MODE" = bundle ]; then WORK=$(latest_any); else WORK=$(latest_session); fi
  if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then die "no session to $MODE in $WORKROOT"; fi
  SESSION=$(basename "$WORK")
  [ -f "$WORK/options.txt" ] && { read -r ONLY_SAVED SKIP_SAVED < "$WORK/options.txt"; [ -z "$ONLY" ] && [ "$ONLY_SAVED" != - ] && ONLY=$ONLY_SAVED; [ -z "$SKIP" ] && [ "$SKIP_SAVED" != - ] && SKIP=$SKIP_SAVED; }
  if [ "$MODE" = bundle ]; then bundle; exit $?; fi
else
  SESSION=$(date +%Y%m%d-%H%M%S)
  WORK=$WORKROOT/$SESSION
  mkdir -p "$WORK/steps"
  echo "${ONLY:--} ${SKIP:--}" > "$WORK/options.txt"
fi

trap cleanup EXIT
trap 'echo; echo "interrupted: cleaning up (resume later with: bash bbprofile.sh --resume)"; exit 130' INT TERM HUP

bold "Helia's Dream profiling session $SESSION"
say "work folder: $WORK"
check_kit || die "kit check failed"
check_game || die "game check failed"
check_tools || die "tool check failed"
pgrep -x heliasdream.bin > /dev/null && die "heliasdream.bin is running: close the game first"
binary_state || die "could not sort out heliasdream.bin"
todo=()
for s in "${ALL_STEPS[@]}"; do
  wanted "$s" || continue
  [ -f "$WORK/steps/$s/.done" ] || [ -f "$WORK/steps/$s/.skipped" ] && continue
  todo+=("$s")
done
[ ${#todo[@]} -gt 0 ] || { say "nothing left to run in this session"; bundle; exit $?; }
total=$BUNDLE_EST; for s in "${todo[@]}"; do total=$((total + EST[$s])); done
say "steps to run: ${todo[*]} (~$(mins $total) min including the tarball; finished around $(date -d "+$total sec" +%H:%M))"
say "Before you start: if you can, quit the game last at the Old Yharnam spot, so each start loads you there."
sudo_begin
perf_install && PERF_OK=1
sysctl_begin

for s in "${todo[@]}"; do
  if [ "$s" = a6 ] && [ -z "$ONLY" ]; then
    ask "Optional a6 (plain again, ~3 min: shows whether the A/B differences beat the drift). Run it? [Y/n] "
    case "$REPLY" in n|N) mkdir -p "$WORK/steps/a6"; touch "$WORK/steps/a6/.skipped"; continue ;; esac
  fi
  run_step "$s"; rc=$?
  if [ $rc = 4 ]; then say "stopping here; continue later with: bash bbprofile.sh --resume"; exit 0; fi
  if [ $rc != 0 ] && [ $rc != 3 ]; then
    ask "Step $s failed. r = retry, Enter = go on with the next step, q = stop for now: "
    case "$REPLY" in
      r|R) run_step "$s"; rc=$?; [ $rc = 4 ] && exit 0 ;;
      q|Q) say "continue later with: bash bbprofile.sh --resume"; exit 0 ;;
    esac
  fi
done
bundle
