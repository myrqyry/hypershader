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
run_case() {
    local shader="$1" mode="$2" log="$work/$2.log"
    mkdir -p "$work/$mode/home" "$work/$mode/config"
    (cd "$work/$mode" && HOME="$work/$mode/home" XDG_CONFIG_HOME="$work/$mode/config" \
      xvfb-run -a -s '-screen 0 1280x720x24' timeout 23 "$work/test" "$module" "$data" "$shader" "$mode") >"$log" 2>&1 || {
        tail -130 "$log"; return 1;
    }
    echo "TEST graph $mode"
    grep -E '^TWO_BUFFER_' "$log"
    grep -q '^TWO_BUFFER_RUNTIME_PASS=1' "$log"
    if [[ "$mode" == invalid ]]; then
        grep -q 'Invalid graph buffer A descriptor' "$log"
    elif [[ "$mode" == budget ]]; then
        grep -q 'Graph resource budget exceeded' "$log"
        test "$(grep -c 'Graph resource budget exceeded' "$log")" -eq 2
    else
        grep -q 'Two-buffer graph enabled: A -> B -> Draw' "$log"
    fi
}
if [[ $# -ge 3 ]]; then
    run_case "$3" "${4:-default}"
else
    run_case "$data/shaders/tests/two_buffer_graph.effect" default
    run_case "$data/shaders/tests/two_buffer_scaled_graph.effect" scaled
    run_case "$data/shaders/tests/two_buffer_invalid_graph.effect" invalid
    run_case "$data/shaders/tests/two_buffer_budget_graph.effect" budget
fi
