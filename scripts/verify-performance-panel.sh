#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "${1:-$repo/build_x86_64/hyperShaderFX.so}")"
data="$(realpath "${2:-$repo/data}")"
work="$(mktemp -d /tmp/hs-performance-panel.XXXXXX)"
trap 'rm -rf "$work"' EXIT
export MESA_SHADER_CACHE_DISABLE=true
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -fPIC -O2 -I"$repo/src" -I"$inc" $(pkg-config --cflags Qt6Core) "$repo/scripts/verify-performance-controls.cpp" -o "$work/model" $(pkg-config --libs Qt6Core libobs)
"$work/model"
g++ -std=c++17 -fPIC -O2 -I"$repo/src" -I"$inc" $(pkg-config --cflags Qt6Widgets) "$repo/scripts/verify-performance-panel.cpp" -o "$work/panel" $(pkg-config --libs Qt6Widgets libobs)
mkdir "$work/home"
cp -a "$data" "$work/data"
mkdir -p "$work/data/shaders/tests"
cat > "$work/data/shaders/tests/performance_modulation_probe.effect" <<'SHADER'
#include "../../stdlib/hypershader_standard.glsl"
uniform float amount; // @min 0 @max 1 @step 0.01 @default 0.25
void hs_shade_pixel(out vec4 fragColor,in vec2 fragCoord){fragColor=float4(amount,0,0,1);}
HYPERSHADER_PIXEL_WRAPPER
SHADER
if ! (cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a timeout 35 "$work/panel" "$module" "$work/data") >"$work/result.log" 2>&1; then
 tail -65 "$work/result.log";exit 1
fi
grep -E '^PERFORMANCE_(PANEL|MODULATION|PCM)' "$work/result.log"
