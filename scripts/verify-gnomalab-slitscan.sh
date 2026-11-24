#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
shader="$data/shaders/root/filter.gnomalab_slitscan.effect"
test -f "$shader" || { echo 'FAIL: native Gn Slit-scan shader missing' >&2; exit 1; }
work="$(mktemp -d /tmp/hsfx-isf-slit-XXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" \
  "$repo/scripts/verify-creative-graph-shaders.cpp" -o "$work/probe" $(pkg-config --libs libobs)
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
export MESA_SHADER_CACHE_DISABLE=true
mkdir -p "$work/home"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
 xvfb-run -a timeout 28 "$work/probe" "$module" "$data" "$shader" slit) >"$work/test.log" 2>&1 || {
 grep -E 'SLIT_|NEW_GRAPH_SHADER|Shader load failed|Shader|ERROR|error:' "$work/test.log" | tail -30 >&2 || true
 exit 1
}
grep -q 'Two-buffer graph enabled: A -> B -> Draw' "$work/test.log"
grep -q 'Loaded shader: shaders/root/filter.gnomalab_slitscan.effect' "$work/test.log"
grep -q '^SLIT_UI_SETTINGS .*PASS=1' "$work/test.log"
grep -q '^MODE_VERIFY=slit .*PASS=1' "$work/test.log"
grep -q '^NEW_GRAPH_SHADER=.*PASS=1' "$work/test.log"
! grep -Eq 'Shader load failed|set to invalid size|missing pixel shader' "$work/test.log"
grep -E 'SLIT_UI_SETTINGS|MODE_VERIFY=slit|NEW_GRAPH_SHADER=' "$work/test.log"
echo 'GNOMALAB_SLITSCAN_NATIVE_GPU_PASS=1'
