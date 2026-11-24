#!/usr/bin/env bash
# Native libobs GPU test; does not require or modify the user's OBS session.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-halation-gpu-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-optical-halation-gpu.cpp" \
    -o "$work/test-halation" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
xvfb-run -a -s '-screen 0 1280x720x24' "$work/test-halation" \
  "$repo/data/shaders/root/optical_halation.effect"
