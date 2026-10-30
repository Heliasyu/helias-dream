#!/bin/bash
# The tester packages for a group playtest on the dev server, built by hand
# and never part of a release (tools/check_release_bundle.sh fails a release
# that carries one; releases go to the live server). The Windows zip
# (tools/win_package.sh's) and the Linux tarball (tools/package_linux.sh's),
# each with the playtest config, TESTING.md and a launcher that writes a log
# file, from tools/playtest/. Players' own game files never go in: they bring
# the dump and the decrypted 1.09 eboot.
#   tools/package_playtest.sh WIN_ZIP LINUX_TARBALL OUTDIR     (LINUX_TARBALL "-": Windows only)
# The general package's README is left out, so TESTING.md is the one set of
# instructions; the game's paths live in the per-user config, the playtest
# toml adds only the dev server.
set -eu
cd "$(dirname "$0")/.."
win_zip=$(realpath "$1"); lin_tgz=$2; [ "$lin_tgz" = - ] || lin_tgz=$(realpath "$2"); out=$(realpath -m "$3")
pt=$PWD/tools/playtest
mkdir -p "$out"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT

# Windows.
(cd "$work" && unzip -q "$win_zip")
wdir=$(ls -d "$work"/helias-dream-windows-* | head -1)
wname=$(basename "$wdir" | sed 's/^helias-dream-windows-/helias-dream-playtest-/')-windows
rm -f "$wdir/README.txt"
mkdir -p "$wdir/logs"
# CRLF for Notepad and cmd.
sed 's/$/\r/' "$pt/heliasdream-playtest.toml" > "$wdir/heliasdream-playtest.toml"
sed 's/$/\r/' "$pt/TESTING.md" > "$wdir/TESTING.md"
cp "$pt/run-heliasdream.bat" "$wdir/run-heliasdream.bat"
mv "$wdir" "$work/$wname"
(cd "$work" && rm -f "$out/$wname.zip" && zip -qr "$out/$wname.zip" "$wname")

if [ "$lin_tgz" = - ]; then
    ls -la "$out/$wname.zip"
    exit 0
fi

# Linux.
(cd "$work" && tar xzf "$lin_tgz")
ldir=$(ls -d "$work"/helias-dream-linux-* "$work"/heliasdream-vk-linux-* "$work"/heliasdream-linux-* 2>/dev/null | head -1)   # heliasdream-vk: either name
[ -n "$ldir" ] || { echo "no Linux package in $lin_tgz"; exit 1; }
lname=$(basename "$ldir" | sed -E 's/^heliasdream(-vk)?-linux-/heliasdream\1-playtest-/')-linux-x86_64
cp "$pt/heliasdream-playtest.toml" "$pt/TESTING.md" "$ldir/"
rm -f "$ldir/heliasdream.example.toml"
sed -i 's/^cfg=\${HELIAS_CONFIG:-heliasdream.toml}/cfg=${HELIAS_CONFIG:-heliasdream-playtest.toml}/' "$ldir/run-heliasdream.sh"
grep -q 'heliasdream-playtest.toml' "$ldir/run-heliasdream.sh" || { echo "run-heliasdream.sh: config line not found"; exit 1; }
# The README's setup is for the general package; TESTING.md has the playtest's.
sed -i 's/^  heliasdream.example.toml lists every setting./  heliasdream.example.toml lists every setting; TESTING.md has the playtest steps./' "$ldir/README-linux.txt"
find "$ldir" -type d -exec chmod 755 {} + && find "$ldir" -type f -exec chmod 644 {} +
chmod 755 "$ldir/heliasdream" "$ldir/run-heliasdream.sh"
mv "$ldir" "$work/$lname"
(cd "$work" && rm -f "$out/$lname.tar.gz" && tar czf "$out/$lname.tar.gz" --owner=0 --group=0 "$lname")

ls -la "$out/$wname.zip" "$out/$lname.tar.gz"
