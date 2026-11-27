// hypershader Standard Library - Glitch & Digital Effects
// Digital artifact simulation and corruption effects

#ifndef _STDLIB_GLITCH_H_
#define _STDLIB_GLITCH_H_

// --- RGB Split/Shift ---
// Channel separation effect
float3 rgbShift(Texture2D tex, float2 uv, float2 rOffset, float2 gOffset, float2 bOffset) {
    float r = texture(tex, uv + rOffset).r;
    float g = texture(tex, uv + gOffset).g;
    float b = texture(tex, uv + bOffset).b;
    return float3(r, g, b);
}

// Animated RGB shift using time
float3 rgbShiftAnimated(Texture2D tex, float2 uv, float time, float strength) {
    float2 rOff = float2(sin(time * 3.7) * strength, cos(time * 2.3) * strength * 0.5);
    float2 gOff = float2(0.0, 0.0);
    float2 bOff = float2(cos(time * 4.1) * strength, sin(time * 2.9) * strength * 0.5);
    return rgbShift(tex, uv, rOff, gOff, bOff);
}

// --- Scanline Glitch ---
// Horizontal displacement on random scanlines
float2 uvScanlineGlitch(float2 uv, float time, float intensity, float lineHeight) {
    // Generate pseudo-random per-line noise
    float line = floor(uv.y / lineHeight);
    float noise = frac(sin(line * 12.9898 + time * 43758.5453) * 43758.5453);
    
    // Only glitch some lines (threshold controls frequency)
    if (noise > 0.95) {
        float offset = (noise - 0.95) * 20.0 * intensity;
        uv.x += offset * sign(noise - 0.975);
    }
    
    return uv;
}

// --- Block Glitch ---
// Rectangular region displacement
float2 uvBlockGlitch(float2 uv, float time, float2 blockSize, float intensity, float seed) {
    float2 block = floor(uv / blockSize);
    float noise = frac(sin(dot(block, float2(12.9898, 78.233)) + time * 0.5 + seed) * 43758.5453);
    
    if (noise > 0.92) {
        // Random offset for this block
        float2 offset = float2(
            frac(sin(noise * 127.1) * 43758.5453) - 0.5,
            frac(sin(noise * 269.5) * 43758.5453) - 0.5
        ) * intensity;
        
        return uv + offset;
    }
    
    return uv;
}

// --- VHS Tracking Lines ---
// Horizontal noise bands like VHS tracking errors
float vhsTracking(float2 uv, float time, float speed, float thickness) {
    float band = sin(uv.y * 50.0 + time * speed);
    band = smoothstep(1.0 - thickness, 1.0, band);
    return band;
}

// --- Static/Snow Noise ---
// TV static effect
float3 staticNoise(float2 uv, float time, float intensity) {
    float noise = frac(sin(dot(uv * time, float2(12.9898, 78.233))) * 43758.5453);
    return float3(noise) * intensity;
}

// --- Color Quantization ---
// Reduce color depth for posterize/banding effect
float3 quantizeColor(float3 color, float levels) {
    return floor(color * levels) / (levels - 1.0);
}

// --- Bit Crush (for color values) ---
// Simulate low bit depth
float3 bitCrush(float3 color, float bits) {
    float levels = pow(2.0, bits);
    return floor(color * levels) / levels;
}

// --- Interlacing ---
// Simulate interlaced video (even/odd field offset)
float2 uvInterlace(float2 uv, float2 resolution, float offset, bool evenField) {
    float line = floor(uv.y * resolution.y);
    bool isEven = mod(line, 2.0) < 1.0;
    
    if (isEven == evenField) {
        uv.x += offset;
    }
    
    return uv;
}

// --- Jitter ---
// Random per-frame position shake
float2 uvJitter(float2 uv, float time, float intensity) {
    float2 jitter = float2(
        frac(sin(time * 12.9898) * 43758.5453) - 0.5,
        frac(sin(time * 78.233) * 43758.5453) - 0.5
    ) * intensity;
    
    return uv + jitter;
}

// --- Wave Distortion (VHS-style) ---
float2 uvVHSWarp(float2 uv, float time) {
    float wave = sin(uv.y * 20.0 + time * 5.0) * 0.002;
    wave += sin(uv.y * 100.0 + time * 10.0) * 0.001;
    return float2(uv.x + wave, uv.y);
}

// --- Dropout Effect ---
// Random missing lines (VHS damage simulation)
float dropoutMask(float2 uv, float time, float threshold) {
    float line = floor(uv.y * 480.0);
    float noise = frac(sin(line * 12.9898 + floor(time * 30.0) * 43758.5453) * 43758.5453);
    return noise > threshold ? 1.0 : 0.0;
}

// --- Digital Corruption ---
// Simulate JPEG/video compression artifacts
float3 digitalCorrupt(float3 color, float2 uv, float time, float intensity) {
    // Block-based color shift
    float2 block = floor(uv * 16.0);
    float blockNoise = frac(sin(dot(block + floor(time * 2.0), float2(12.9898, 78.233))) * 43758.5453);
    
    if (blockNoise > 1.0 - intensity) {
        // Shift color values
        float shift = (blockNoise - (1.0 - intensity)) / intensity;
        color = lerp(color, color.gbr, shift);
    }
    
    return color;
}

// --- Hologram Flicker ---
// Simulate unstable holographic display
float hologramFlicker(float time, float speed, float minBrightness) {
    float flicker = sin(time * speed) * sin(time * speed * 0.7) * sin(time * speed * 1.3);
    flicker = flicker * 0.5 + 0.5;
    return lerp(minBrightness, 1.0, flicker);
}

// --- Chromatic Tear ---
// Directional color separation for motion blur effect
float3 chromaticTear(Texture2D tex, float2 uv, float2 direction, float strength) {
    float3 result;
    result.r = texture(tex, uv - direction * strength).r;
    result.g = texture(tex, uv).g;
    result.b = texture(tex, uv + direction * strength).b;
    return result;
}

// --- Composite Glitch Effect ---
// Combines multiple glitch effects
float3 compositeGlitch(Texture2D tex, float2 uv, float time, float intensity) {
    // Apply various glitches based on intensity
    float2 glitchUV = uv;
    
    // Scanline displacement
    if (intensity > 0.3) {
        glitchUV = uvScanlineGlitch(glitchUV, time, intensity * 0.1, 0.01);
    }
    
    // Block glitch
    if (intensity > 0.5) {
        glitchUV = uvBlockGlitch(glitchUV, time, float2(0.1, 0.05), intensity * 0.2, 0.0);
    }
    
    // RGB shift
    float3 color;
    if (intensity > 0.2) {
        color = rgbShiftAnimated(tex, glitchUV, time, intensity * 0.02);
    } else {
        color = texture(tex, glitchUV).rgb;
    }
    
    // Quantization at high intensity
    if (intensity > 0.7) {
        float levels = lerp(256.0, 8.0, (intensity - 0.7) / 0.3);
        color = quantizeColor(color, levels);
    }
    
    return color;
}

#endif
