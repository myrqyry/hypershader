// HyperShader Standard Library - Color Utilities

#ifndef _STDLIB_COLORS_H_
#define _STDLIB_COLORS_H_

// --- Color Space Conversions ---

// Global Color Settings
uniform float global_exposure; // @min 0.0 @max 10.0 @default 1.0 @label "Color Exposure"
uniform float global_gamma;    // @min 0.1 @max 5.0 @default 2.2 @label "Color Gamma"

// === RGB ↔ HSV ===
float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = (c.g < c.b) ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
    float4 q = (c.r < p.x) ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// === RGB ↔ HSL ===
float3 rgb2hsl(float3 rgb) {
    float h = 0.0;
    float s = 0.0;
    float l = 0.0;
    float cMin = min(rgb.r, min(rgb.g, rgb.b));
    float cMax = max(rgb.r, max(rgb.g, rgb.b));
    
    l = (cMax + cMin) * 0.5;
    
    if (cMax > cMin) {
        float cDelta = cMax - cMin;
        s = (l < 0.5) ? cDelta / (cMax + cMin) : cDelta / (2.0 - cMax - cMin);
        
        if (rgb.r == cMax) {
            h = (rgb.g - rgb.b) / cDelta;
        } else if (rgb.g == cMax) {
            h = 2.0 + (rgb.b - rgb.r) / cDelta;
        } else {
            h = 4.0 + (rgb.r - rgb.g) / cDelta;
        }
        
        h = h < 0.0 ? h + 6.0 : h;
        h = h / 6.0;
    }
    return float3(h, s, l);
}

