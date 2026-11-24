#!/usr/bin/env bash
# Real libobs/OpenGL shader control and bypass validation for the 3D repairs.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-geometry-controls-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
export MESA_SHADER_CACHE_DISABLE=true
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
for name in ultimate gaussian-billboard; do
    case "$name" in
        ultimate) shader="3d_ultimate_showcase"; label="ULTIMATE_SCENE_CONTROLS";;
        gaussian-billboard) shader="Gaussian_Billboard"; label="GAUSSIAN_BILLBOARD_CONTROLS";;
    esac
    g++ -std=c++17 -O2 -w -I"$inc" \
        "$repo/scripts/verify-$name-controls.cpp" -o "$work/$name" \
        $(pkg-config --libs libobs)
    mkdir -p "$work/home-$name"
    (cd "$work/home-$name" &&
        HOME="$work/home-$name" XDG_CONFIG_HOME="$work/home-$name" \
        xvfb-run -a timeout 48 "$work/$name" "$module" "$data" \
            "$data/shaders/root/$shader.effect") > "$work/$name.log" 2>&1 || {
        echo "FAIL $name: native control harness"
        grep -E "$label|Shader load failed|error:|invalid size" "$work/$name.log" | tail -20 || true
        exit 1
    }
    grep "$label" "$work/$name.log"
    grep -Fq "$label" "$work/$name.log"
    grep -q 'PASS=1' "$work/$name.log"
done
echo 'PASS: native geometry and Gaussian control/bypass tests'
