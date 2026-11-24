// HyperShader Standard Library - Blur Functions
// Mathematical implementations of standard blur algorithms

#ifndef _STDLIB_BLUR_H_
#define _STDLIB_BLUR_H_

// --- Gaussian Distribution ---
// The mathematical Gaussian function: g(x) = (1/σ√2π) * e^(-x²/2σ²)
// Common knowledge from probability/statistics, not copyrightable
float gaussian1D(float x, float sigma) {
    if (sigma < 0.0001) return 1.0; // Avoid division by zero
    float twoSigmaSq = 2.0 * sigma * sigma;
    return exp(-(x * x) / twoSigmaSq) / (sqrt(TAU) * sigma);
}

// 2D Gaussian (separable property: G2D(x,y) = G1D(x) * G1D(y))
float gaussian2D(float2 offset, float sigma) {
    return gaussian1D(offset.x, sigma) * gaussian1D(offset.y, sigma);
}

// --- Box Blur (Simple Average) ---
// Samples in a square kernel
float4 boxBlur(Texture2D tex, float2 uv, float2 texelSize, int radius) {
    float4 sum = float4(0.0);
    float count = 0.0;
    
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            float2 offset = float2(x, y) * texelSize;
            sum += texture(tex, uv + offset);
            count += 1.0;
        }
    }
    return sum / count;
}

// --- Separable Gaussian Blur (Single Direction) ---
// Run twice (horizontal then vertical) for full blur
// direction: float2(1,0) for horizontal, float2(0,1) for vertical
float4 gaussianBlur1D(Texture2D tex, float2 uv, float2 texelSize, float2 direction, float sigma, int samples) {
    float4 result = float4(0.0);
    float weightSum = 0.0;
    
    // Center sample
    float centerWeight = gaussian1D(0.0, sigma);
    result += texture(tex, uv) * centerWeight;
    weightSum += centerWeight;
    
    // Symmetric samples on both sides
    for (int i = 1; i <= samples; i++) {
        float weight = gaussian1D(float(i), sigma);
        float2 offset = direction * texelSize * float(i);
        
        result += texture(tex, uv + offset) * weight;
        result += texture(tex, uv - offset) * weight;
        weightSum += weight * 2.0;
    }
    
    return result / weightSum;
}

// --- Directional Blur (Motion Blur Style) ---
// angle in radians, strength is blur distance
float4 directionalBlur(Texture2D tex, float2 uv, float2 texelSize, float angle, float strength, int samples) {
    float2 direction = float2(cos(angle), sin(angle));
    float4 result = float4(0.0);
    float weightSum = 0.0;
    
    for (int i = 0; i < samples; i++) {
        float t = float(i) / float(samples - 1) - 0.5; // -0.5 to 0.5
        float2 offset = direction * t * strength * texelSize;
        float weight = gaussian1D(t * 2.0, 0.4); // Weight by gaussian for smooth falloff
        
        result += texture(tex, uv + offset) * weight;
        weightSum += weight;
    }
    
    return result / weightSum;
}

// --- Radial Blur (Zoom Effect) ---
// center: blur origin (0.5, 0.5 for screen center)
// strength: intensity of blur
float4 radialBlur(Texture2D tex, float2 uv, float2 center, float strength, int samples) {
    float4 result = float4(0.0);
    float2 dir = uv - center;
    
    for (int i = 0; i < samples; i++) {
        float t = float(i) / float(samples);
        float scale = 1.0 - strength * t;
        float2 sampleUV = center + dir * scale;
        result += texture(tex, sampleUV);
    }
    
    return result / float(samples);
}

// --- Kawase Blur (Fast Approximation) ---
// Efficient blur using bilinear filtering exploitation
// Pass multiple times with increasing offset for stronger blur
float4 kawaseBlur(Texture2D tex, float2 uv, float2 texelSize, float offset) {
    float4 result = float4(0.0);
    
    result += texture(tex, uv + float2(-offset - 0.5, -offset - 0.5) * texelSize);
    result += texture(tex, uv + float2(-offset - 0.5,  offset + 0.5) * texelSize);
    result += texture(tex, uv + float2( offset + 0.5, -offset - 0.5) * texelSize);
    result += texture(tex, uv + float2( offset + 0.5,  offset + 0.5) * texelSize);
    
    return result * 0.25;
}

// --- Dual Kawase Downsample ---
// For efficient two-pass blur: downsample then upsample
float4 dualKawaseDown(Texture2D tex, float2 uv, float2 texelSize) {
    float4 sum = texture(tex, uv) * 4.0;
    sum += texture(tex, uv - texelSize);
    sum += texture(tex, uv + texelSize);
    sum += texture(tex, uv + float2(texelSize.x, -texelSize.y));
    sum += texture(tex, uv + float2(-texelSize.x, texelSize.y));
    return sum / 8.0;
}

// --- Dual Kawase Upsample ---
float4 dualKawaseUp(Texture2D tex, float2 uv, float2 texelSize) {
    float4 sum = float4(0.0);
    sum += texture(tex, uv + float2(-texelSize.x * 2.0, 0.0));
    sum += texture(tex, uv + float2(-texelSize.x, texelSize.y)) * 2.0;
    sum += texture(tex, uv + float2(0.0, texelSize.y * 2.0));
    sum += texture(tex, uv + float2(texelSize.x, texelSize.y)) * 2.0;
    sum += texture(tex, uv + float2(texelSize.x * 2.0, 0.0));
    sum += texture(tex, uv + float2(texelSize.x, -texelSize.y)) * 2.0;
    sum += texture(tex, uv + float2(0.0, -texelSize.y * 2.0));
    sum += texture(tex, uv + float2(-texelSize.x, -texelSize.y)) * 2.0;
    return sum / 12.0;
}

#endif
