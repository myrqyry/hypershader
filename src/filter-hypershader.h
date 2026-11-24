/**
 * @file filter-hypershader.h
 * @brief OBS Studio plugin for native HyperShader effects.
 *
 * HyperShaderFX hosts metadata-driven OBS effect shaders with native inputs,
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
 * - `uniform vec3 iResolution;`
 *   - Viewport resolution (in pixels): x=width, y=height, z=aspect ratio (width/height).
 * - `uniform float iTime;`
 *   - Shader playback time (in seconds).
 * - `uniform float iTimeDelta;`
 *   - Render time since last frame (in seconds).
 * - `uniform int iFrame;`
 *   - Frame count since playback started.
 * - `uniform float iFrameRate;`
 *   - Estimated frames per second.
 * - `uniform vec4 iMouse;`
 *   - Mouse coordinates: xy = current, zw = click.
 * - `uniform vec4 iDate;`
 *   - (Year, Month 0-11, Day, TimeInSeconds).
 * - `uniform float iSampleRate;`
 *   - Audio sample rate (e.g., 48000.0).
 *
 * @section Inputs
 * - `uniform sampler2D iChannel0..3;`
 *   - Input textures (Images or OBS Sources).
 * - `uniform vec3 iChannelResolution[4];`
 *   - Resolution of each channel texture.
 * - `uniform float iChannelTime[4];`
 *   - Time for each channel (synced to iTime).
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
