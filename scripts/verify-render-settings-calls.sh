#!/usr/bin/env bash
# Native Linux probe: count real settings access while drawing known pixels.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-render-settings.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root" "$work/home"
cp -a "$data/stdlib" "$work/data/"
cat > "$work/data/shaders/root/cache_probe.effect" <<'SHADER'
#include "../../stdlib/hypershader_standard.glsl"
uniform float amount; // @min 0 @max 1 @default 0.25
uniform float4 audio_reaction_state;
uniform float4 hs_input_atlas0;
void hs_shade_pixel(out vec4 fragColor,in vec2 fragCoord){
    fragColor=vec4(amount,hs_input_atlas0.x/4.0,audio_reaction_state.x,1);
}
HYPERSHADER_PIXEL_WRAPPER
SHADER
g++ -std=c++17 -O2 -Wl,--export-dynamic -I"$(pkg-config --variable=includedir libobs)/obs" \
    "$repo/scripts/verify-render-settings-calls.cpp" -o "$work/probe" $(pkg-config --libs libobs) -ldl
status=0
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 20 \
 "$work/probe" "$module" "$work/data") >"$work/log" 2>&1 || status=$?
grep -E '^RENDER_SETTINGS_CALLS |Loaded shader:|Shader load failed|error:|Detail:' "$work/log" || true
exit "$status"
