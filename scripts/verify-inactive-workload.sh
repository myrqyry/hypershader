#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-inactive.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root" "$work/home"
cp -a "$data/stdlib" "$work/data/"
cat > "$work/data/shaders/root/workload.effect" <<'SHADER'
uniform float4x4 ViewProj;
uniform float audio_level;
uniform float hs_sight_present;
struct VertData { float4 pos:POSITION; float2 uv:TEXCOORD0; };
VertData VS(VertData v){v.pos=mul(float4(v.pos.xyz,1),ViewProj);return v;}
float4 Shade(VertData v):TARGET {return float4(saturate(audio_level),hs_sight_present,0,1);}
technique Draw {pass {vertex_shader=VS(v);pixel_shader=Shade(v);}}
SHADER
g++ -std=c++17 -O2 -I"$repo/src" -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-inactive-workload.cpp" -o "$work/probe" $(pkg-config --libs libobs)
status=0
for lane in source filter; do
  (cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 35 "$work/probe" "$module" "$work/data" "$lane") >"$work/log" 2>&1 || status=1
  grep '^WORKLOAD' "$work/log" || tail -35 "$work/log"
done
exit "$status"
