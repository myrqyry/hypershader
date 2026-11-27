#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-creative-graphs-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-creative-graph-shaders.cpp" -o "$work/probe" $(pkg-config --libs libobs)
export MESA_SHADER_CACHE_DISABLE=true
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
failed=0
for name in filter.vidicon_afterglow filter.ink_currents filter.ripple_glass filter.liquid_light; do
 mkdir -p "$work/$name"
 (cd "$work/$name" && HOME="$work/$name" XDG_CONFIG_HOME="$work/$name" \
   xvfb-run -a timeout 24 "$work/probe" "$module" "$data" \
   "$data/shaders/root/$name.effect") >"$work/$name.log" 2>&1 || {
     echo "FAIL: $name"; grep -E 'Shader load failed|Error creating shader|CREATIVE_FRAME_DEBUG|NEW_GRAPH_SHADER|Two-buffer|error:' "$work/$name.log" | tail -12; failed=1; continue;
   }
 grep -q 'Two-buffer graph enabled: A -> B -> Draw' "$work/$name.log" || {
   echo "FAIL: $name graph was not activated"; tail -25 "$work/$name.log"; exit 1;
 }
 if grep -Eq 'Shader load failed|set to invalid size|Pass \(0\).*missing' "$work/$name.log"; then
   echo "FAIL: $name native shader or uniform binding error";
   grep -E 'Shader load failed|set to invalid size|missing pixel shader' "$work/$name.log" | head -15;
   exit 1;
 fi
 grep -q "Loaded shader: shaders/root/$name.effect" "$work/$name.log" || {
   echo "FAIL: $name did not load"; tail -35 "$work/$name.log"; exit 1;
 }
 grep '^NEW_GRAPH_SHADER=' "$work/$name.log"
done
if test "$failed" -ne 0; then exit 1; fi
echo 'PASS: four distinct real multi-buffer effects with responsive controls and temporal output'
