// hypershader Standard Library - Text & SDF Rendering
// Utilities for SDF font rendering and text effects

#ifndef _STDLIB_TEXT_H_
#define _STDLIB_TEXT_H_

// === SDF TEXT SAMPLING ===

// Basic SDF text sampling with anti-aliasing
// sdfValue: sampled value from SDF font texture (0.5 = edge)
// smoothing: anti-aliasing amount (typically 0.1 / fontSize)
float sdfText(float sdfValue, float smoothing) {
    return smoothstep(0.5 - smoothing, 0.5 + smoothing, sdfValue);
}

// SDF text with custom threshold
float sdfTextThreshold(float sdfValue, float threshold, float smoothing) {
    return smoothstep(threshold - smoothing, threshold + smoothing, sdfValue);
}

// Automatic smoothing based on screen-space derivatives
float sdfTextAuto(float sdfValue) {
    float smoothing = fwidth(sdfValue);
    return smoothstep(0.5 - smoothing, 0.5 + smoothing, sdfValue);
}

// === SDF TEXT EFFECTS ===

// Outline effect
// Returns: x = fill alpha, y = outline alpha
float2 sdfOutline(float sdfValue, float outlineWidth, float smoothing) {
    float fill = smoothstep(0.5 - smoothing, 0.5 + smoothing, sdfValue);
    float outline = smoothstep(0.5 - outlineWidth - smoothing, 0.5 - outlineWidth + smoothing, sdfValue);
    return float2(fill, outline - fill);
}

// Colored outline
float4 sdfTextWithOutline(float sdfValue, float4 fillColor, float4 outlineColor, float outlineWidth, float smoothing) {
    float2 masks = sdfOutline(sdfValue, outlineWidth, smoothing);
    return fillColor * masks.x + outlineColor * masks.y;
}

// Drop shadow
float sdfShadow(sampler2D sdfTex, float2 uv, float2 shadowOffset, float shadowBlur) {
    float shadowSdf = texture(sdfTex, uv - shadowOffset).a;
    return smoothstep(0.5 - shadowBlur, 0.5 + shadowBlur, shadowSdf);
}

// Glow effect
float sdfGlow(float sdfValue, float glowSize, float glowFalloff) {
    float dist = 0.5 - sdfValue;
    float glow = 1.0 - smoothstep(0.0, glowSize, dist);
    return pow(glow, glowFalloff);
}

// Inner glow
float sdfInnerGlow(float sdfValue, float glowSize, float glowFalloff) {
    float dist = sdfValue - 0.5;
    float glow = 1.0 - smoothstep(0.0, glowSize, dist);
    return pow(glow, glowFalloff) * step(0.5, sdfValue);
}

// Complete SDF text with all effects
float4 sdfTextComplete(float sdfValue, float4 fillColor, float4 outlineColor, float4 glowColor,
                        float outlineWidth, float glowSize, float smoothing) {
    float fill = sdfText(sdfValue, smoothing);
    float2 outlineMasks = sdfOutline(sdfValue, outlineWidth, smoothing);
    float glow = sdfGlow(sdfValue, glowSize, 2.0);
    
    float4 result = glowColor * glow * (1.0 - outlineMasks.y - fill);
    result += outlineColor * outlineMasks.y;
    result += fillColor * fill;
    
    return result;
}

// === PROCEDURAL DIGITS ===

// 7-segment digit lookup (0-9)
// Returns 1.0 if segment is on, 0.0 if off
// Segments: top, top-right, bottom-right, bottom, bottom-left, top-left, middle
float digitSegment(int digit, int segment) {
    // Bit patterns for digits 0-9 (7 segments each)
    const int patterns[10] = int[](
        0x7E, // 0: 1111110
        0x30, // 1: 0110000
        0x6D, // 2: 1101101
        0x79, // 3: 1111001
        0x33, // 4: 0110011
        0x5B, // 5: 1011011
        0x5F, // 6: 1011111
        0x70, // 7: 1110000
        0x7F, // 8: 1111111
        0x7B  // 9: 1111011
    );
    
    return float((patterns[digit] >> (6 - segment)) & 1);
}

