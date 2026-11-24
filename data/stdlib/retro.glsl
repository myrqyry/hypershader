// HyperShader Standard Library - Retro & Analog Video
// Contains advanced functions for CRT, NTSC, and Scanline simulation.

#ifndef _STDLIB_RETRO_H_
#define _STDLIB_RETRO_H_

// --- NTSC Color Space ---
// Standard YIQ conversion matrix for NTSC signals
float3 rgb2yiq(float3 c) {
    return float3(
        0.299 * c.r + 0.587 * c.g + 0.114 * c.b,
        0.596 * c.r - 0.275 * c.g - 0.321 * c.b,
        0.212 * c.r - 0.523 * c.g + 0.311 * c.b
    );
}

float3 yiq2rgb(float3 c) {
    return float3(
        1.0 * c.x + 0.956 * c.y + 0.621 * c.z,
        1.0 * c.x - 0.272 * c.y - 0.647 * c.z,
        1.0 * c.x - 1.105 * c.y + 1.702 * c.z
    );
}

// --- Distortion ---
// Applies barrel distortion to UV coordinates.
// strength: 0.0 to 0.2 is typical.
vec2 retro_curve(vec2 uv, float strength) {
    uv = (uv - 0.5) * 2.0;
    vec2 offset = abs(uv.yx) / vec2(6.0, 4.0);
    uv = uv + uv * offset * offset * strength * 10.0;
    uv = uv * 0.5 + 0.5;
    return uv;
}

// --- Mask / Phosphor ---
// Simulates an Aperture Grille (RGB vertical stripes)
// str: 0.0 (off) to 1.0 (full black mask)
float3 retro_maskAperture(float3 color, float2 fragCoord, float str) {
    if (str <= 0.0) return color;
    
    int p = int(mod(fragCoord.x, 3.0));
    vec3 mask = vec3(str);
    if (p == 0) mask.r = 1.0;
    else if (p == 1) mask.g = 1.0;
    else mask.b = 1.0;
    
    return color * mask;
}

// Simulates a Shadow Mask (Staggered dots)
float3 retro_maskShadow(float3 color, float2 fragCoord, float str) {
    if (str <= 0.0) return color;
    
    vec2 pos = floor(fragCoord);
    float m = mod(pos.x + mod(pos.y, 2.0), 2.0);
    return color * (1.0 - m * str);
}

// --- Scanlines ---
// Better scanline function with width control
// count: Number of lines (usually iResolution.y for 1:1)
// opacity: 0.0 to 1.0
// width: 0.5 (soft) to 2.0 (sharp)
float3 retro_scanlines(float3 color, float2 uv, float count, float opacity, float width) {
    float s = sin(uv.y * count * 3.14159 * 2.0);
    s = s * 0.5 + 0.5;
    s = pow(s, width);
    float mod = 1.0 - (1.0 - s) * opacity;
    return color * mod;
}

// --- Dithering ---
// Ordered Dithering (Bayer 4x4)
// Reduces color banding by adding patterned noise.
float retro_ditherBayer(vec2 pos) {
    const float bayer[16] = float[](
        0.0/16.0, 8.0/16.0, 2.0/16.0, 10.0/16.0,
        12.0/16.0, 4.0/16.0, 14.0/16.0, 6.0/16.0,
        3.0/16.0, 11.0/16.0, 1.0/16.0, 9.0/16.0,
        15.0/16.0, 7.0/16.0, 13.0/16.0, 5.0/16.0
    );
    int x = int(mod(pos.x, 4.0));
    int y = int(mod(pos.y, 4.0));
    return bayer[y * 4 + x] - 0.5;
}

// ============================================================================
// PS1-STYLE EFFECTS
// Inspired by PlayStation 1 hardware limitations
// ============================================================================

// --- PS1 UV Jitter ---
// Snaps UV coordinates to a low-resolution grid, creating vertex wobble look
// level: lower = more jitter (80-400 typical, PS1 was ~256)
vec2 ps1_jitter(vec2 uv, float level) {
    return floor(uv * level) / level;
}

