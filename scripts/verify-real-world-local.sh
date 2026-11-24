#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath -- "$1")"
data="$(realpath -- "$2")"
test -r "$module" && test -d "$data"
work="$(mktemp -d /tmp/hyper-local-effects-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-real-world-local.cpp" \
 -o "$work/check" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
export LD_LIBRARY_PATH="$(dirname "$module"):${LD_LIBRARY_PATH:-}"
failed=0
for mode in rgb bulge noise defaults; do
 case "$mode" in
  rgb) name=realworld_rgb_shift;;
  bulge) name=realworld_focus_bulge;;
  noise|defaults) name=realworld_focus_noise;;
 esac
 echo "TEST $name"
 if ! (cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' timeout 19 \
  "$work/check" "$module" "$data" "$data/shaders/root/$name.effect" "$mode");then
  failed=$((failed+1))
 fi
done
echo "REAL_WORLD_SUMMARY failed=$failed"
test "$failed" -eq 0
