#!/usr/bin/env bash
# Compile and execute the same catalog helper used by the Qt dock.
# No OBS settings or installed files are changed.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-catalog-XXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -fPIC $(pkg-config --cflags Qt6Core)     "$repo/scripts/verify-shader-manager-catalog.cpp"     -o "$work/catalog" $(pkg-config --libs Qt6Core)
"$work/catalog" "$repo/data/shaders"     "$HOME/.config/obs-studio/plugins/hyperShaderFX/data/shaders"
