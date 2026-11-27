#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-compose-test-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -w -I"$inc" "$repo/scripts/verify-global-compositor.cpp" \
 -o "$work/probe" $(pkg-config --libs libobs)
mkdir -p "$work/home"
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
export MESA_SHADER_CACHE_DISABLE=true
for shader in playground filter.ink_currents filter.afterimage_press; do
  mkdir -p "$work/$shader"
  (cd "$work/$shader" && HOME="$work/$shader" XDG_CONFIG_HOME="$work/$shader" \
   xvfb-run -a timeout 42 "$work/probe" "$module" "$data" \
   "$data/shaders/root/$shader.effect") >"$work/$shader.log" 2>&1 || {
    grep -E 'GLOBAL_FILTER_COMPOSITOR|Compositor|Shader load failed|error:' "$work/$shader.log" | tail -24
    exit 1
  }
  grep 'GLOBAL_FILTER_COMPOSITOR' "$work/$shader.log"
done

g++ -std=c++17 -O2 -w -I"$inc" "$repo/scripts/verify-global-source-compositor.cpp" \
  -o "$work/source-probe" $(pkg-config --libs libobs)
for source in source.paper_tide source.spectral_garden; do
  mkdir -p "$work/source-$source"
  (cd "$work/source-$source" && HOME="$work/source-$source" XDG_CONFIG_HOME="$work/source-$source" \
    xvfb-run -a timeout 33 "$work/source-probe" "$module" "$data" \
    "$data/shaders/root/$source.effect") >"$work/source-$source.log" 2>&1 || {
     grep -E 'GLOBAL_SOURCE_COMPOSITOR|Compositor|Shader load failed|error:' "$work/source-$source.log" | tail -24
     exit 1
  }
  grep 'GLOBAL_SOURCE_COMPOSITOR' "$work/source-$source.log"
done
mkdir -p "$work/frame-only"
(cd "$work/frame-only" && HOME="$work/frame-only" XDG_CONFIG_HOME="$work/frame-only" \
    xvfb-run -a timeout 33 "$work/source-probe" "$module" "$data" \
    "$data/shaders/tests/global_iframe_speed.effect") >"$work/frame-only.log" 2>&1 || {
    grep -E 'GLOBAL_SOURCE_COMPOSITOR|error:' "$work/frame-only.log" | tail -24
    exit 1
}
grep 'GLOBAL_SOURCE_COMPOSITOR' "$work/frame-only.log"

g++ -std=c++17 -O2 -w -I"$inc" "$repo/scripts/verify-global-transition-compositor.cpp" \
 -o "$work/transition-probe" $(pkg-config --libs libobs)
mkdir -p "$work/transition"
(cd "$work/transition" && HOME="$work/transition" XDG_CONFIG_HOME="$work/transition" \
 xvfb-run -a timeout 30 "$work/transition-probe" "$module" "$data" \
 "$data/shaders/root/playground.effect") >"$work/transition.log" 2>&1 || {
 grep -E 'GLOBAL_TRANSITION_COMPOSITOR|Double destroy|error:' "$work/transition.log" | tail -30
 exit 1
}
! grep -q 'Double destroy just occurred' "$work/transition.log"
grep 'GLOBAL_TRANSITION_COMPOSITOR' "$work/transition.log"
echo 'PASS: native global time and source compositor across filter, graph, feedback, sources and transition'
