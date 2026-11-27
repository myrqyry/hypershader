// --- hypershader Native Standard Library ---
// Foundation for first-party hypershader effects.
// External-format adapters may depend on this file; native effects never depend on them.

#ifndef HYPERSHADER_STANDARD_H
#define HYPERSHADER_STANDARD_H

// GLSL-style authoring conveniences supported by hypershader's OBS effect layer.
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define sampler2D texture2d
// NOTE: mat2/float2x2 is not portable across OBS graphics backends.
#define mat3 float3x3
#define mat4 float4x4
#define fract frac
#define mix lerp
#define atan(v) atan2((v), 1.0f)
#define texture(t, uv) t.Sample(textureSampler, uv)
#define textureLod(t, uv, lod) t.SampleLevel(textureSampler, uv, lod)

// Native hypershader runtime contract.
uniform float4x4 ViewProj;
uniform texture2d image;

uniform float hs_transition_progress;
uniform float hs_time;
uniform float3 hs_resolution;
uniform float hs_scale;
uniform float4 hs_pointer;
uniform float4 hs_date;
uniform float hs_delta_time;
uniform int hs_frame_index;
uniform float hs_frame_rate;
uniform float hs_sample_rate;

uniform texture2d hs_input0;
uniform texture2d hs_input1;
uniform texture2d hs_input2;
uniform texture2d hs_input3;

uniform float4 hs_input_resolution0;
uniform float4 hs_input_resolution1;
uniform float4 hs_input_resolution2;
uniform float4 hs_input_resolution3;

#define hs_input_resolution(i) (i==0?hs_input_resolution0:i==1?hs_input_resolution1:i==2?hs_input_resolution2:hs_input_resolution3)

uniform float audio_level;
uniform float audio_peak;
uniform float audio_beat;     /* short onset impulse, ~60ms decay, 0 when silent */
uniform float audio_envelope; /* sustained beat envelope 0..1, attack ~instant / release ~220ms */
// Existing FFT-derived onset/tempo signals. No extra audio capture.
uniform float audio_kick;
uniform float audio_snare;
uniform float audio_flux;
uniform float audio_bpm;
uniform float audio_beat_phase;
uniform float audio_bar_phase;
uniform float audio_beat_confidence;
uniform float audio_band0;
uniform float audio_band1;
uniform float audio_band2;
uniform float audio_band3;
uniform float audio_band4;
uniform float audio_band5;
uniform float audio_band6;
uniform float audio_band7;

#define audio_band(i) (i==0?audio_band0:i==1?audio_band1:i==2?audio_band2:i==3?audio_band3:i==4?audio_band4:i==5?audio_band5:i==6?audio_band6:audio_band7)

sampler_state textureSampler {
    Filter    = Linear;
    AddressU  = Clamp;
    AddressV  = Clamp;
};

struct VertData {
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

VertData VSDefault(VertData v_in) {
    VertData vert_out;
    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    vert_out.uv  = v_in.uv;
    return vert_out;
}

// Convenience wrapper for simple native effects that expose:
// void hs_shade_pixel(out vec4 fragColor, in vec2 fragCoord)
#define HYPERSHADER_PIXEL_WRAPPER \
    float4 PSMain(VertData v_in) : TARGET { \
        float2 fragCoord = v_in.uv * hs_resolution.xy; \
        float4 fragColor; \
        hs_shade_pixel(fragColor, fragCoord); \
        return fragColor; \
    } \
    technique Draw { \
        pass { \
            vertex_shader = VSDefault(v_in); \
            pixel_shader  = PSMain(v_in); \
        } \
    }

#endif
