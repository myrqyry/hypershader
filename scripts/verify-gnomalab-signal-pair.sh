#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
scratch="$(mktemp -d /tmp/hsfx-gnoma-pair-XXXXXX)"
trap 'rm -rf "$scratch"' EXIT
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" \
 "$repo/scripts/verify-gnomalab-signal-pair.cpp" -o "$scratch/probe" $(pkg-config --libs libobs)
export MESA_SHADER_CACHE_DISABLE=true
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
for name in topo_scan broke_in_bad; do
 shader="$data/shaders/root/filter.gnomalab_${name}.effect"
 test -f "$shader" || { echo "EXPECTED_SHADER_MISSING=$shader" >&2; exit 1; }
 mkdir -p "$scratch/$name/home"
 (cd "$scratch/$name" && HOME="$scratch/$name/home" XDG_CONFIG_HOME="$scratch/$name/home" \
    xvfb-run -a timeout 30 "$scratch/probe" "$module" "$data" "$shader" "$name") >"$scratch/$name/log" 2>&1 || {
    grep -E 'GNOMALAB_|Shader load failed|error:|MISSING|UI_WRONG' "$scratch/$name/log" | tail -55 >&2 || true
    exit 1
 }
 grep -q 'GNOMALAB_PAIR_GPU_PASS=1' "$scratch/$name/log"
 grep -q 'GNOMALAB_PAIR_UI_PASS=1' "$scratch/$name/log"
 grep -q "Loaded shader: shaders/root/filter.gnomalab_${name}.effect" "$scratch/$name/log"
 ! grep -Eq 'Shader load failed|set to invalid size|missing pixel shader' "$scratch/$name/log"
 grep -E '^GNOMALAB_PAIR_' "$scratch/$name/log"
done
echo 'GNOMALAB_SIGNAL_PAIR_PASS=2'
