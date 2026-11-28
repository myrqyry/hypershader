#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-render-scale.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root" "$work/home"
cp -a "$data/stdlib" "$work/data/"
cat > "$work/data/shaders/root/plain.effect" <<'SHADER'
uniform float4x4 ViewProj;
uniform float3 hs_resolution;
sampler_state smp { Filter=Point; AddressU=Clamp; AddressV=Clamp; };
struct VertData { float4 pos:POSITION; float2 uv:TEXCOORD0; };
VertData VS(VertData v){v.pos=mul(float4(v.pos.xyz,1),ViewProj);return v;}
float4 Shade(VertData v):TARGET {return float4(hs_resolution.x/100.0,hs_resolution.y/76.0,0,1);}
technique Draw {pass {vertex_shader=VS(v);pixel_shader=Shade(v);}}
SHADER
python3 - "$work/data/shaders/root" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=(p/'plain.effect').read_text()
f='uniform texture2d hs_history;\n'+s.replace('0,1);','min(hs_history.Sample(smp,v.uv).b+0.02,0.8),1);')
(p/'feedback.effect').write_text(f)
g='uniform int hs_pass_state_order = 0;\nuniform int hs_pass_state_divisor = 2;\nuniform texture2d hs_pass_state;\nuniform texture2d hs_pass_state_previous;\nuniform float3 hs_pass_state_resolution;\nuniform float3 hs_graph_output_resolution;\n'+s
g=g.replace('float4 Shade(VertData v):TARGET {return float4(hs_resolution.x/100.0,hs_resolution.y/76.0,0,1);}', '''float4 State(VertData v):TARGET {
 return float4(0,0,min(hs_pass_state_previous.Sample(smp,v.uv).b+0.02,0.8),1);
}
technique Pass_state {pass {vertex_shader=VS(v);pixel_shader=State(v);}}
float4 Shade(VertData v):TARGET {
 bool good=length(hs_graph_output_resolution.xy-hs_resolution.xy)<0.01 &&
 length(hs_pass_state_resolution.xy-ceil(hs_resolution.xy/2.0))<0.01;
 return float4(hs_resolution.x/100.0,hs_resolution.y/76.0,good?hs_pass_state.Sample(smp,v.uv).b:1,1);
}''')
(p/'graph.effect').write_text(g)
PY
g++ -std=c++17 -O2 -Wl,--export-dynamic -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-render-scale.cpp" -o "$work/probe" $(pkg-config --libs libobs) -ldl
status=0
for lane in source filter; do
 for kind in plain feedback graph; do
  echo "RENDER_SCALE_CASE $lane $kind"
  (cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 25 "$work/probe" "$module" "$work/data" "$lane" "shaders/root/$kind.effect") >"$work/log" 2>&1 || status=1
  grep -E '^RENDER_SCALE' "$work/log" || tail -30 "$work/log"
 done
done
exit "$status"
