#!/bin/bash
# The program icon from res/icon/heliasdream.svg: res/heliasdream.ico (the engine's
# resource), the launcher's HeliasDream.ico (its exe and windows) and a 256 px PNG
# for Linux desktops. Needs rsvg-convert and ImageMagick.
set -eu
cd "$(dirname "$0")/.."
svg=res/icon/heliasdream.svg
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
for z in 16 20 24 32 40 48 64 128 256; do rsvg-convert -w $z -h $z "$svg" -o "$work/$z.png"; done
magick "$work"/{16,20,24,32,40,48,64,128,256}.png res/heliasdream.ico
cp res/heliasdream.ico launcher/HeliasDream/Assets/HeliasDream.ico
cp "$work/256.png" res/icon/heliasdream-256.png
echo "icon: res/heliasdream.ico, launcher/HeliasDream/Assets/HeliasDream.ico, res/icon/heliasdream-256.png"
