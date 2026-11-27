// ===================================================================
// shaderfilter_compat.glsl
// Compatibility layer for obs-shaderfilter (exeldro) shaders
// Include this to use shaders written for obs-shaderfilter in hypershader
// ===================================================================

#ifndef SHADERFILTER_COMPAT_GLSL
#define SHADERFILTER_COMPAT_GLSL

// Build this compatibility adapter on the native hypershader runtime.
#include "hypershader_standard.glsl"

// === TIME UNIFORMS ===
// obs-shaderfilter uses elapsed_time, we use hs_time
#define elapsed_time hs_time
#define elapsed_time_start hs_time
#define elapsed_time_show hs_time
#define elapsed_time_active hs_time
#define elapsed_time_enable hs_time

// === RESOLUTION UNIFORMS ===
// obs-shaderfilter uses uv_size and uv_pixel_interval
#define uv_size hs_resolution.xy
#define uv_pixel_interval float2(1.0 / hs_resolution.x, 1.0 / hs_resolution.y)

// === RANDOM UNIFORMS ===
// obs-shaderfilter provides various random floats
// We generate pseudo-random using time
#define rand_f frac(sin(hs_time * 12.9898) * 43758.5453)
#define rand_activation_f frac(sin(hs_frame_index * 78.233) * 43758.5453)
#define rand_instance_f 0.5

// === TIME-BASED UNIFORMS ===
// Loop-based timing
#define local_time frac(hs_time)
#define loops int(hs_time)
#define loop_second frac(hs_time)

// Current time (rough approximation using hs_date if available)
#define current_time_sec int(fmod(hs_time, 60.0))
#define current_time_min int(fmod(hs_time / 60.0, 60.0))
#define current_time_hour int(fmod(hs_time / 3600.0, 24.0))
#define current_time_ms int(fmod(hs_time * 1000.0, 1000.0))

// === UV TRANSFORMS ===
// obs-shaderfilter uses uv_offset and uv_scale for border expansion
#define uv_offset float2(0.0, 0.0)
#define uv_scale float2(1.0, 1.0)

// === TEXTURE ACCESS ===
// obs-shaderfilter uses previous_output for feedback
#define previous_output hs_input0
#define prevous_output hs_input0
#define prevous_image hs_input0

// === STANDARD PIXEL SHADER WRAPPER ===
// obs-shaderfilter shaders often define PSEffect or hs_shade_pixel
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

// For simple hs_shade_pixel-style shaders from obs-shaderfilter:
// Already handled by HYPERSHADER_PIXEL_WRAPPER from hypershader_standard.glsl

#endif // SHADERFILTER_COMPAT_GLSL
