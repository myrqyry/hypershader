#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-history-reset.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root" "$work/home"
cp -a "$data/stdlib" "$work/data/"
cat > "$work/data/shaders/root/feedback.effect" <<'SHADER'
uniform float4x4 ViewProj;
uniform texture2d hs_history;
uniform float gain; // @min 0.0 @max 1.0 @step 0.01 @default 0.3 @label "Gain"
sampler_state smp { Filter=Point; AddressU=Clamp; AddressV=Clamp; };
struct VertData { float4 pos:POSITION; float2 uv:TEXCOORD0; };
VertData VS(VertData v){v.pos=mul(float4(v.pos.xyz,1),ViewProj);return v;}
float4 Next(VertData v):TARGET {return float4(min(hs_history.Sample(smp,v.uv).r+0.04,0.8),gain,0,1);}
technique Draw {pass {vertex_shader=VS(v);pixel_shader=Next(v);}}
SHADER
python3 - "$work/data/shaders/root" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=(p/'feedback.effect').read_text()
s=s.replace('uniform texture2d hs_history;','uniform int hs_pass_state_order = 0;\nuniform texture2d hs_pass_state;\nuniform texture2d hs_pass_state_previous;')
s=s.replace('hs_history.Sample','hs_pass_state_previous.Sample').replace('technique Draw {','technique Pass_state {')
s='uniform int hs_graph_frame;\n'+s
s+='\nfloat4 Final(VertData v):TARGET {float4 c=hs_pass_state.Sample(smp,v.uv);c.b=min(float(hs_graph_frame)*0.1,1.0);return c;}\ntechnique Draw {pass {vertex_shader=VS(v);pixel_shader=Final(v);}}\n'
(p/'graph.effect').write_text(s)
PY
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-history-reset.cpp" -o "$work/probe" $(pkg-config --libs libobs)
status=0
for lane in source filter; do
 for kind in feedback graph; do
  echo "HISTORY_CASE $lane $kind"
  (cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 20 "$work/probe" "$module" "$work/data" "$lane" "shaders/root/$kind.effect") >"$work/log" 2>&1 || status=1
  grep -E '^HISTORY_RESET' "$work/log" || tail -30 "$work/log"
 done
done
exit "$status"
