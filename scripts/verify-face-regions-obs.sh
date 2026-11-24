#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
python3 - "$repo" "$work" <<'PY'
from pathlib import Path
import sys
repo,work=map(Path,sys.argv[1:])
lib=repo/"data/stdlib"
s=(lib/"mediapipe.glsl").read_text()
for name in ("hypershader_standard.glsl","face_regions.glsl"):
    s=s.replace('#include "'+name+'"',(lib/name).read_text())
s+="""
float4 PSRegions(VertData v):TARGET {
 float3 c=hs_face_tint_luma(float3(0.9,0.1,0.4),0.5);
 return float4(hs_eye_mask(v.uv,false,hs_resolution.xy),hs_eye_mask(v.uv,true,hs_resolution.xy),dot(c,float3(0.2126,0.7152,0.0722)),1.0);
}
technique Draw {pass {vertex_shader=VSDefault(v_in);pixel_shader=PSRegions(v);}}
"""
(work/"probe.effect").write_text(s)
PY
c++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-face-regions-obs.cpp" -o "$work/probe" $(pkg-config --libs libobs)
(cd "$work" && MESA_SHADER_CACHE_DISABLE=true xvfb-run -a timeout 25 "$work/probe" "$work/probe.effect")
