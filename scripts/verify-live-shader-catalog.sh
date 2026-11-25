#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hs-live-catalog.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
cp -a "$data" "$work/data"
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-live-shader-catalog.cpp" -o "$work/native" $(pkg-config --libs libobs) $(pkg-config --cflags --libs Qt6Core)
status=0
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 45 "$work/native" "$module" "$work/data") > "$work/report" 2>&1 || status=$?
grep 'LIVE_CATALOG' "$work/report" || tail -50 "$work/report"
exit "$status"