// PS1 jitter with animation (simulates vertex swimming)
vec2 ps1_jitter_animated(vec2 uv, float level, float time, float intensity) {
    // Add slight time-based wobble
    float wobbleX = sin(uv.y * 20.0 + time * 3.0) * intensity * 0.002;
    float wobbleY = cos(uv.x * 20.0 + time * 2.5) * intensity * 0.002;
    vec2 jittered = uv + vec2(wobbleX, wobbleY);
    return floor(jittered * level) / level;
}

// --- PS1 Color Depth ---
// Reduces colors to 15-bit (5 bits per channel like PS1)
// levels: 32 for PS1 accuracy, lower for more extreme
vec3 ps1_colorDepth(vec3 color, float levels) {
    return floor(color * levels + 0.5) / levels;
}

// --- PS1 Affine Texture Warping ---
// Simulates the lack of perspective-correct texturing
// Creates the wavy/warped look on angled surfaces
vec2 ps1_affineWarp(vec2 uv, float intensity) {
    // Simulate perspective error by warping based on distance from center
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    float warp = sin(dist * 10.0) * intensity * 0.05;
    return uv + centered * warp;
}

// --- PS1 Complete Filter ---
// All PS1 effects combined for authentic look
vec3 ps1_filter(texture2d tex, vec2 uv, vec2 resolution, float jitterLevel, float colorLevels, float time) {
    // Apply jitter to UV
    vec2 jitteredUV = ps1_jitter_animated(uv, jitterLevel, time, 1.0);
    
    // Sample with jittered UV
    vec3 color = texture(tex, jitteredUV).rgb;
    
    // Reduce color depth
    color = ps1_colorDepth(color, colorLevels);
    
    // Add dithering to reduce banding
    float dither = retro_ditherBayer(uv * resolution) / colorLevels;
    color += dither * 0.5;
    color = clamp(color, 0.0, 1.0);
    
    return color;
}

// --- PS1 Audio-Reactive Jitter ---
// Jitter intensity responds to audio
vec2 ps1_jitter_audio(vec2 uv, float baseLevel, float audioLevel) {
    float level = baseLevel - audioLevel * 100.0; // Audio increases jitter
    level = max(level, 30.0); // Minimum resolution
    return floor(uv * level) / level;
}

// --- VHS-style Tracking Error ---
// Horizontal displacement like VHS tape tracking issues
vec2 vhs_tracking(vec2 uv, float time, float intensity) {
    float offset = sin(uv.y * 50.0 + time * 5.0) * intensity;
    offset += sin(uv.y * 120.0 + time * 8.0) * intensity * 0.3;
    return vec2(uv.x + offset * 0.02, uv.y);
}

// --- Composite Video Bleeding ---
// Simulates color bleeding from composite video (like PS1 RF output)
vec3 composite_bleed(texture2d tex, vec2 uv, float2 resolution, float amount) {
    vec3 color = texture(tex, uv).rgb;
    
    // Sample horizontally for color bleeding
    float bleedDist = amount / resolution.x;
    vec3 left = texture(tex, uv - vec2(bleedDist, 0.0)).rgb;
    vec3 right = texture(tex, uv + vec2(bleedDist, 0.0)).rgb;
    
    // Blend chrominance (keep luma sharp)
    float3 yiq = rgb2yiq(color);
    float3 yiqLeft = rgb2yiq(left);
    float3 yiqRight = rgb2yiq(right);
    
    // Average IQ components
    yiq.yz = (yiq.yz + yiqLeft.yz + yiqRight.yz) / 3.0;
    
    return yiq2rgb(yiq);
}

// --- Palette Color Lookup ---
// Maps image colors to a palette texture (like indexed color)
// paletteSize: number of colors in palette width (e.g., 16, 32, 64)
vec3 palette_lookup(texture2d paletteTex, vec3 color, float paletteSize) {
    // Use average luminance to pick palette color
    float luma = (color.r + color.g + color.b) / 3.0;
    luma = floor(luma * paletteSize) / paletteSize;
    return texture(paletteTex, vec2(luma, 0.5)).rgb;
}

