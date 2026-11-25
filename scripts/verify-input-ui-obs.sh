#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hs-input-ui.XXXXXX)"
trap 'rm -rf "$work"' EXIT
cp -a "$data" "$work/data"
mkdir -p "$work/home" "$work/data/shaders/tests"
python3 - "$work/data/shaders/tests" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1])
for name,decl,body in [
 ('plain','','/* hs_sight_depth audio_level hs_input0 */ float v=audio_gain;'),
 ('audio','','float v=audio_band(2);'),
 ('texture','','float v=hs_input0.Sample(textureSampler,fragCoord/hs_resolution.xy).r;'),
 ('named','uniform texture2d overlay; // @input 0 @label "Overlay Image"','float v=overlay.Sample(textureSampler,fragCoord/hs_resolution.xy).r;'),
 ('combined','uniform float hs_sight_motion_energy;','float v=audio_level+hs_sight_motion_energy+hs_input0.Sample(textureSampler,fragCoord/hs_resolution.xy).r;')]:
 (p/f'input-{name}.effect').write_text('#include "../../stdlib/hypershader_standard.glsl"\n'+decl+'\nuniform float audio_gain; // @default 0.5\nvoid hs_shade_pixel(out vec4 fragColor,in vec2 fragCoord){'+body+' fragColor=float4(v,v,v,1.0);}\nHYPERSHADER_PIXEL_WRAPPER\n')
PY
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-input-ui-obs.cpp" -o "$work/probe" $(pkg-config --libs libobs)
status=0
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a timeout 45 "$work/probe" "$module" "$work/data") > "$work/log" 2>&1 || status=$?
if (( status )); then grep -E -A3 "Shader load failed|Compiled preprocessed shader:.*effect=\(nil\)" "$work/log" || true; fi
grep '^INPUT_UI' "$work/log" || tail -60 "$work/log"
exit "$status"
