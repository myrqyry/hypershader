#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
face="${2:?pass a 256x256 RGBA public test image}"
effect="${3:-$repo/data/shaders/tests/mediapipe_pipeline_probe.effect}"
work="$(mktemp -d /tmp/hyper-mp-filter-XXXXXX)"
export MESA_SHADER_CACHE_DISABLE=true
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" \
    "$repo/scripts/verify-mediapipe-filter-obs.cpp" -o "$work/probe" $(pkg-config --libs libobs)
(cd "$work" && HOME="$work" XDG_CONFIG_HOME="$work" xvfb-run -a timeout 30 "$work/probe" \
    "$module" "$repo/data" "$effect" "$face") >"$work/log" 2>&1 || {
    rg -n '(MP_FILTER_GPU|\[MediaPipe\]|error:|failed|MP_AUDIT)' "$work/log" | tail -55 || true
    exit 1
}
grep -E '^MP_FILTER_GPU' "$work/log"
grep -q 'MP_FILTER_GPU .*PASS=1' "$work/log"