// Nearest color in palette (better quality, slower)
vec3 palette_nearest(texture2d paletteTex, vec3 color, int paletteSize) {
    float minDist = 9999.0;
    vec3 nearest = color;
    
    for (int i = 0; i < paletteSize; i++) {
        float u = (float(i) + 0.5) / float(paletteSize);
        vec3 paletteColor = texture(paletteTex, vec2(u, 0.5)).rgb;
        float dist = distance(color, paletteColor);
        if (dist < minDist) {
            minDist = dist;
            nearest = paletteColor;
        }
    }
    return nearest;
}

// --- SNES-style Mode 7 ---
// Perspective floor effect (like F-Zero, Mario Kart)
vec2 mode7_uv(vec2 uv, float horizon, float scale, float angle) {
    // Only apply below horizon
    if (uv.y < horizon) return vec2(-1.0); // Above horizon
    
    float y = uv.y - horizon;
    float z = horizon / y; // Perspective division
    
    float s = sin(angle);
    float c = cos(angle);
    
    vec2 rotated;
    rotated.x = (uv.x - 0.5) * z * scale;
    rotated.y = z * scale;
    
    // Apply rotation
    return vec2(
        rotated.x * c - rotated.y * s,
        rotated.x * s + rotated.y * c
    );
}

// --- 8x8 Bayer Matrix (better quality dithering) ---
float retro_ditherBayer8(vec2 pos) {
    const float bayer8[64] = float[](
        0.0/64.0,32.0/64.0, 8.0/64.0,40.0/64.0, 2.0/64.0,34.0/64.0,10.0/64.0,42.0/64.0,
       48.0/64.0,16.0/64.0,56.0/64.0,24.0/64.0,50.0/64.0,18.0/64.0,58.0/64.0,26.0/64.0,
       12.0/64.0,44.0/64.0, 4.0/64.0,36.0/64.0,14.0/64.0,46.0/64.0, 6.0/64.0,38.0/64.0,
       60.0/64.0,28.0/64.0,52.0/64.0,20.0/64.0,62.0/64.0,30.0/64.0,54.0/64.0,22.0/64.0,
        3.0/64.0,35.0/64.0,11.0/64.0,43.0/64.0, 1.0/64.0,33.0/64.0, 9.0/64.0,41.0/64.0,
       51.0/64.0,19.0/64.0,59.0/64.0,27.0/64.0,49.0/64.0,17.0/64.0,57.0/64.0,25.0/64.0,
       15.0/64.0,47.0/64.0, 7.0/64.0,39.0/64.0,13.0/64.0,45.0/64.0, 5.0/64.0,37.0/64.0,
       63.0/64.0,31.0/64.0,55.0/64.0,23.0/64.0,61.0/64.0,29.0/64.0,53.0/64.0,21.0/64.0
    );
    int x = int(mod(pos.x, 8.0));
    int y = int(mod(pos.y, 8.0));
    return bayer8[y * 8 + x] - 0.5;
}

// --- Dithered color quantization ---
// Combines dithering with quantization for better gradients
vec3 retro_quantizeDithered(vec3 color, vec2 pos, float levels) {
    float dither = retro_ditherBayer(pos) / levels;
    color += dither;
    return floor(color * levels + 0.5) / levels;
}

// --- CRT Phosphor Persistence (ghosting) ---
// Simulates phosphor decay - blend with previous frame
vec3 crt_persistence(vec3 current, vec3 previous, float decay) {
    return max(current, previous * decay);
}

// --- Interlaced Display ---
// Simulates interlaced video (alternating lines per frame)
vec3 retro_interlace(texture2d tex, vec2 uv, vec2 resolution, float frame) {
    float line = floor(uv.y * resolution.y);
    bool isEvenFrame = mod(frame, 2.0) < 1.0;
    bool isEvenLine = mod(line, 2.0) < 1.0;
    
    // On even frames, show even lines; on odd frames, show odd lines
    // For the "off" lines, blend with neighbors
    if (isEvenFrame != isEvenLine) {
        vec3 above = texture(tex, uv - vec2(0.0, 1.0 / resolution.y)).rgb;
        vec3 below = texture(tex, uv + vec2(0.0, 1.0 / resolution.y)).rgb;
        return (above + below) * 0.5;
    }
    
    return texture(tex, uv).rgb;
}

#endif
