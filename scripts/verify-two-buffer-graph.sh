#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="$(realpath "${2:-$repo/data}")"
work="$(mktemp -d /tmp/hyper-two-buffers-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-two-buffer-graph.cpp" -o "$work/test" $(pkg-config --libs libobs)
export MESA_SHADER_CACHE_DISABLE=true
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
# Generate a known 3x2 RGBA texture locally; no third-party fixture asset.
python3 - "$work/channel.png" <<'PYPNG'
import struct,sys,zlib
from pathlib import Path
def chunk(kind,data):
    return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
row=b"\x00"+bytes([64,128,191,255])*3
Path(sys.argv[1]).write_bytes(b"\x89PNG\r\n\x1a\n"+
    chunk(b"IHDR",struct.pack(">IIBBBBB",3,2,8,6,0,0,0))+
    chunk(b"IDAT",zlib.compress(row*2))+chunk(b"IEND",b""))
PYPNG
# The real shader loader confines effects to its data directory. Keep
# generated cases inside an isolated copy, never loosen that boundary.
original_data="$data"
data="$work/data"
cp -a --reflink=auto "$original_data" "$data"
generated="$data/shaders/tests/generated-graph"
mkdir -p "$generated"
# Derive malformed and maximum-sized schedules from the same native fixture.
python3 - "$data/shaders/tests/ordered_pass_graph.effect" "$generated" <<'PYGRAPH'
from pathlib import Path
import sys
source=Path(sys.argv[1]).read_text()
work=Path(sys.argv[2])
variants={
    "duplicate": source.replace("hs_pass_resolve_order = 2","hs_pass_resolve_order = 1"),
    "gap": source.replace("hs_pass_resolve_order = 2","hs_pass_resolve_order = 3"),
    "missing": source.replace("technique Pass_resolve","technique Missing_resolve"),
    "divisor": source.replace("hs_pass_resolve_divisor = 8","hs_pass_resolve_divisor = 3"),
    "orphan": "uniform float hs_pass_orphan;\n"+source,
    "excess": "uniform int hs_pass_excess_order = 8;\n"+source,
}
for name,code in variants.items():
    (work/f"bad-{name}.effect").write_text(code)
# Extend the three-stage fixture by five more real GPU stages. Each
# checks a different predecessor alpha plus its own initialized history.
decl=[]
passes=[]
previous="resolve"
for i in range(3,8):
    name=f"stage{i}"
    decl.append(f"""uniform int hs_pass_{name}_order = {i};
uniform int hs_pass_{name}_divisor = 8;
uniform int hs_pass_{name}_format = 1;
uniform texture2d hs_pass_{name};
uniform texture2d hs_pass_{name}_previous;
uniform float3 hs_pass_{name}_resolution;""")
    passes.append(f"""float4 PSStage{i}(VertData v):TARGET {{
 float4 c=hs_pass_{previous}.Sample(simSampler,v.uv);
 float4 p=hs_pass_{name}_previous.Sample(simSampler,v.uv);
 bool good=abs(c.a-{i-1}.0)<0.01 && c.b<0.01 &&
   (hs_graph_frame==0 ? abs(p.a-1.0)<0.01 : abs(p.a-{i}.0)<0.01) &&
   length(hs_resolution.xy-hs_pass_{name}_resolution.xy)<0.01;
 return float4(c.rg,good?0.0:1.0,{i}.0);
}}
technique Pass_{name} {{ pass {{vertex_shader=VSDefault(v);pixel_shader=PSStage{i}(v);}} }}""")
    previous=name
