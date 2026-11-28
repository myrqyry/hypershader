#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-entity-lifecycle.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root" "$work/home"
cp -a "$data/stdlib" "$work/data/"
cat > "$work/data/shaders/root/plain.effect" <<'SHADER'
#include "../../stdlib/hypershader_standard.glsl"
void hs_shade_pixel(out vec4 fragColor, in vec2 fragCoord) {
    fragColor = vec4(0.2, 0.2, 0.3, 1.0);
}
HYPERSHADER_PIXEL_WRAPPER
SHADER
cat > "$work/data/shaders/root/entity.effect" <<'SHADER'
#include "../../stdlib/hypershader_standard.glsl"
uniform int entityCount;
uniform texture2d entityTexture;
void hs_shade_pixel(out vec4 fragColor, in vec2 fragCoord) {
    vec4 first = entityTexture.Sample(textureSampler, vec2(0.015, 0.5));
    fragColor = vec4(float(entityCount) / 16.0, first.y, 0.0, 1.0);
}
HYPERSHADER_PIXEL_WRAPPER
SHADER
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-entity-lifecycle.cpp" -o "$work/probe" $(pkg-config --libs libobs)
status=0
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 25 "$work/probe" "$module" "$work/data") >"$work/log" 2>&1 || status=$?
grep -E 'ECS_|EnTT Scene|Shader load failed|error:' "$work/log" || true
test "$status" = 0
# Assert actual allocation lifecycle in the native module, not just shader output.
awk '
/EnTT Scene Manager initialized/ {if(stage!=1 && stage!=3) bad=1; creates++}
/EnTT Scene Manager released/ {if(stage!=2 && stage!=4) bad=1; releases++}
/ECS_PLAIN_READY=1/ {stage=1}
/ECS_ENTITY_READY=1/ {stage=2}
/ECS_AWAY_READY=1/ {stage=3}
/ECS_RETURN_READY=1/ {stage=4}
END {if(bad || creates!=2 || releases!=2 || stage!=4) exit 1}
' "$work/log"
echo ECS_LAZY_ALLOCATION_PASS=1
