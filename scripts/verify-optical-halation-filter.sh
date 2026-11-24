#!/usr/bin/env bash
# Load actual HyperShaderFX native OBS filter in isolated libobs video graph.
# The module defaults to the locally verified installed binary; no OBS GUI needed.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
installed="$HOME/.config/obs-studio/plugins/hyperShaderFX"
module="${1:-$installed/bin/64bit/hyperShaderFX.so}"
plugin_data="${2:-$repo/data}"
shader="${3:-$repo/data/shaders/root/optical_halation.effect}"
test -r "$module" && test -r "$shader"
work="$(mktemp -d /tmp/hyper-optical-runtime-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-optical-halation-filter.cpp" \
  -o "$work/real-filter" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
mkdir "$work/hyperShaderFX"
export LD_LIBRARY_PATH="$installed/bin/64bit:${LD_LIBRARY_PATH:-}"
for variant in bloom no_bloom; do
  echo "TEST optical_halation: $variant"
  if [[ "$variant" == "no_bloom" ]]; then
    (cd "$work" && HYPER_TEST_ZERO_HALATION=1 xvfb-run -a -s '-screen 0 1280x720x24' \
      timeout 30 "$work/real-filter" "$module" "$plugin_data" "$shader")
  else
    (cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' \
      timeout 30 "$work/real-filter" "$module" "$plugin_data" "$shader")
  fi
done
