// HyperShader Standard Library - Screen-Space Effects
// Depth-based and screen-coordinate effects

#ifndef _STDLIB_SCREENSPACE_H_
#define _STDLIB_SCREENSPACE_H_

// === DEPTH UTILITIES ===

// Linear eye depth from raw depth buffer value
float linearEyeDepth(float rawDepth, float4 zBufferParams) {
    return 1.0 / (zBufferParams.z * rawDepth + zBufferParams.w);
}

// Linear 01 depth (0 at camera, 1 at far plane)
float linear01Depth(float rawDepth, float4 zBufferParams) {
    return 1.0 / (zBufferParams.x * rawDepth + zBufferParams.y);
}

// === DEPTH RIM / EDGE DETECTION ===

// Sobel sample points for edge detection
const int2 sobelPoints[9] = {
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0), int2(0,  0), int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1)
};

// Scale rim width based on depth (consistent appearance at any distance)
float scaleRimByDepth(float rimWidth, float depth, float2 screenSize) {
    float scale = 1.0 / depth;
    return rimWidth * 50.0 / screenSize.y * scale;
}

// Depth-based rim/outline detection
// Returns 0-1 edge intensity
float depthRim(sampler2D depthTex, float2 screenUV, float modelDepth, 
               float rimWidth, float threshold, float sharpness, float2 texelSize) {
    float depthStatus = 0.0;
    float scaledWidth = scaleRimByDepth(rimWidth, modelDepth, 1.0 / texelSize);
    
    for (int i = 0; i < 9; i++) {
        float2 offset = float2(sobelPoints[i]) * scaledWidth * texelSize;
        float sampleDepth = linearEyeDepth(texture(depthTex, screenUV + offset).r, _ZBufferParams);
        depthStatus += step(modelDepth + threshold, sampleDepth);
    }
    
    float edgeFactor = depthStatus / 9.0;
    return pow(edgeFactor, sharpness);
}

// === TOUCH/INTERSECTION REACTIVE ===

// Depth intersection effect (glows where objects intersect)
float touchIntersection(float sceneDepth, float fragmentDepth, float radius, float hardness) {
    float depthDiff = sceneDepth - fragmentDepth;
    float intersect = 1.0 - smoothstep(0.0, radius, depthDiff);
    return pow(max(intersect, 0.0), hardness);
}

// Rainbow touch effect
float3 touchRainbow(float depthDiff, float time, float speed, float spread) {
    float hue = depthDiff * spread + time * speed;
    return 0.5 + 0.5 * cos(TAU * (hue + float3(0.0, 0.33, 0.67)));
}

// === SCREEN-SPACE REFLECTIONS (SSR) ===

// Planar SSR - simple screen-space reflection
float2 ssrPlanarUV(float2 screenUV, float3 reflectDir, float3x3 viewMatrix, float parallax) {
    float3 viewRefl = mul(viewMatrix, reflectDir);
    float parallaxAmount = parallax * saturate(1.0 - viewRefl.z);
    return screenUV + viewRefl.xy * parallaxAmount;
}

// SSR with distortion
float2 ssrDistortedUV(float2 screenUV, float3 reflectDir, float3x3 viewMatrix, 
                       float parallax, float2 distortion) {
    float2 baseUV = ssrPlanarUV(screenUV, reflectDir, viewMatrix, parallax);
    return baseUV + distortion;
}

// SSR blur sampling (radial)
float3 ssrBlurred(sampler2D grabTex, float2 uv, float2 texelSize, float blurAmount, int samples) {
    float3 color = texture(grabTex, uv).rgb;
    float2 blurOffset = texelSize * blurAmount;
    
    for (int i = 0; i < samples; i++) {
        float angle = float(i) / float(samples) * TAU;
        float2 offset = float2(cos(angle), sin(angle)) * blurOffset;
        color += texture(grabTex, uv + offset).rgb;
    }
    
    return color / float(samples + 1);
}

// === REFRACTION ===

// Basic refraction UV offset
float2 refractionUV(float2 screenUV, float3 normal, float ior) {
    return screenUV + normal.xy * ior;
}

// Chromatic aberration for refraction
float3 refractionChromatic(sampler2D grabTex, float2 uv, float caOffset) {
    float3 color;
    color.r = texture(grabTex, uv + float2(caOffset, 0.0)).r;
    color.g = texture(grabTex, uv).g;
    color.b = texture(grabTex, uv - float2(caOffset, 0.0)).b;
    return color;
}

// Refraction with zoom effect
float2 refractionZoom(float2 uv, float3 normal, float zoom) {
    float normalFactor = frac(dot(normal, float3(12.9898, 78.233, 37.719))) * 2.0 - 1.0;
    float zoomFactor = 1.0 - (normalFactor * zoom);
    float2 center = float2(0.5);
    return (uv - center) * zoomFactor + center;
}

// === DISTANCE FADE ===

// Calculate fade factor based on camera distance
float distanceFade(float3 worldPos, float3 cameraPos, float nearStart, float nearEnd, 
                   float farStart, float farEnd, bool useNear, bool useFar) {
    float dist = distance(worldPos, cameraPos);
    float fade = 1.0;
    
    if (useNear && nearStart > nearEnd) {
        fade *= smoothstep(nearEnd, nearStart, dist);
    }
    
    if (useFar && farEnd > farStart) {
        fade *= 1.0 - smoothstep(farStart, farEnd, dist);
    }
    
    return fade;
}

// === DITHERING PATTERNS ===

// Tilted checkerboard pattern (stylized dithering)
float tiltedCheckerboard(float2 screenPos, float scale) {
    float u = screenPos.x + screenPos.y;
    float v = screenPos.x - screenPos.y;
    float2 gridPos = floor(float2(u, v) / scale);
    return fmod(gridPos.x + gridPos.y, 2.0);
}

// Apply dithered transparency
float applyDither(float alpha, float2 screenPos, float amount, float scale) {
    float pattern = tiltedCheckerboard(screenPos, scale);
    return lerp(alpha, alpha * pattern, amount);
}

// Screen-door transparency (useful for VR/stereo)
float screenDoor(float2 screenPos, int level) {
    // Level 0-4 for different transparency amounts
    int2 pos = int2(screenPos) % 4;
    int index = pos.y * 4 + pos.x;
    
    // Bayer 4x4 thresholds
    const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (bayer[index] < level * 4) ? 1.0 : 0.0;
}

// === SCREEN EFFECTS ===

// Vignette based on screen coordinates
float screenVignette(float2 uv, float intensity, float smoothness) {
    float2 centered = uv - 0.5;
    float dist = length(centered);
    return 1.0 - smoothstep(0.5 - smoothness, 0.5, dist * intensity);
}

// Screen-space scanlines
float screenScanlines(float2 screenPos, float density, float intensity) {
    float line = sin(screenPos.y * density * PI) * 0.5 + 0.5;
    return 1.0 - (1.0 - line) * intensity;
}

#endif
