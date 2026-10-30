#!/usr/bin/env bash
# heliasdream-vk: a game session under the Khronos validation layer with
# synchronization validation on, to find the missing barriers and
# dependencies the renderer has (docs/vk/PLAN.md, Phase 5.1). The same
# settings as tools/vvl_check.sh, on the real game: expect a low frame rate.
# Needs the layer (Arch/CachyOS: vulkan-validation-layers; Debian/Ubuntu:
# vulkan-validationlayers).
#
#   tools/vvl_game.sh [bbtest session name] [-- heliasdream args...]
#
# Runs `./bbtest start <name>` (default vvl) in BBTEST_GAME_DIR (bbtest's
# default folder), with the layer's messages in logs/vvl-<date>-<time>.txt
# there, at most 5 of each message, then prints how many of each kind came.
# Play a minute or two through what matters (a fight with particles, smoke,
# the blood on a hit, a menu), then quit; `bbtest stop` bundles as usual.
# Extra environment passes through, e.g. HELIAS_PASS_BARRIERS=lazy.
set -euo pipefail
GAME_DIR=${BBTEST_GAME_DIR:-$HOME/BloodBorne PC/heliasdream-linux-v0.2.18}
name=${1:-vvl}
[ $# -gt 0 ] && shift
[ -x "$GAME_DIR/bbtest" ] || { echo "vvl_game: no ./bbtest in $GAME_DIR (set BBTEST_GAME_DIR)" >&2; exit 1; }
found=0
for d in /usr/share/vulkan/explicit_layer.d /usr/local/share/vulkan/explicit_layer.d "${XDG_DATA_HOME:-$HOME/.local/share}/vulkan/explicit_layer.d"; do
    ls "$d"/*validation*.json > /dev/null 2>&1 && found=1
done
[ $found = 1 ] || { echo "vvl_game: the validation layer is not installed (Arch: sudo pacman -S vulkan-validation-layers)" >&2; exit 1; }

mkdir -p "$GAME_DIR/logs"
out="$GAME_DIR/logs/vvl-$(date +%Y%m%d-%H%M%S).txt"
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true
export VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn
export VK_KHRONOS_VALIDATION_DUPLICATE_MESSAGE_LIMIT=5
export VK_KHRONOS_VALIDATION_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG
export VK_KHRONOS_VALIDATION_LOG_FILENAME="$out"
echo "vvl_game: validation messages go to $out"
(cd "$GAME_DIR" && ./bbtest start "$name" "$@") || true

[ -s "$out" ] || { echo "vvl_game: no validation messages (or the layer did not load: check the log for VK_LAYER_KHRONOS_validation)"; exit 0; }
echo
echo "vvl_game: messages by kind ($out):"
# The layer names each message by its VUID or sync hazard in [ ... ].
grep -oE '\[ (VUID-[A-Za-z0-9_-]+|SYNC-HAZARD-[A-Z_-]+|UNASSIGNED-[A-Za-z0-9_.-]+) \]' "$out" | sort | uniq -c | sort -rn | head -40
echo
echo "sync hazards: $(grep -c 'SYNC-HAZARD' "$out" || true)"
