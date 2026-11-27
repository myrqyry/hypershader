#!/usr/bin/env bash
# Inspect real rendered pixels for one already-wrapped experimental .effect.
# A passing readback proves native compilation and changing the input pixels,
# not artistic fidelity, multi-GPU compatibility or real OBS GUI controls.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
shader="$(realpath "${1:?Pass a wrapped .effect file}")"
module="$(realpath "${2:-$root/build_x86_64/hypershader.so}")"
data="$(realpath "${3:-$root/data}")"
work="$(mktemp -d /tmp/hyper-webgl2-pixels-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -w -I"$inc" "$root/scripts/probe-webgl2-pixels.cpp" -o "$work/probe" $(pkg-config --libs libobs)
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
mkdir -p "$work/home"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
    xvfb-run -a timeout 25 "$work/probe" "$module" "$data" "$shader") > "$work/log" 2>&1 || {
      grep -E '^RENDER |^CORPUS_PIXEL_PASS|Shader load failed|Error creating shader' "$work/log" | tail -12 || true
      exit 1
    }
grep -E '^RENDER |^CORPUS_PIXEL_PASS' "$work/log"
