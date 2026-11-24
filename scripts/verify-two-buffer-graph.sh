#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-two-buffers-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-two-buffer-graph.cpp" -o "$work/test" $(pkg-config --libs libobs)
export MESA_SHADER_CACHE_DISABLE=true
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/config" xvfb-run -a -s '-screen 0 1280x720x24' timeout 23 "$work/test" "$module" "$data" "$data/shaders/tests/two_buffer_graph.effect") >"$work/test.log" 2>&1 || {
 tail -45 "$work/test.log"; exit 1;
}
grep -E '^TWO_BUFFER_' "$work/test.log"
grep -q '^TWO_BUFFER_RUNTIME_PASS=1' "$work/test.log"
