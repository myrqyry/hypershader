#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-audio-vis-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-audio-visualizer.cpp" \
    -o "$work/probe" $(pkg-config --libs libobs)
(cd "$work" && HOME="$work" XDG_CONFIG_HOME="$work" \
    xvfb-run -a timeout 20 "$work/probe" "$module" "$data") >"$work/result.log" 2>&1 || {
    grep -E 'AUDIO_VIS|Shader load|error:|Failed|ASSERT|SOUND' "$work/result.log" | tail -25 || true
    tail -45 "$work/result.log"
    exit 1
}
grep -E '^(AUDIO_VIS|AUDIO_PRESET_SWITCH)' "$work/result.log"
grep -q 'AUDIO_VIS .*PASS=1' "$work/result.log"
grep -q '^AUDIO_PRESET_SWITCH audio=1 explicit_override=1 away=1' "$work/result.log"
