// --- HyperShader Native Standard Library ---
// Foundation for first-party HyperShader effects.
// External-format adapters may depend on this file; native effects never depend on them.

#ifndef HYPERSHADER_STANDARD_H
#define HYPERSHADER_STANDARD_H

// GLSL-style authoring conveniences supported by HyperShader's OBS effect layer.
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

// Native HyperShader runtime contract.
uniform float4x4 ViewProj;
uniform texture2d image;

uniform float iProgress;
uniform float iTime;
uniform float3 iResolution;
uniform float iScale;
uniform float4 iMouse;
uniform float4 iDate;
uniform float iTimeDelta;
uniform int iFrame;
uniform float iFrameRate;
uniform float iSampleRate;

uniform texture2d iChannel0;
uniform texture2d iChannel1;
uniform texture2d iChannel2;
uniform texture2d iChannel3;

uniform float4 iChannelResolution0;
uniform float4 iChannelResolution1;
uniform float4 iChannelResolution2;
uniform float4 iChannelResolution3;

#define iChannelResolution(i) (i==0?iChannelResolution0:i==1?iChannelResolution1:i==2?iChannelResolution2:iChannelResolution3)

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
// void mainImage(out vec4 fragColor, in vec2 fragCoord)
#define HYPERSHADER_MAINIMAGE_WRAPPER \
    float4 PSMain(VertData v_in) : TARGET { \
        float2 fragCoord = v_in.uv * iResolution.xy; \
        float4 fragColor; \
        mainImage(fragColor, fragCoord); \
        return fragColor; \
    } \
    technique Draw { \
        pass { \
            vertex_shader = VSDefault(v_in); \
            pixel_shader  = PSMain(v_in); \
        } \
    }

#endif
