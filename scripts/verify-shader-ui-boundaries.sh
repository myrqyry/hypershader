#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$repo/build_x86_64/hypershader.so"
data="$repo/data"
if test $# -ge 1; then module="$1"; fi
if test $# -ge 2; then data="$2"; fi
work="$(mktemp -d /tmp/hyper-ui-boundaries-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -w -I"$(pkg-config --variable=includedir libobs)/obs" \
 "$repo/scripts/verify-shader-ui-boundaries.cpp" -o "$work/probe" \
 $(pkg-config --libs libobs)
mkdir "$work/home"
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
export MESA_SHADER_CACHE_DISABLE=true
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" \
 xvfb-run -a timeout 70 "$work/probe" "$module" "$data" \
 "$data/shaders/root/shabon/source.shabon_aurora_waves.effect" \
 "$data/shaders/root/blurred_gradient.effect") > "$work/report.log" 2>&1 || {
 grep -E 'SHADER_UI_BOUNDARIES|Shader load failed|Error creating shader|error:' "$work/report.log" | tail -25 || true
 exit 1
}
grep '^SHADER_UI_BOUNDARIES' "$work/report.log"
test "$(grep -c ' PASS=1' "$work/report.log")" -eq 3
grep '^SHADER_INFO_SWITCH' "$work/report.log"
