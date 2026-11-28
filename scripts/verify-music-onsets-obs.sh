#!/usr/bin/env bash
# Real libobs PCM -> existing Hyper audio worker -> new musical uniforms -> GPU.
# 440 Hz is mid-range; expect snare-band onset and spectral flux, not kick.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
work="$(mktemp -d /tmp/hyper-music-onsets-XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/shaders/root"
cat > "$work/data/shaders/root/music_probe.effect" <<'SHADER'
uniform float4x4 ViewProj;
uniform texture2d image;

uniform float audio_level;    // @audio "level" @gain 1.0
uniform float audio_snare;
uniform float audio_flux;
uniform float alias_snare; // @audio "snare" @gain 1 @smooth 0
uniform float alias_flux; // @audio "flux" @gain 1 @smooth 0

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
    return float4(saturate(audio_level), saturate(min(audio_snare,alias_snare)), saturate(min(audio_flux,alias_flux)), 1.0);
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader  = PSBeat(v_in);
    }
}
SHADER
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-beat-onsets.cpp" \
  -o "$work/probe" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
if ! (cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' timeout 90 \
 "$work/probe" "$module" "$work/data" "$work/data/shaders/root/music_probe.effect") \
 >"$work/result.log" 2>&1;then tail -45 "$work/result.log";exit 1;fi
grep 'BEAT_PROBE .*PASS=1' "$work/result.log"
# Independent real bass transient: use a 94 Hz PCM fixture and require
# the same shader binding to deliver audio_kick (not audio_snare).
python3 - "$repo" "$work" <<'PYINNER'
from pathlib import Path
import sys
repo,work=map(Path,sys.argv[1:])
source=(repo/"scripts/verify-beat-onsets.cpp").read_text()
assert "two_pi * 440.0 / 48000.0" in source
(work/"bass.cpp").write_text(source.replace("two_pi * 440.0 / 48000.0","two_pi * 94.0 / 48000.0"))
shader=(work/"data/shaders/root/music_probe.effect").read_text()
assert "audio_snare" in shader
(work/"data/shaders/root/bass_probe.effect").write_text(shader.replace("audio_snare","audio_kick").replace("alias_snare","alias_kick").replace('@audio "snare"','@audio "kick"').replace("uniform float audio_kick;", 'uniform float audio_kick; // @audio "kick"'))
PYINNER
g++ -std=c++17 -O2 -I"$inc" "$work/bass.cpp" -o "$work/bass"   $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
if ! (cd "$work" && xvfb-run -a -s '-screen 0 1280x720x24' timeout 90  "$work/bass" "$module" "$work/data" "$work/data/shaders/root/bass_probe.effect")  >"$work/bass.log" 2>&1;then tail -45 "$work/bass.log";exit 1;fi
grep 'BEAT_PROBE .*PASS=1' "$work/bass.log"
if grep -E "Parameter '(iAudio|audio_)[^']*' set to invalid size" "$work/result.log" "$work/bass.log"; then
  echo "Native audio uniform upload failed" >&2
  exit 1
fi
echo "NATIVE_AUDIO_UPLOAD_SIZE_PASS=1"
