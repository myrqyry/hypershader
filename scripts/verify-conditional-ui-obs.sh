#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-conditional-ui-XXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" \
  "$repo/scripts/verify-conditional-ui-obs.cpp" -o "$work/probe" $(pkg-config --libs libobs)
(cd "$work" && HOME="$work" XDG_CONFIG_HOME="$work" LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a timeout 35 "$work/probe" "$repo/build_x86_64/hypershader.so" "$repo/data" \
  "$repo/data/shaders/tests/conditional_controls_probe.effect") >"$work/log" 2>&1 || {
  tail -60 "$work/log";exit 1;
}
grep '^CONDITIONAL_UI' "$work/log"
grep -q 'PASS=1' "$work/log"
