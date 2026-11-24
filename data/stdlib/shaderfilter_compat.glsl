// ===================================================================
// shaderfilter_compat.glsl
// Compatibility layer for obs-shaderfilter (exeldro) shaders
// Include this to use shaders written for obs-shaderfilter in HyperShader
// ===================================================================

#ifndef SHADERFILTER_COMPAT_GLSL
#define SHADERFILTER_COMPAT_GLSL

// Build this compatibility adapter on the native HyperShader runtime.
#include "hypershader_standard.glsl"

// === TIME UNIFORMS ===
// obs-shaderfilter uses elapsed_time, we use iTime
#define elapsed_time iTime
#define elapsed_time_start iTime
#define elapsed_time_show iTime
#define elapsed_time_active iTime
#define elapsed_time_enable iTime

// === RESOLUTION UNIFORMS ===
// obs-shaderfilter uses uv_size and uv_pixel_interval
#define uv_size iResolution.xy
#define uv_pixel_interval float2(1.0 / iResolution.x, 1.0 / iResolution.y)

// === RANDOM UNIFORMS ===
// obs-shaderfilter provides various random floats
// We generate pseudo-random using time
#define rand_f frac(sin(iTime * 12.9898) * 43758.5453)
#define rand_activation_f frac(sin(iFrame * 78.233) * 43758.5453)
#define rand_instance_f 0.5

// === TIME-BASED UNIFORMS ===
// Loop-based timing
#define local_time frac(iTime)
#define loops int(iTime)
#define loop_second frac(iTime)

// Current time (rough approximation using iDate if available)
#define current_time_sec int(fmod(iTime, 60.0))
#define current_time_min int(fmod(iTime / 60.0, 60.0))
#define current_time_hour int(fmod(iTime / 3600.0, 24.0))
#define current_time_ms int(fmod(iTime * 1000.0, 1000.0))

// === UV TRANSFORMS ===
// obs-shaderfilter uses uv_offset and uv_scale for border expansion
#define uv_offset float2(0.0, 0.0)
#define uv_scale float2(1.0, 1.0)

// === TEXTURE ACCESS ===
// obs-shaderfilter uses previous_output for feedback
#define previous_output iChannel0
#define prevous_output iChannel0
#define prevous_image iChannel0

// === STANDARD PIXEL SHADER WRAPPER ===
// obs-shaderfilter shaders often define PSEffect or mainImage
// This macro wraps a PSEffect-style shader

// Usage for .shader files (pixel shader only):
// Define your function as: float4 PSEffect(VertData v_in) : TARGET { /* body */ }
// Then at the end: SHADERFILTER_EFFECT_WRAPPER

#define SHADERFILTER_EFFECT_WRAPPER \
    technique Draw { \
        pass { \
            vertex_shader = VSDefault(v_in); \
            pixel_shader  = PSEffect(v_in); \
        } \
    }

// For simple mainImage-style shaders from obs-shaderfilter:
// Already handled by HYPERSHADER_MAINIMAGE_WRAPPER from hypershader_standard.glsl

#endif // SHADERFILTER_COMPAT_GLSL
