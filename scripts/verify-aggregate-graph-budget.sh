#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-aggregate.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root" "$work/home"
cp -a "$data/stdlib" "$data/locale" "$work/data/"
cp "$data/shaders/tests/two_buffer_graph.effect" "$work/data/shaders/root/graph.effect"
cat > "$work/data/shaders/root/plain.effect" <<'SHADER'
uniform float4x4 ViewProj;
struct VertData { float4 pos:POSITION; float2 uv:TEXCOORD0; };
VertData VS(VertData v){v.pos=mul(float4(v.pos.xyz,1),ViewProj);return v;}
float4 PS(VertData v):TARGET{return float4(0.1,0.5,0.1,1);}
technique Draw {pass {vertex_shader=VS(v);pixel_shader=PS(v);}}
SHADER
g++ -std=c++17 -O2 -Wl,--export-dynamic -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-aggregate-graph-budget.cpp" -o "$work/probe" $(pkg-config --libs libobs) -ldl
status=0
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" HS_GRAPH_AGGREGATE_MIB=1 xvfb-run -a timeout 25 "$work/probe" "$module" "$work/data") >"$work/log" 2>&1 || status=$?
grep -E '^AGGREGATE|Aggregate graph|Loaded shader:' "$work/log" || tail -30 "$work/log"
exit "$status"
