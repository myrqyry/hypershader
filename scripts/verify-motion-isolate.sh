#!/usr/bin/env bash
# Exercises the real native hypershader graph, moving footage, and shader controls.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
shader="$data/shaders/root/filter.motion_isolate.effect"
test -f "$shader"
work="$(mktemp -d /tmp/hyper-motion-isolate-XXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" \
  "$repo/scripts/verify-creative-graph-shaders.cpp" -o "$work/test" $(pkg-config --libs libobs)
export MESA_SHADER_CACHE_DISABLE=true
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
mkdir -p "$work/home"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
 xvfb-run -a timeout 30 "$work/test" "$module" "$data" "$shader" mask) > "$work/out.log" 2>&1 || {
  tail -65 "$work/out.log"; exit 1;
}
grep -q 'Two-buffer graph enabled: A -> B -> Draw' "$work/out.log"
grep -q 'Loaded shader: shaders/root/filter.motion_isolate.effect' "$work/out.log"
grep -q 'NEW_GRAPH_SHADER=.*PASS=1' "$work/out.log"
grep -q 'MODE_VERIFY=mask .*PASS=1' "$work/out.log"
grep -q 'MOTION_UI_SETTINGS=.*expected=8 present=8 PASS=1' "$work/out.log"
grep '^MOTION_UI_SETTINGS=' "$work/out.log"
grep '^MODE_VERIFY=' "$work/out.log"
if grep -Eq 'Shader load failed|set to invalid size|missing pixel shader' "$work/out.log"; then
  tail -65 "$work/out.log"; exit 1
fi
grep 'NEW_GRAPH_SHADER=' "$work/out.log"
echo 'MOTION_ISOLATE_NATIVE_GPU_PASS=1'
