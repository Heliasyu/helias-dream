#!/bin/bash
# The release's bundles go to the live server by default: no playtest kit
# among the assets, no config in a bundle that names a server, the example
# config showing the live server's settings, launchers that pass no config,
# and the binaries' own first-start template naming the live server over
# https. The publish job runs it on what it is about to sign.
#   tools/check_release_bundle.sh DIR     (dist/release)
set -eu
d=${1:?usage: tools/check_release_bundle.sh DIR}
fail() { echo "release bundle: $*" >&2; exit 1; }
if ls "$d" | grep -i playtest; then fail "a playtest kit is among the assets"; fi
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
n=0
for z in "$d"/helias-dream-windows-*.zip "$d"/heliasdream-win-*.zip; do [ -f "$z" ] || continue; mkdir -p "$work/w$n"; unzip -q "$z" -d "$work/w$n"; n=$((n + 1)); done
# heliasdream-vk: its Linux and Steam Deck bundles are helias-dream-linux-* and helias-dream-steamdeck-* (heliasdream-vk-* before the rename).
for t in "$d"/heliasdream-linux-*.tar.gz "$d"/heliasdream-steamdeck-*.tar.gz "$d"/heliasdream-vk-linux-*.tar.gz "$d"/heliasdream-vk-steamdeck-*.tar.gz "$d"/helias-dream-linux-*.tar.gz "$d"/helias-dream-steamdeck-*.tar.gz; do [ -f "$t" ] || continue; mkdir -p "$work/l$n"; tar xzf "$t" -C "$work/l$n"; n=$((n + 1)); done
[ "$n" -gt 0 ] || fail "no bundles in $d"
# Only the example config may name a server (it shows the defaults).
if find "$work" -name '*.toml' ! -iname heliasdream.example.toml -exec grep -lE '^[[:space:]]*(host|np_server|auth_server)[[:space:]]*=' {} + | grep .; then
    fail "a bundle carries a config that names a server"
fi
while IFS= read -r f; do
    for want in 'host = "thehuntersdream.com"' 'scheme = "https"' 'verify_tls = true'; do
        tr -d '\r' < "$f" | grep -qxF "$want" || fail "${f#"$work"/}: lacks $want"
    done
done < <(find "$work" -iname heliasdream.example.toml)
while IFS= read -r f; do
    if tr -d '\r' < "$f" | grep -E '^[^r#].*--config|^(heliasdream|HeliasDream-engine)\.exe .*--config'; then fail "${f#"$work"/} passes a config"; fi
done < <(find "$work" -name 'run-engine.bat' -o -name 'run-heliasdream.bat')
# What a first start writes (core/config.cpp's template), in each executable.
# The launchers carry the engine as it is (an uncompressed resource), so its strings are there.
for b in "$d"/HeliasDream "$d"/HeliasDream.exe; do
    [ -f "$b" ] || continue
    s=$(strings -a "$b")
    for want in 'host = "thehuntersdream.com"' 'scheme = "https"' 'verify_tls = true' 'require_account = true' \
                'auth_server = "https://thehuntersdream.com"'; do
        grep -qxF "$want" <<<"$s" || fail "$(basename "$b"): its first-start config lacks $want"
    done
done
echo "release bundles: $n checked, the live server over https by default, no playtest kit"
