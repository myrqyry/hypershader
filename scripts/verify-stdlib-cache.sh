#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-stdlib-cache.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root" "$work/data/stdlib" "$work/home"
cp -a "$data/locale" "$work/data/"
printf '// @default 0.3 @label "Cache Gain"\nuniform float cache_gain;\nfloat cache_value(){return 0.2;}\n' > "$work/data/stdlib/cache_leaf.glsl"
printf '#include "cache_leaf.glsl"\nfloat cache_parent(){return cache_value();}\n' > "$work/data/stdlib/cache_parent.glsl"
cat > "$work/body" <<'SHADER'
uniform float4x4 ViewProj;
struct VertData {float4 pos:POSITION;float2 uv:TEXCOORD0;};
VertData VS(VertData v){v.pos=mul(float4(v.pos.xyz,1),ViewProj);return v;}
float4 Shade(VertData v):TARGET{return float4(cache_parent(),1,cache_gain,1);}
technique Draw {pass {vertex_shader=VS(v);pixel_shader=Shade(v);}}
SHADER
{ printf '#include "cache_parent.glsl"\n'; cat "$work/body"; } > "$work/data/shaders/root/cache_a.effect"
{ printf '#include "cache_leaf.glsl"\n#include "cache_parent.glsl"\n'; cat "$work/body"; } > "$work/data/shaders/root/cache_b.effect"
{ printf '#include "cache_other.glsl"\n#include "cache_parent.glsl"\n'; cat "$work/body"; } > "$work/data/shaders/root/cache_c.effect"
g++ -std=c++17 -O2 -Wl,--export-dynamic -I"$repo/src" -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-stdlib-cache.cpp" -o "$work/probe" $(pkg-config --libs libobs) -ldl
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 30 "$work/probe" "$module" "$work/data") >"$work/log" 2>&1 || { grep -E 'STDLIB_CACHE|CACHE_PIXEL|Required #include failed' "$work/log" || tail -40 "$work/log"; exit 1; }
grep '^STDLIB_CACHE' "$work/log"
