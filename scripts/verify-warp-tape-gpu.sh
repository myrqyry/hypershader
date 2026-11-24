#!/usr/bin/env bash
# Actual libobs GPU shader compilation and moving-column feedback regression.
# Uses headless OpenGL and a disposable working directory; no OBS settings.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
shader="$repo/data/shaders/root/warp_tape.effect"
test -r "$shader"
work="$(mktemp -d /tmp/hyper-warp-tape-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=gnu++17 -O2 -I"$inc" "$repo/scripts/verify-warp-tape-gpu.cpp" \
    -o "$work/verify" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
xvfb-run -a -s '-screen 0 1280x720x24' \
    timeout 30 "$work/verify" "$shader"
