#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$repo/build_x86_64/hyperShaderFX.so"
data="$repo/data"
if [ "$#" -ge 1 ]; then module="$1"; fi
if [ "$#" -ge 2 ]; then data="$2"; fi
shader="$data/shaders/root/noisemaker_cell_refract.effect"
test -r "$module" && test -r "$shader"
work="$(mktemp -d /tmp/hyper-cell-refract-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-noisemaker-cell-refract.cpp"   -o "$work/real-filter" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
mkdir -p "$work/hyperShaderFX"
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:/usr/lib/x86_64-linux-gnu"
(cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' timeout 90  "$work/real-filter" "$module" "$data" "$shader")
