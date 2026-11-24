#!/usr/bin/env bash
# Real libobs / HyperShaderFX async-loader and GPU feedback test.
# No user OBS process or settings are touched. Run with xvfb-run.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
shader="$data/shaders/root/pframe_drift.effect"
test -r "$module" && test -r "$shader"
work="$(mktemp -d /tmp/hyper-pframe-runtime-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-pframe-filter-runtime.cpp" \
    -o "$work/test-pframe-filter" \
    $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
mkdir -p "$work/hyperShaderFX"
plugin_libs="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit"
export LD_LIBRARY_PATH="$plugin_libs:${LD_LIBRARY_PATH:-}"
for variant in high_loss zero_loss; do
    echo "TEST: HyperShaderFX P-frame filter — $variant"
    if [[ "$variant" == "zero_loss" ]]; then
        (cd "$work" && HYPER_TEST_ZERO_LOSS=1 xvfb-run -a -s '-screen 0 1280x720x24' \
            timeout 30 "$work/test-pframe-filter" "$module" "$data" \
            "$shader")
    else
        (cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' \
            timeout 30 "$work/test-pframe-filter" "$module" "$data" \
            "$shader")
    fi
done
