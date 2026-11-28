#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-transient.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root" "$work/home"
cp -a "$data/stdlib" "$work/data/"
cat > "$work/data/shaders/root/transient.effect" <<'SHADER'
uniform float4x4 ViewProj;
uniform int hs_pass_state_order = 0;
uniform int hs_pass_state_divisor = 2;
uniform texture2d hs_pass_state;
uniform texture2d hs_pass_state_previous;
uniform int hs_pass_a_order = 1;
uniform int hs_pass_a_divisor = 2;
uniform int hs_pass_a_transient = 1;
uniform int hs_pass_a_last_use = 2;
uniform texture2d hs_pass_a;
uniform int hs_pass_b_order = 2;
uniform int hs_pass_b_divisor = 2;
uniform int hs_pass_b_transient = 1;
uniform int hs_pass_b_last_use = 3;
uniform texture2d hs_pass_b;
uniform int hs_pass_c_order = 3;
uniform int hs_pass_c_divisor = 2;
uniform int hs_pass_c_transient = 1;
uniform texture2d hs_pass_c;
sampler_state smp { Filter=Point; AddressU=Clamp; AddressV=Clamp; };
struct VertData { float4 pos:POSITION; float2 uv:TEXCOORD0; };
VertData VS(VertData v){v.pos=mul(float4(v.pos.xyz,1),ViewProj);return v;}
float4 State(VertData v):TARGET {
 return float4(min(hs_pass_state_previous.Sample(smp,v.uv).r+0.012,0.8),0,0,1);
}
float4 A(VertData v):TARGET {return float4(hs_pass_state.Sample(smp,v.uv).r,0.2,0,1);}
float4 B(VertData v):TARGET {return hs_pass_a.Sample(smp,v.uv)+float4(0,0.2,0,0);}
float4 C(VertData v):TARGET {return hs_pass_b.Sample(smp,v.uv)+float4(0,0.2,0,0);}
float4 Final(VertData v):TARGET {return hs_pass_c.Sample(smp,v.uv);}
technique Pass_state {pass {vertex_shader=VS(v);pixel_shader=State(v);}}
technique Pass_a {pass {vertex_shader=VS(v);pixel_shader=A(v);}}
technique Pass_b {pass {vertex_shader=VS(v);pixel_shader=B(v);}}
technique Pass_c {pass {vertex_shader=VS(v);pixel_shader=C(v);}}
technique Draw {pass {vertex_shader=VS(v);pixel_shader=Final(v);}}
SHADER
expected=4
if [[ "${3:-transient}" == persistent ]]; then
    # Identical shader arithmetic, but the legacy persistent contract.
    sed -i -E '/^uniform int .*_(transient|last_use) =/d; /uniform texture2d hs_pass_[abc];/s/(hs_pass_[abc]);/\1; uniform texture2d \1_previous;/' "$work/data/shaders/root/transient.effect"
    expected=8
else
    # Expired A shares the active destination of C: it MUST bind black,
    # not its old contents nor the active write target.
    sed -i 's/return hs_pass_b.Sample(smp,v.uv)+float4(0,0.2,0,0);/return hs_pass_b.Sample(smp,v.uv)+float4(0,0.2+hs_pass_a.Sample(smp,v.uv).r,0,0);/' "$work/data/shaders/root/transient.effect"
    sed 's/hs_pass_a_last_use = 2/hs_pass_a_last_use = 0/' "$work/data/shaders/root/transient.effect" > "$work/data/shaders/root/bad-end.effect"
    sed 's/hs_pass_a_last_use = 2/hs_pass_a_last_use = 5/' "$work/data/shaders/root/transient.effect" > "$work/data/shaders/root/bad-future.effect"
    sed 's/uniform texture2d hs_pass_a;/uniform texture2d hs_pass_a; uniform texture2d hs_pass_a_previous;/' "$work/data/shaders/root/transient.effect" > "$work/data/shaders/root/bad-history.effect"
fi
g++ -std=c++17 -O2 -Wl,--export-dynamic -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-transient-graph.cpp" -o "$work/probe" $(pkg-config --libs libobs) -ldl
status=0
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" xvfb-run -a timeout 20 "$work/probe" "$module" "$work/data" "$expected") >"$work/log" 2>&1 || status=$?
grep -E 'TRANSIENT_GRAPH|Invalid named pass graph|Named pass graph enabled|Loaded shader:' "$work/log" || tail -30 "$work/log"
if [[ "$expected" == 4 && "$status" == 0 ]]; then
    test "$(grep -c 'Invalid named pass graph:' "$work/log")" -eq 3
fi
exit "$status"
