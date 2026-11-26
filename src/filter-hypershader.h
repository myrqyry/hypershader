/**
 * @file filter-hypershader.h
 * @brief OBS Studio plugin for native hypershader effects.
 *
 * hypershader hosts metadata-driven OBS effect shaders with native inputs,
 * audio analysis, persistence, scene data, and optional compatibility adapters
 * for external shader formats.
 */

#pragma once

/**
 * @page shader_api Shader API Reference
 * 
 * The following uniforms are automatically bound to shaders by this plugin:
 *
 * @section Standard Uniforms
 * - `uniform vec3 hs_resolution;`
 *   - Viewport resolution (in pixels): x=width, y=height, z=aspect ratio (width/height).
 * - `uniform float hs_time;`
 *   - Shader playback time (in seconds).
 * - `uniform float hs_delta_time;`
 *   - Render time since last frame (in seconds).
 * - `uniform int hs_frame_index;`
 *   - Frame count since playback started.
 * - `uniform float hs_frame_rate;`
 *   - Estimated frames per second.
 * - `uniform vec4 hs_pointer;`
 *   - Mouse coordinates: xy = current, zw = click.
 * - `uniform vec4 hs_date;`
 *   - (Year, Month 0-11, Day, TimeInSeconds).
 * - `uniform float hs_sample_rate;`
 *   - Audio sample rate (e.g., 48000.0).
 *
 * @section Inputs
 * - `uniform sampler2D hs_input0..3;`
 *   - Input textures (Images or OBS Sources).
 * - `uniform float4 hs_input_resolution0..3;`
 *   - Resolution of each channel texture.
 * - `uniform float hs_input_time0..3;`
 *   - Time for each channel (synced to hs_time).
 *
 * @section Audio
 * If audio analysis is enabled, the following custom metadata tags can be used in shader comments
 * to map audio features to uniforms:
 * 
 * - `// uniform float myBass; @audio "bass" @gain 2.0 @smooth 0.5`
 * 
 * Supported tags: "bass", "mid", "treble", "level", "peak".
 */

/* 
 * This header is primarily for documentation purposes as the plugin 
 * implementation is currently self-contained in filter-hypershader.c
 */
