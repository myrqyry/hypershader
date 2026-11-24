#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$repo/build_x86_64/hyperShaderFX.so"
data="$repo/data"
shader="$data/shaders/root/transition.crt_power.effect"
test -s "$shader" && test -s "$module"
temp="$(mktemp -d /tmp/hyper-crt-transition-XXXXXXXX)"
trap 'rm -rf "$temp"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -I"$inc"   "$repo/scripts/verify-crt-power-transition.cpp" -o "$temp/test" $(pkg-config --libs libobs)
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:$(printenv LD_LIBRARY_PATH || true)"
export MESA_SHADER_CACHE_DISABLE=true
(cd "$temp" && HOME="$temp" XDG_CONFIG_HOME="$temp" xvfb-run -a timeout 40   "$temp/test" "$module" "$data" "$shader")
