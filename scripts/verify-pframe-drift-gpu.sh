#!/usr/bin/env bash
# Verify actual OBS effect compilation, temporal history and keyframe recovery.
# Disposable Xvfb session; does not touch user's OBS settings or scenes.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hypershader-pframe-XXXXXX)"
trap 'rm -rf "$work"' EXIT
command -v xvfb-run >/dev/null || { echo "Need xvfb-run" >&2; exit 2; }
command -v pkg-config >/dev/null || { echo "Need pkg-config" >&2; exit 2; }
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" \
  "$repo/scripts/verify-pframe-drift-gpu.cpp" \
  -o "$work/test-pframe" \
  $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
xvfb-run -a -s '-screen 0 1280x720x24' \
  "$work/test-pframe" "$repo/data/shaders/root/pframe_drift.effect"