float3 hsl2rgb(float3 hsl) {
    float3 rgb = clamp(
        abs(fmod(hsl.x * 6.0 + float3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0,
        0.0, 1.0
    );
    return hsl.z + hsl.y * (rgb - 0.5) * (1.0 - abs(2.0 * hsl.z - 1.0));
}

// === RGB ↔ YUV (BT.709) ===
// Useful for video processing and luma-based effects
float3 rgb2yuv(float3 rgb) {
    float y = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    float u = dot(rgb, float3(-0.1146, -0.3854, 0.5)) + 0.5;
    float v = dot(rgb, float3(0.5, -0.4542, -0.0458)) + 0.5;
    return float3(y, u, v);
}

float3 yuv2rgb(float3 yuv) {
    yuv.yz -= 0.5;
    float r = yuv.x + 1.5748 * yuv.z;
    float g = yuv.x - 0.1873 * yuv.y - 0.4681 * yuv.z;
    float b = yuv.x + 1.8556 * yuv.y;
    return float3(r, g, b);
}

// === RGB ↔ LAB (simplified) ===
// Attempt perceptually uniform color space
float3 rgb2lab(float3 rgb) {
    // Convert to XYZ first (simplified D65 illuminant)
    float3 xyz;
    rgb = pow(max(rgb, 0.0), float3(2.2)); // Linearize
    xyz.x = dot(rgb, float3(0.4124, 0.3576, 0.1805));
    xyz.y = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    xyz.z = dot(rgb, float3(0.0193, 0.1192, 0.9505));
    
    // XYZ to LAB
    xyz = xyz / float3(0.9505, 1.0, 1.089); // D65 reference
    xyz = (xyz > 0.008856) ? pow(xyz, float3(1.0/3.0)) : (7.787 * xyz) + 16.0/116.0;
    
    float l = (116.0 * xyz.y) - 16.0;
    float a = 500.0 * (xyz.x - xyz.y);
    float b = 200.0 * (xyz.y - xyz.z);
    
    return float3(l / 100.0, a / 128.0 + 0.5, b / 128.0 + 0.5);
}

// --- Color Utilities ---
float3 invert(float3 color) { return 1.0 - color; }

// Grayscale using standard luminance weights
float3 grayscale(float3 color) {
    float lum = dot(color, float3(0.299, 0.587, 0.114));
    return float3(lum);
}

// Posterize to N levels
float3 posterize(float3 color, float levels) {
    return floor(color * levels) / (levels - 1.0);
}

// Threshold (black/white based on luminance)
float3 threshold(float3 color, float thresh) {
    float lum = dot(color, float3(0.299, 0.587, 0.114));
    return lum > thresh ? float3(1.0) : float3(0.0);
}

// Sepia tone
float3 sepia(float3 color) {
    float3 result;
    result.r = dot(color, float3(0.393, 0.769, 0.189));
    result.g = dot(color, float3(0.349, 0.686, 0.168));
    result.b = dot(color, float3(0.272, 0.534, 0.131));
    return result;
}

// --- Blend Modes (Standard Photoshop logic) ---
// Note: 'base' is the bottom layer, 'blend' is the top layer.

float3 blendScreen(float3 base, float3 blend) {
    return 1.0 - (1.0 - base) * (1.0 - blend);
}

float3 blendMultiply(float3 base, float3 blend) {
    return base * blend;
}

float3 blendOverlay(float3 base, float3 blend) {
    return (base.r < 0.5 && base.g < 0.5 && base.b < 0.5) ? 
           (2.0 * base * blend) : 
           (1.0 - 2.0 * (1.0 - base) * (1.0 - blend));
}

float3 blendSoftLight(float3 base, float3 blend) {
    return (1.0 - 2.0 * blend) * base * base + 2.0 * base * blend;
}

float3 blendHardLight(float3 base, float3 blend) {
    return blendOverlay(blend, base); // Hard Light is Overlay with swapped args
}

float3 blendColorDodge(float3 base, float3 blend) {
    return base / max(1.0 - blend, 0.001);
}

float3 blendColorBurn(float3 base, float3 blend) {
    return 1.0 - (1.0 - base) / max(blend, 0.001);
}

float3 blendLinearDodge(float3 base, float3 blend) {
    return base + blend;
}

float3 blendLinearBurn(float3 base, float3 blend) {
    return base + blend - 1.0;
}

float3 blendDifference(float3 base, float3 blend) {
    return abs(base - blend);
}

float3 blendExclusion(float3 base, float3 blend) {
    return base + blend - 2.0 * base * blend;
}

// Blend with opacity
float3 blendWithOpacity(float3 base, float3 blend, float opacity) {
    return lerp(base, blend, opacity);
}

// --- Palettes ---
// IQ Cosine Palette: http://www.iquilezles.org/www/articles/palettes/palettes.htm
float3 palette(in float t, in float3 a, in float3 b, in float3 c, in float3 d) {
    return a + b * cos(6.28318 * (c * t + d));
}

// Rainbow palette
float3 rainbow(float t) {
    return 0.5 + 0.5 * cos(TAU * (t + float3(0.0, 0.33, 0.67)));
}

// Heat map (blue -> cyan -> green -> yellow -> red)
float3 heatmap(float t) {
    t = saturate(t);
    float3 c;
    if (t < 0.25) {
        c = lerp(float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 1.0), t * 4.0);
    } else if (t < 0.5) {
        c = lerp(float3(0.0, 1.0, 1.0), float3(0.0, 1.0, 0.0), (t - 0.25) * 4.0);
    } else if (t < 0.75) {
        c = lerp(float3(0.0, 1.0, 0.0), float3(1.0, 1.0, 0.0), (t - 0.5) * 4.0);
    } else {
        c = lerp(float3(1.0, 1.0, 0.0), float3(1.0, 0.0, 0.0), (t - 0.75) * 4.0);
    }
    return c;
}

// --- Tonemapping & Gamma ---

float3 tonemapACES(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float3 tonemapReinhard(float3 x) {
    return x / (x + 1.0);
}

float3 tonemapFilmic(float3 x) {
    float3 X = max(float3(0.0), x - 0.004);
    return (X * (6.2 * X + 0.5)) / (X * (6.2 * X + 1.7) + 0.06);
}

float3 toGamma(float3 color) { return pow(color, float3(1.0/global_gamma)); }
float3 toLinear(float3 color) { return pow(color, float3(global_gamma)); }

// Standard Post-Processing Pipeline: Exposure -> Tonemap -> Gamma
float3 postProcess(float3 col) {
    col *= global_exposure;
    col = tonemapACES(col);
    col = toGamma(col);
    return col;
}

#endif
