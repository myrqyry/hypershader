// HyperShader Standard Library - Edge Detection & Enhancement
// Convolution-based edge detection algorithms

#ifndef _STDLIB_EDGES_H_
#define _STDLIB_EDGES_H_

// --- Sobel Edge Detection ---
// Classic gradient-based edge detection
// Returns edge magnitude (0-1 range) and optional gradient direction
float sobelEdge(Texture2D tex, float2 uv, float2 texelSize) {
    // Sample 3x3 neighborhood
    float tl = lumaRec709(texture(tex, uv + float2(-1, -1) * texelSize).rgb);
    float tc = lumaRec709(texture(tex, uv + float2( 0, -1) * texelSize).rgb);
    float tr = lumaRec709(texture(tex, uv + float2( 1, -1) * texelSize).rgb);
    float ml = lumaRec709(texture(tex, uv + float2(-1,  0) * texelSize).rgb);
    float mr = lumaRec709(texture(tex, uv + float2( 1,  0) * texelSize).rgb);
    float bl = lumaRec709(texture(tex, uv + float2(-1,  1) * texelSize).rgb);
    float bc = lumaRec709(texture(tex, uv + float2( 0,  1) * texelSize).rgb);
    float br = lumaRec709(texture(tex, uv + float2( 1,  1) * texelSize).rgb);
    
    // Sobel kernels
    float gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
    float gy = (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr);
    
    return sqrt(gx * gx + gy * gy);
}

// Sobel with direction output (for normal mapping, etc.)
float2 sobelGradient(Texture2D tex, float2 uv, float2 texelSize) {
    float tl = lumaRec709(texture(tex, uv + float2(-1, -1) * texelSize).rgb);
    float tc = lumaRec709(texture(tex, uv + float2( 0, -1) * texelSize).rgb);
    float tr = lumaRec709(texture(tex, uv + float2( 1, -1) * texelSize).rgb);
    float ml = lumaRec709(texture(tex, uv + float2(-1,  0) * texelSize).rgb);
    float mr = lumaRec709(texture(tex, uv + float2( 1,  0) * texelSize).rgb);
    float bl = lumaRec709(texture(tex, uv + float2(-1,  1) * texelSize).rgb);
    float bc = lumaRec709(texture(tex, uv + float2( 0,  1) * texelSize).rgb);
    float br = lumaRec709(texture(tex, uv + float2( 1,  1) * texelSize).rgb);
    
    float gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
    float gy = (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr);
    
    return float2(gx, gy);
}

// --- Prewitt Edge Detection ---
// Simpler/faster than Sobel
float prewittEdge(Texture2D tex, float2 uv, float2 texelSize) {
    float tl = lumaRec709(texture(tex, uv + float2(-1, -1) * texelSize).rgb);
    float tc = lumaRec709(texture(tex, uv + float2( 0, -1) * texelSize).rgb);
    float tr = lumaRec709(texture(tex, uv + float2( 1, -1) * texelSize).rgb);
    float ml = lumaRec709(texture(tex, uv + float2(-1,  0) * texelSize).rgb);
    float mr = lumaRec709(texture(tex, uv + float2( 1,  0) * texelSize).rgb);
    float bl = lumaRec709(texture(tex, uv + float2(-1,  1) * texelSize).rgb);
    float bc = lumaRec709(texture(tex, uv + float2( 0,  1) * texelSize).rgb);
    float br = lumaRec709(texture(tex, uv + float2( 1,  1) * texelSize).rgb);
    
    // Prewitt kernels (uniform weights)
    float gx = (tr + mr + br) - (tl + ml + bl);
    float gy = (bl + bc + br) - (tl + tc + tr);
    
    return sqrt(gx * gx + gy * gy);
}

// --- Laplacian Edge Detection ---
// Second derivative - finds zero crossings
float laplacianEdge(Texture2D tex, float2 uv, float2 texelSize) {
    float center = lumaRec709(texture(tex, uv).rgb);
    
    float top    = lumaRec709(texture(tex, uv + float2( 0, -1) * texelSize).rgb);
    float bottom = lumaRec709(texture(tex, uv + float2( 0,  1) * texelSize).rgb);
    float left   = lumaRec709(texture(tex, uv + float2(-1,  0) * texelSize).rgb);
    float right  = lumaRec709(texture(tex, uv + float2( 1,  0) * texelSize).rgb);
    
    // Laplacian: sum of neighbors - 4 * center
    return abs(top + bottom + left + right - 4.0 * center);
}

// --- Sharpen Filter ---
// Enhance edges by subtracting blurred version
float3 sharpen(Texture2D tex, float2 uv, float2 texelSize, float strength) {
    float3 center = texture(tex, uv).rgb;
    
    float3 blur = (
        texture(tex, uv + float2(-1,  0) * texelSize).rgb +
        texture(tex, uv + float2( 1,  0) * texelSize).rgb +
        texture(tex, uv + float2( 0, -1) * texelSize).rgb +
        texture(tex, uv + float2( 0,  1) * texelSize).rgb
    ) * 0.25;
    
    // Unsharp mask: original + strength * (original - blur)
    return center + strength * (center - blur);
}

// --- High Pass Filter ---
// Extract high frequency details only
float3 highPass(Texture2D tex, float2 uv, float2 texelSize, float radius) {
    float3 center = texture(tex, uv).rgb;
    
    // Simple blur for low frequency
    float3 blur = float3(0.0);
    float count = 0.0;
    int iRadius = int(radius);
    
    for (int y = -iRadius; y <= iRadius; y++) {
        for (int x = -iRadius; x <= iRadius; x++) {
            blur += texture(tex, uv + float2(x, y) * texelSize).rgb;
            count += 1.0;
        }
    }
    blur /= count;
    
    // High pass = original - low pass, shifted to 0.5 center
    return (center - blur) + 0.5;
}

// --- Outline Effect ---
// Create cartoon-style outlines
float3 outline(Texture2D tex, float2 uv, float2 texelSize, float threshold, float3 outlineColor) {
    float edge = sobelEdge(tex, uv, texelSize);
    float3 original = texture(tex, uv).rgb;
    
    return edge > threshold ? outlineColor : original;
}

// --- Sprite Outline ---
// Draw a border where transparent pixels neighbor opaque ones.
// thickness: number of texel steps (1 = thin, 2+ = thicker)
float4 spriteOutline(Texture2D tex, float2 uv, float2 texelSize, int thickness,
                     float4 borderColor) {
    float centerAlpha = texture(tex, uv).a;
    if (centerAlpha > 0.5) return texture(tex, uv); // inside sprite, no outline

    float maxNeighbor = 0.0;
    for (int y = -thickness; y <= thickness; y++) {
        for (int x = -thickness; x <= thickness; x++) {
            if (x == 0 && y == 0) continue;
            float a = texture(tex, uv + float2(x, y) * texelSize).a;
            maxNeighbor = max(maxNeighbor, a);
        }
    }
    return maxNeighbor > 0.5 ? borderColor : float4(0.0);
}

// --- Emboss Effect ---
// 3D relief lighting simulation
float3 emboss(Texture2D tex, float2 uv, float2 texelSize, float strength, float angle) {
    float2 dir = float2(cos(angle), sin(angle));
    
    float3 before = texture(tex, uv - dir * texelSize).rgb;
    float3 after  = texture(tex, uv + dir * texelSize).rgb;
    
    // Difference gives directional gradient
    float3 diff = (after - before) * strength;
    
    // Add to neutral gray
    return float3(0.5) + diff;
}

#endif
