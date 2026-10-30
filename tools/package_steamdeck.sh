#!/bin/bash
# The Steam Deck kit: the Linux package (tools/package_linux.sh's) with the
# Deck's settings - tools/steamdeck/heliasdream-steamdeck.toml: its screen's
# 1280x800, full screen, 30 fps - STEAMDECK.md, and the launcher pointed at
# them. The game's paths stay in the per-user config, as for every package.
#   tools/package_steamdeck.sh LINUX_TARBALL OUTDIR
set -eu
cd "$(dirname "$0")/.."
lin_tgz=$(realpath "$1"); out=$(realpath -m "$2")
sd=$PWD/tools/steamdeck
mkdir -p "$out"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
(cd "$work" && tar xzf "$lin_tgz")
# heliasdream-vk: the Linux package is helias-dream-linux-<rev> (tools/package_linux.sh);
# the kit keeps the prefix, helias-dream-steamdeck-<rev>.
ldir=$(ls -d "$work"/helias-dream-linux-* "$work"/heliasdream-vk-linux-* "$work"/heliasdream-linux-* 2>/dev/null | head -1)
[ -n "$ldir" ] || { echo "no Linux package in $1"; exit 1; }
name=$(basename "$ldir" | sed -E 's/^(heliasdream(-vk)?|helias-dream)-linux-/\1-steamdeck-/')
cp "$sd/heliasdream-steamdeck.toml" "$sd/STEAMDECK.md" "$ldir/"
sed -i 's/^cfg=\${HELIAS_CONFIG:-heliasdream.toml}/cfg=${HELIAS_CONFIG:-heliasdream-steamdeck.toml}/' "$ldir/run-heliasdream.sh"
grep -q 'heliasdream-steamdeck.toml' "$ldir/run-heliasdream.sh" || { echo "run-heliasdream.sh: config line not found"; exit 1; }
sed -i 's/^  heliasdream.example.toml lists every setting./  heliasdream.example.toml lists every setting; STEAMDECK.md has the Steam Deck steps./' "$ldir/README-linux.txt"
grep -q 'STEAMDECK.md' "$ldir/README-linux.txt" || { echo "README-linux.txt: settings line not found"; exit 1; }
find "$ldir" -type d -exec chmod 755 {} + && find "$ldir" -type f -exec chmod 644 {} +
chmod 755 "$ldir/heliasdream" "$ldir/run-heliasdream.sh"
mv "$ldir" "$work/$name"
(cd "$work" && rm -f "$out/$name.tar.gz" && tar czf "$out/$name.tar.gz" --owner=0 --group=0 "$name")
ls -la "$out/$name.tar.gz"