// Render a 7-segment digit
// uv: 0-1 coordinates within digit space
// Returns alpha value
float sevenSegmentDigit(float2 uv, int digit, float thickness, float smoothing) {
    float result = 0.0;
    
    // Segment positions and orientations
    // Horizontal segments
    float hSeg = abs(uv.x - 0.5);
    float vSeg = abs(uv.y - 0.5);
    
    // Top segment
    if (digitSegment(digit, 0) > 0.0) {
        float d = max(abs(uv.x - 0.5), abs(uv.y - 0.85));
        result = max(result, smoothstep(thickness + smoothing, thickness - smoothing, d) * step(abs(uv.y - 0.85), 0.1));
    }
    
    // Top-right segment
    if (digitSegment(digit, 1) > 0.0) {
        float d = max(abs(uv.x - 0.8), abs(uv.y - 0.65));
        result = max(result, smoothstep(thickness + smoothing, thickness - smoothing, d) * step(abs(uv.x - 0.8), 0.1));
    }
    
    // Bottom-right segment
    if (digitSegment(digit, 2) > 0.0) {
        float d = max(abs(uv.x - 0.8), abs(uv.y - 0.35));
        result = max(result, smoothstep(thickness + smoothing, thickness - smoothing, d) * step(abs(uv.x - 0.8), 0.1));
    }
    
    // Bottom segment
    if (digitSegment(digit, 3) > 0.0) {
        float d = max(abs(uv.x - 0.5), abs(uv.y - 0.15));
        result = max(result, smoothstep(thickness + smoothing, thickness - smoothing, d) * step(abs(uv.y - 0.15), 0.1));
    }
    
    // Bottom-left segment
    if (digitSegment(digit, 4) > 0.0) {
        float d = max(abs(uv.x - 0.2), abs(uv.y - 0.35));
        result = max(result, smoothstep(thickness + smoothing, thickness - smoothing, d) * step(abs(uv.x - 0.2), 0.1));
    }
    
    // Top-left segment
    if (digitSegment(digit, 5) > 0.0) {
        float d = max(abs(uv.x - 0.2), abs(uv.y - 0.65));
        result = max(result, smoothstep(thickness + smoothing, thickness - smoothing, d) * step(abs(uv.x - 0.2), 0.1));
    }
    
    // Middle segment
    if (digitSegment(digit, 6) > 0.0) {
        float d = max(abs(uv.x - 0.5), abs(uv.y - 0.5));
        result = max(result, smoothstep(thickness + smoothing, thickness - smoothing, d) * step(abs(uv.y - 0.5), 0.1));
    }
    
    return result;
}

// Render a multi-digit number
float renderNumber(float2 uv, int number, int maxDigits, float spacing, float thickness) {
    float result = 0.0;
    int remaining = number;
    
    for (int i = 0; i < maxDigits; i++) {
        int digit = remaining % 10;
        remaining /= 10;
        
        float xOffset = float(maxDigits - 1 - i) * spacing;
        float2 digitUV = float2(uv.x - xOffset, uv.y);
        
        if (digitUV.x >= 0.0 && digitUV.x <= 1.0) {
            result = max(result, sevenSegmentDigit(digitUV, digit, thickness, 0.02));
        }
        
        if (remaining == 0 && i > 0) break;
    }
    
    return result;
}

// === ASCII ART CONVERSION ===

// ASCII brightness levels (darkest to brightest)
// Standard character set: " .:-=+*#%@"
float asciiIndex(float brightness, float levels) {
    return floor(brightness * levels) / levels;
}

// Sample ASCII character from grid texture
// charTex: texture with characters arranged in grid (e.g., 16x16)
// charIndex: 0-1 (which character to use based on brightness)
// localUV: UV within the character cell
float2 asciiCharUV(float2 localUV, float charIndex, float2 gridSize) {
    float charId = floor(charIndex * (gridSize.x * gridSize.y));
    float2 charPos = float2(
        mod(charId, gridSize.x),
        floor(charId / gridSize.x)
    );
    return (charPos + localUV) / gridSize;
}

// Complete ASCII art effect
float4 asciiArt(sampler2D inputTex, sampler2D charTex, float2 uv, float2 resolution, 
                float2 charSize, float2 gridSize) {
    // Calculate which character cell we're in
    float2 cellCount = resolution / charSize;
    float2 cellId = floor(uv * cellCount);
    float2 cellUV = frac(uv * cellCount);
    
    // Sample center of cell for brightness
    float2 sampleUV = (cellId + 0.5) / cellCount;
    float4 sampleColor = texture(inputTex, sampleUV);
    float brightness = dot(sampleColor.rgb, float3(0.299, 0.587, 0.114));
    
    // Get character UV
    float2 charUV = asciiCharUV(cellUV, brightness, gridSize);
    float charAlpha = texture(charTex, charUV).r;
    
    return float4(sampleColor.rgb * charAlpha, charAlpha);
}

// === TEXT ANIMATION ===

// Typewriter effect (reveal characters over time)
float typewriter(float2 uv, float progress, float charWidth) {
    float charIndex = uv.x / charWidth;
    return step(charIndex, progress);
}

// Wave text animation
float2 textWave(float2 uv, float time, float amplitude, float frequency, float speed) {
    float wave = sin(uv.x * frequency + time * speed) * amplitude;
    return float2(uv.x, uv.y + wave);
}

// Shake text animation
float2 textShake(float2 uv, float time, float intensity) {
    float shakeX = sin(time * 50.0) * intensity;
    float shakeY = cos(time * 47.0) * intensity;
    return uv + float2(shakeX, shakeY);
}

// Glitch text effect
float2 textGlitch(float2 uv, float time, float intensity, float blockSize) {
    float block = floor(uv.y / blockSize);
    float noise = frac(sin(block * 12.9898 + floor(time * 10.0)) * 43758.5453);
    
    if (noise > 0.95) {
        uv.x += (noise - 0.5) * intensity;
    }
    
    return uv;
}

// === TEXT MASKING ===

// Use text as mask for video/image
float4 textMaskedVideo(sampler2D videoTex, sampler2D textTex, float2 uv) {
    float4 video = texture(videoTex, uv);
    float textMask = texture(textTex, uv).a;
    return video * textMask;
}

// Knockout text (show video through text shape)
float4 knockoutText(sampler2D videoTex, sampler2D textTex, float2 uv, float4 bgColor) {
    float4 video = texture(videoTex, uv);
    float textMask = texture(textTex, uv).a;
    return lerp(bgColor, video, textMask);
}

#endif
