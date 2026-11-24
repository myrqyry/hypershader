#!/usr/bin/env bash
# Real standalone OBS source + feedback-history reuse, isolated under Xvfb.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-source-feedback-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-source-feedback-cycle.cpp" \
  -o "$work/verify-source" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
(cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' timeout 20 \
 "$work/verify-source" "$module" "$data" \
 "$data/shaders/tests/source_feedback_cycle_probe.effect") >"$work/result.log" 2>&1 || {
  tail -40 "$work/result.log"
  exit 1
}
grep 'SOURCE_FEEDBACK_CYCLE' "$work/result.log"
grep -q 'SOURCE_FEEDBACK_CYCLE .*PASS=1' "$work/result.log"
