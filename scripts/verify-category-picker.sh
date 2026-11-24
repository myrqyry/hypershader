#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$repo/build_x86_64/hyperShaderFX.so"
data="$repo/data"
if test "$#" -ge 1; then module="$1"; fi
if test "$#" -ge 2; then data="$2"; fi
work="$(mktemp -d /tmp/hyper-category-ui-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -w -I"$inc" -I"$repo/src" "$repo/scripts/verify-category-picker.cpp" -o "$work/native" $(pkg-config --libs libobs)
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
mkdir "$work/home"
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 45 "$work/native" "$module" "$data") > "$work/report.log" 2>&1 || {
 grep -E "CREATED_SOURCE|FAILED_CREATE|PROPERTIES|CATEGORY_PICKER|FAIL |CATEGORY_NATIVE_INTEGRATION" "$work/report.log" | tail -80;exit 1;
}
grep -E 'CATEGORY_PICKER|CATEGORY_NATIVE_INTEGRATION' "$work/report.log"
grep -q 'CATEGORY_NATIVE_INTEGRATION PASS=1' "$work/report.log"
