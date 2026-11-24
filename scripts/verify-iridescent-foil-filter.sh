#!/usr/bin/env bash
# Real HyperShaderFX plugin-loader verification of the Glyphfield-inspired foil effect.
# Uses disposable OBS runtime settings; never modifies the installed plugin.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit/hyperShaderFX.so}"
data="${2:-$repo/data}"
shader="$data/shaders/root/iridescent_foil.effect"
test -r "$module" && test -r "$shader"
work="$(mktemp -d /tmp/hyper-foil-filter-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-iridescent-foil-filter.cpp" \
  -o "$work/real-filter" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
mkdir -p "$work/hyperShaderFX"
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
(cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' timeout 30 \
 "$work/real-filter" "$module" "$data" "$shader")
