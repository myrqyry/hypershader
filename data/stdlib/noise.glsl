// HyperShader Standard Library - Noise Functions

#ifndef _STDLIB_NOISE_H_
#define _STDLIB_NOISE_H_

// Global Noise Configuration
uniform int noise_type; // @list "Standard,Smooth,Cellular" @label "Noise Algorithm"

// Pseudo-random hash (1D -> 1D)
float hash11(float p) {
    p = frac(p * .1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

// Pseudo-random hash (2D -> 1D)
float hash12(float2 p) {
    float3 p3  = frac(float3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// Pseudo-random hash (3D -> 1D) - needed for filters.glsl
float hash13(float3 p3) {
    p3  = frac(p3 * .1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return frac((p3.x + p3.y) * p3.z);
}


// 2D Noise (Value Noise)
float noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f*f*(3.0-2.0*f);
    return lerp(lerp(hash12(i + float2(0.0,0.0)), 
                     hash12(i + float2(1.0,0.0)), u.x),
                lerp(hash12(i + float2(0.0,1.0)), 
                     hash12(i + float2(1.0,1.0)), u.x), u.y);
}

// Fractal Brownian Motion (FBM)
float fbm(float2 p, int octaves) {
    float v = 0.0;
    float a = 0.5;
    float2 shift = float2(100.0, 100.0);
    // Rotate to reduce axial bias (inline math; float2x2 is unsupported in OBS)
    float c05 = cos(0.5);
    float s05 = sin(0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= octaves) break;
        v += a * noise(p);
        // Manual 2x2 matrix multiplication for OpenGL compatibility
        float2 rotated = float2(p.x * c05 - p.y * s05, p.x * s05 + p.y * c05);
        p = rotated * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

// FBM 3D overload (for shaders that pass vec3)
float fbm(float3 p, int octaves) {
    return fbm(p.xy, octaves); // Sample 2D slice
}

#endif
