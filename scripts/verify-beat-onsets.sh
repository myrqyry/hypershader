#!/usr/bin/env bash
# Real libobs -> pulsed PCM -> audio worker -> audio_beat / audio_envelope -> pixels.
# Proves the beat uniforms reach a shader on transients and fall back to zero in silence.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hyperShaderFX.so}"
data="${2:-$repo/data}"
[ -f "$module" ] || { echo "missing module: $module" >&2; exit 1; }

work="$(mktemp -d /tmp/hyper-beat-onsets-XXXXXX)"
trap 'rm -rf "$work"' EXIT

inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-beat-onsets.cpp" \
    -o "$work/probe" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"

# The shader loader rejects effect paths outside the data directory, so stage a
# minimal data root that holds only the probe effect.
mkdir -p "$work/data/shaders/root"
cat > "$work/data/shaders/root/beat_probe.effect" <<'EOF'
uniform float4x4 ViewProj;
uniform texture2d image;

uniform float audio_level;    // @audio "level" @gain 1.0
uniform float audio_envelope;
uniform float audio_beat;

sampler_state def_sampler
{
    Filter   = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertData
{
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

VertData VSDefault(VertData v_in)
{
    VertData v_out;
    v_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    v_out.uv  = v_in.uv;
    return v_out;
}

float4 PSBeat(VertData v_in) : TARGET
{
    return float4(saturate(audio_level), saturate(audio_envelope), saturate(audio_beat), 1.0);
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader  = PSBeat(v_in);
    }
}
EOF

if ! (cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' timeout 90 \
      "$work/probe" "$module" "$work/data" "$work/data/shaders/root/beat_probe.effect") \
      >"$work/result.log" 2>&1; then
    tail -30 "$work/result.log"
    exit 1
fi

grep -q 'Shader load failed' "$work/result.log" && { tail -20 "$work/result.log"; exit 1; }
grep -E '^BEAT_PROBE ' "$work/result.log"