# Final Draw is the only place we replace the last-stage reference.
begin=source.index("float4 PSFinal")
end=source.index("technique Pass_resolve",begin)
final=source[begin:end].replace("hs_pass_resolve.Sample","hs_pass_stage7.Sample").replace("c.a-2.0","c.a-7.0")
source=source[:begin]+"\n".join(passes)+"\n"+final+source[end:]
(work/"ordered-eight.effect").write_text("\n".join(decl)+"\n"+source)
PYGRAPH
run_case() {
    local shader="$1" mode="$2" log="$work/$2.log"
    mkdir -p "$work/$mode/home" "$work/$mode/config"
    local extra=()
    if [[ "$mode" == channels || "$mode" == named ]]; then extra+=("$work/channel.png"); fi
    if [[ "$mode" == ordered ]]; then extra+=("$generated"); fi
    (cd "$work/$mode" && HOME="$work/$mode/home" XDG_CONFIG_HOME="$work/$mode/config" \
      xvfb-run -a -s '-screen 0 1280x720x24' timeout 40 "$work/test" "$module" "$data" "$shader" "$mode" "${extra[@]}") >"$log" 2>&1 || {
        tail -130 "$log"; return 1;
    }
    echo "TEST graph $mode"
    grep -E '^TWO_BUFFER_' "$log"
    grep -q '^TWO_BUFFER_RUNTIME_PASS=1' "$log"
    if [[ "$mode" == invalid ]]; then
        grep -q 'Invalid graph buffer A descriptor' "$log"
    elif [[ "$mode" == budget ]]; then
        grep -q 'Graph resource budget exceeded' "$log"
        test "$(grep -c 'Graph resource budget exceeded' "$log")" -eq 2
    elif [[ "$mode" == ordered ]]; then
        grep -q "Named pass graph enabled: 3 passes" "$log"
        if [[ "$(grep -c 'Invalid named pass graph:' "$log")" -ne 6 ]]; then
            grep -E 'bad-.*effect|Invalid named pass graph|Shader load failed|Detail:' "$log" | tail -80
            return 1
        fi
    elif [[ "$mode" == source-builder ]]; then
        grep -q "Named pass graph enabled: 2 passes" "$log"
    elif [[ "$mode" == source-graph ]]; then
        grep -q "Named pass graph enabled: 3 passes" "$log"
    elif [[ "$mode" == source-graph-eight ]]; then
        grep -q "Named pass graph enabled: 8 passes" "$log"
    elif [[ "$mode" == ordered-eight ]]; then
        grep -q "Named pass graph enabled: 8 passes" "$log"
    elif [[ "$mode" == named ]]; then
        grep -q 'Invalid named texture input' "$log"
        grep -q 'TWO_BUFFER_NAMED_RELOAD retained=19 switched=19 reset_labels=1 PASS=1' "$log"
    elif [[ "$mode" == live-source || "$mode" == named-live-source ]]; then
        grep -q "Loaded shader: shaders/tests/$(basename "$shader")" "$log"
    else
        grep -q 'Two-buffer graph enabled: A -> B -> Draw' "$log"
    fi
}
if [[ $# -ge 3 ]]; then
    selected="$(realpath "$3")"
    if [[ "$selected" == "$original_data/"* ]]; then
        selected="$data/${selected#"$original_data/"}"
    fi
    run_case "$selected" "${4:-default}"
else
    run_case "$data/shaders/tests/two_buffer_graph.effect" default
    run_case "$data/shaders/tests/two_buffer_scaled_graph.effect" scaled
    run_case "$data/shaders/tests/ordered_pass_graph.effect" ordered
    run_case "$data/shaders/tests/ordered_pass_graph.effect" source-graph
    run_case "$generated/ordered-eight.effect" ordered-eight
    run_case "$generated/ordered-eight.effect" source-graph-eight
    run_case "$data/shaders/tests/two_buffer_scaled_graph.effect" source-graph-ab
    run_case "$data/shaders/tests/two_buffer_invalid_graph.effect" invalid
    run_case "$data/shaders/tests/two_buffer_budget_graph.effect" budget
    run_case "$data/shaders/tests/two_buffer_channel_graph.effect" channels
    run_case "$data/shaders/tests/two_buffer_named_graph.effect" named
    run_case "$data/shaders/tests/two_buffer_live_channel_graph.effect" live
    run_case "$data/shaders/tests/live_channel_source.effect" live-source
    run_case "$data/shaders/tests/named_live_channel_source.effect" named-live-source
fi
