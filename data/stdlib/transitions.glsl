// hypershader Standard Library - Transition Effects
// Smooth blending and transition algorithms between two sources

#ifndef _STDLIB_TRANSITIONS_H_
#define _STDLIB_TRANSITIONS_H_

// --- Basic Transitions ---

// Linear crossfade (standard dissolve)
float4 transitionDissolve(float4 colorA, float4 colorB, float t) {
    return lerp(colorA, colorB, t);
}

// Fade through black/color
float4 transitionFadeThrough(float4 colorA, float4 colorB, float t, float4 fadeColor) {
    if (t < 0.5) {
        return lerp(colorA, fadeColor, t * 2.0);
    } else {
        return lerp(fadeColor, colorB, (t - 0.5) * 2.0);
    }
}

// --- Luminance-Based Transitions ---

// Luma wipe: transition based on brightness (dark/light pixels transition first)
float4 transitionLumaWipe(float4 colorA, float4 colorB, float t, float sharpness) {
    float lumaA = dot(colorA.rgb, float3(0.299, 0.587, 0.114));
    
    // Map luma to transition threshold
    float threshold = t * (1.0 + 1.0 / sharpness);
    float transition = saturate((threshold - lumaA) * sharpness);
    
    return lerp(colorA, colorB, transition);
}

// Inverse luma wipe (light pixels transition first)
float4 transitionLumaWipeInverse(float4 colorA, float4 colorB, float t, float sharpness) {
    float lumaA = 1.0 - dot(colorA.rgb, float3(0.299, 0.587, 0.114));
    float threshold = t * (1.0 + 1.0 / sharpness);
    float transition = saturate((threshold - lumaA) * sharpness);
    return lerp(colorA, colorB, transition);
}

// --- Directional Wipes ---

// Linear wipe from one direction
// direction: 0=left, 1=right, 2=top, 3=bottom
float4 transitionLinearWipe(float4 colorA, float4 colorB, float2 uv, float t, int direction, float softness) {
    float progress;
    
    if (direction == 0) progress = uv.x;           // Left to right
    else if (direction == 1) progress = 1.0 - uv.x; // Right to left
    else if (direction == 2) progress = uv.y;       // Top to bottom
    else progress = 1.0 - uv.y;                     // Bottom to top
    
    float edge = smoothstep(t - softness, t + softness, progress);
    return lerp(colorB, colorA, edge);
}

// Radial wipe from center
float4 transitionRadialWipe(float4 colorA, float4 colorB, float2 uv, float2 center, float t, float softness) {
    float dist = distance(uv, center);
    float maxDist = 0.707; // sqrt(0.5) - max distance from center to corner
    float normalizedDist = dist / maxDist;
    
    float edge = smoothstep(t - softness, t + softness, normalizedDist);
    return lerp(colorB, colorA, edge);
}

// Clock wipe (rotational)
float4 transitionClockWipe(float4 colorA, float4 colorB, float2 uv, float2 center, float t, float softness) {
    float2 delta = uv - center;
    float angle = atan2(delta.y, delta.x);
    float normalizedAngle = (angle + PI) / TAU; // 0-1 range
    
    float edge = smoothstep(t - softness, t + softness, normalizedAngle);
    return lerp(colorB, colorA, edge);
}

// --- Effect-Based Transitions ---

// Pixelate transition (pixelate in, then out)
float2 uvPixelateTransition(float2 uv, float2 resolution, float t, float maxPixelSize) {
    // Pixelation peaks at t=0.5
    float pixelAmount = 1.0 - abs(t - 0.5) * 2.0;
    float pixelSize = lerp(1.0, maxPixelSize, pixelAmount);
    
    float2 pixels = resolution / pixelSize;
    return floor(uv * pixels) / pixels;
}

// Blur transition amount (blur peaks at midpoint)
float transitionBlurAmount(float t, float maxBlur) {
    float blurAmount = 1.0 - abs(t - 0.5) * 2.0;
    return blurAmount * maxBlur;
}

// Zoom transition (zoom in A, zoom out to B)
float2 uvZoomTransition(float2 uv, float2 center, float t, float maxZoom) {
    float2 delta = uv - center;
    
    // Zoom peaks at t=0.5
    float zoomProgress = 1.0 - abs(t - 0.5) * 2.0;
    float zoom = lerp(1.0, maxZoom, zoomProgress);
    
    return center + delta / zoom;
}

// Spin/rotate transition
float2 uvSpinTransition(float2 uv, float2 center, float t, float rotations) {
    float2 delta = uv - center;
    float angle = t * rotations * TAU;
    
    float s = sin(angle);
    float c = cos(angle);
    
    return center + float2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);
}

// --- Sliding Transitions ---

// Slide in from direction
float2 uvSlideTransition(float2 uv, float t, int direction) {
    float2 offset;
    
    if (direction == 0) offset = float2(1.0 - t, 0.0);      // From left
    else if (direction == 1) offset = float2(t - 1.0, 0.0); // From right  
    else if (direction == 2) offset = float2(0.0, 1.0 - t); // From top
    else offset = float2(0.0, t - 1.0);                     // From bottom
    
    return uv + offset;
}

// Push transition (A slides out, B slides in)
float4 transitionPush(Texture2D texA, Texture2D texB, float2 uv, float t, int direction) {
    float2 offsetA, offsetB;
    
    if (direction == 0) {
        offsetA = float2(-t, 0.0);
        offsetB = float2(1.0 - t, 0.0);
    } else if (direction == 1) {
        offsetA = float2(t, 0.0);
        offsetB = float2(t - 1.0, 0.0);
    } else if (direction == 2) {
        offsetA = float2(0.0, -t);
        offsetB = float2(0.0, 1.0 - t);
    } else {
        offsetA = float2(0.0, t);
        offsetB = float2(0.0, t - 1.0);
    }
    
    float2 uvA = uv + offsetA;
    float2 uvB = uv + offsetB;
    
    bool inA = (uvA.x >= 0.0 && uvA.x <= 1.0 && uvA.y >= 0.0 && uvA.y <= 1.0);
    bool inB = (uvB.x >= 0.0 && uvB.x <= 1.0 && uvB.y >= 0.0 && uvB.y <= 1.0);
    
    if (inA) return texture(texA, uvA);
    if (inB) return texture(texB, uvB);
    return float4(0.0);
}

// --- Pattern-Based Transitions ---

// Grid/blinds transition
float4 transitionBlinds(float4 colorA, float4 colorB, float2 uv, float t, float numBlinds, bool horizontal) {
    float coord = horizontal ? uv.x : uv.y;
    float blindPos = frac(coord * numBlinds);
    
    float edge = step(blindPos, t);
    return lerp(colorA, colorB, edge);
}

// Diamond/iris transition
float4 transitionDiamond(float4 colorA, float4 colorB, float2 uv, float2 center, float t, float softness) {
    float2 delta = abs(uv - center);
    float dist = delta.x + delta.y; // Manhattan distance = diamond shape
    float maxDist = 1.0;
    float normalizedDist = dist / maxDist;
    
    float edge = smoothstep(t - softness, t + softness, normalizedDist);
    return lerp(colorB, colorA, edge);
}

// Checkerboard transition
float4 transitionCheckerboard(float4 colorA, float4 colorB, float2 uv, float t, float gridSize) {
    float2 cell = floor(uv * gridSize);
    float checker = mod(cell.x + cell.y, 2.0);
    
    // Stagger timing based on checker pattern
    float adjustedT = t * 2.0 - checker;
    adjustedT = saturate(adjustedT);
    
    return lerp(colorA, colorB, adjustedT);
}

#endif
