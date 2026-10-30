#!/usr/bin/env bash
# heliasdream-vk: the player's one command for a round of tests. The steps come
# from test-plan.txt beside it (written for each round), so nothing has to
# be remembered: each step says what to do, waits for Enter, runs, and the
# next one follows. At the end it says the round is done.
#
#   ./run-tests.sh            the round in test-plan.txt
#
# test-plan.txt, one directive a line ('#' lines are comments):
#   build <file>                       copy <file> over heliasdream.bin first (the old one kept as heliasdream.bin.before-tests)
#   step <name> | <env> | <what to do>   a bbtest session called <name> with <env> (VAR=value ...; - for none)
#   vvl <name> | <env> | <what to do>    the same under the validation layer (tools/vvl_game.sh)
set -uo pipefail
cd "$(dirname "$(readlink -f "$0")")" || exit 1
plan=test-plan.txt
[ -f "$plan" ] || { echo "No test-plan.txt here: nothing to test right now."; exit 0; }

bold=$(tput bold 2> /dev/null || true); off=$(tput sgr0 2> /dev/null || true)
say() { printf '\n%s%s%s\n' "$bold" "$*" "$off"; }

total=$(grep -cE '^(step|vvl) ' "$plan")
n=0
while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
        '' | '#'*) continue ;;
        build\ *)
            file=${line#build }
            [ -f "$file" ] || { echo "The test build $file is missing."; exit 1; }
            if ! cmp -s "$file" heliasdream.bin; then
                [ -f heliasdream.bin.before-tests ] || cp -p heliasdream.bin heliasdream.bin.before-tests
                cp "$file" heliasdream.bin && say "Installed the test build $file."
            fi
            ;;
        step\ * | vvl\ *)
            kind=${line%% *}
            rest=${line#* }
            name=$(echo "${rest%%|*}" | xargs)
            rest=${rest#*|}
            envs=$(echo "${rest%%|*}" | xargs)
            what=$(echo "${rest#*|}" | xargs)
            n=$((n + 1))
            say "Test $n of $total: $name"
            echo "$what"
            echo
            ans=
            read -r -p "Press Enter to start the game (or type s and Enter to skip this one): " ans < /dev/tty
            [ "$ans" = s ] && continue
            [ "$envs" = - ] && envs=
            if [ "$kind" = vvl ]; then
                env $envs ./vvl_game.sh "$name" < /dev/tty
            else
                env $envs ./bbtest start "$name" < /dev/tty
            fi
            say "Test $n done."
            ;;
    esac
done < "$plan"
say "All done. Tell Claude: tests done."
