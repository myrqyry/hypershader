#!/usr/bin/env bash
# Isolated libobs properties test: does not launch the user's OBS instance.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$repo/build_x86_64/hyperShaderFX.so"
data="$repo/data"
if test "$#" -ge 1; then module="$1"; fi
if test "$#" -ge 2; then data="$2"; fi
work="$(mktemp -d /tmp/hypershader-ui-XXXXXX)"
trap 'rm -rf "$work"' EXIT
command -v xvfb-run >/dev/null || { echo "Need xvfb-run" >&2; exit 2; }
command -v pkg-config >/dev/null || { echo "Need pkg-config" >&2; exit 2; }
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" \
    "$repo/scripts/verify-filter-ui-obs.cpp" -o "$work/verify-ui" \
    $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
mkdir -p "$work/hyperShaderFX"
(cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' \
    "$work/verify-ui" "$module" "$data")
