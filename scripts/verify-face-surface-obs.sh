#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-face-surface-XXXXXX)"
trap 'rm -rf "$work"' EXIT
cc -std=c11 -O2 -I"$repo/src" -I"$(pkg-config --variable=includedir libobs)/obs" \
  -c "$repo/src/mediapipe/face_surface.c" -o "$work/face_surface.o"
g++ -std=c++17 -O2 -I"$repo/src" -I"$(pkg-config --variable=includedir libobs)/obs" \
  "$repo/scripts/verify-face-surface-obs.cpp" "$work/face_surface.o" \
  -o "$work/probe" $(pkg-config --libs libobs) -lm
(cd "$work" && HOME="$work" XDG_CONFIG_HOME="$work" LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a timeout 25 "$work/probe" "$repo/data/effects/face_surface.effect") > "$work/log" 2>&1 || {
  tail -75 "$work/log"
  exit 1
}
grep '^FACE_SURFACE_GPU' "$work/log"
grep -q 'PASS=1' "$work/log"
