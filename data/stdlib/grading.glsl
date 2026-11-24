// HyperShader Standard Library - Color Grading
// Professional color correction and grading tools

#ifndef _STDLIB_GRADING_H_
#define _STDLIB_GRADING_H_

// --- Lift/Gamma/Gain (LGG) Color Correction ---
// Industry standard color grading model

// Lift: affects shadows (adds to dark values)
float3 applyLift(float3 color, float3 lift) {
    return color + lift * (1.0 - color);
}

// Gamma: affects midtones (power curve)
float3 applyGamma(float3 color, float3 gamma) {
    return pow(abs(color), gamma) * sign(color);
}

// Gain: affects highlights (multiplier)
float3 applyGain(float3 color, float3 gain) {
    return color * gain;
}

// Offset: uniform shift
float3 applyOffset(float3 color, float3 offset) {
    return color + offset;
}

// Full LGG pipeline
float3 applyLGG(float3 color, float3 lift, float3 gamma, float3 gain) {
    color = applyLift(color, lift);
    color = applyGamma(color, gamma);
    color = applyGain(color, gain);
    return color;
}

// --- Slope/Offset/Power (SOP) - ASC CDL ---
// American Society of Cinematographers Color Decision List

float3 applyASC_CDL(float3 color, float3 slope, float3 offset, float3 power) {
    color = color * slope + offset;
    color = pow(max(color, 0.0), power);
    return color;
}

// --- Color Temperature/Tint ---

// Kelvin to RGB approximation (simplified)
float3 kelvinToRGB(float kelvin) {
    float temp = kelvin / 100.0;
    float3 rgb;
    
    // Red
    if (temp <= 66.0) {
        rgb.r = 1.0;
    } else {
        rgb.r = 1.292936 * pow(temp - 60.0, -0.1332047592);
    }
    
    // Green
    if (temp <= 66.0) {
        rgb.g = 0.390082 * log(temp) - 0.631841;
    } else {
        rgb.g = 1.129891 * pow(temp - 60.0, -0.0755148492);
    }
    
    // Blue
    if (temp >= 66.0) {
        rgb.b = 1.0;
    } else if (temp <= 19.0) {
        rgb.b = 0.0;
    } else {
        rgb.b = 0.543207 * log(temp - 10.0) - 1.196254;
    }
    
    return saturate(rgb);
}

// Apply white balance adjustment
// temperature: -1 to 1 (negative=cooler/blue, positive=warmer/yellow)
// tint: -1 to 1 (negative=green, positive=magenta)
float3 applyWhiteBalance(float3 color, float temperature, float tint) {
    // Simple approximation using color shifts
    float3 warm = float3(1.0, 0.9, 0.8);
    float3 cool = float3(0.8, 0.9, 1.0);
    float3 tempMult = lerp(cool, warm, temperature * 0.5 + 0.5);
    
    float3 green = float3(0.9, 1.0, 0.9);
    float3 magenta = float3(1.0, 0.9, 1.0);
    float3 tintMult = lerp(green, magenta, tint * 0.5 + 0.5);
    
    return color * tempMult * tintMult;
}

// --- Tint by Luminance Zone ---
// Apply different tints to shadows, midtones, highlights

float3 applyTriToneGrade(float3 color, float3 shadowTint, float3 midTint, float3 highTint) {
    float luma = dot(color, float3(0.299, 0.587, 0.114));
    
    // Weight functions for each zone
    float shadowWeight = 1.0 - smoothstep(0.0, 0.5, luma);
    float highWeight = smoothstep(0.5, 1.0, luma);
    float midWeight = 1.0 - shadowWeight - highWeight;
    
    float3 tint = shadowTint * shadowWeight + midTint * midWeight + highTint * highWeight;
    
    return color * tint;
}

// --- Saturation Controls ---

// Global saturation adjustment
float3 adjustSaturation(float3 color, float saturation) {
    float luma = dot(color, float3(0.299, 0.587, 0.114));
    return lerp(float3(luma), color, saturation);
}

// Vibrance (protects skin tones, affects low-saturation colors more)
float3 adjustVibrance(float3 color, float vibrance) {
    float luma = dot(color, float3(0.299, 0.587, 0.114));
    float maxC = max(color.r, max(color.g, color.b));
    float minC = min(color.r, min(color.g, color.b));
    float sat = maxC - minC;
    
    // Less saturated colors get more boost
    float amount = vibrance * (1.0 - sat);
    
    return lerp(float3(luma), color, 1.0 + amount);
}

// --- Contrast ---

// Simple contrast (pivot at 0.5)
float3 adjustContrast(float3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

// S-curve contrast (smoother, no clipping)
float3 adjustContrastSCurve(float3 color, float contrast) {
    // Attempt sigmoid-like curve
    float3 x = color * 2.0 - 1.0; // -1 to 1
    float3 curved = x * (abs(x) * contrast + (1.0 - contrast));
    return curved * 0.5 + 0.5;
}

// --- Exposure ---

float3 adjustExposure(float3 color, float exposure) {
    return color * pow(2.0, exposure);
}

// --- Levels ---

// Remap input range to output range (like Photoshop Levels)
float3 adjustLevels(float3 color, float inputBlack, float inputWhite, float outputBlack, float outputWhite, float gamma) {
    // Clamp to input range
    color = saturate((color - inputBlack) / (inputWhite - inputBlack));
    // Apply gamma
    color = pow(color, float3(gamma));
    // Map to output range
    color = color * (outputWhite - outputBlack) + outputBlack;
    return color;
}

// --- Curves (simplified) ---

// Apply simple curve using smoothstep approximation
float3 applyCurve(float3 color, float shadows, float midtones, float highlights) {
    // shadows < 1 = lift shadows, > 1 = crush
    // midtones != 1 = gamma
    // highlights < 1 = compress, > 1 = boost
    
    float3 result = color;
    result = pow(result, float3(1.0 / shadows));
    result = pow(result, float3(midtones));
    result = pow(result, float3(highlights));
    
    return saturate(result);
}

// --- Color Matrix ---

// Apply 3x3 color matrix (for creative color effects)
float3 applyColorMatrix(float3 color, float3x3 matrix) {
    return mul(matrix, color);
}

// Common preset matrices
float3x3 matrixSepia() {
    return float3x3(
        0.393, 0.769, 0.189,
        0.349, 0.686, 0.168,
        0.272, 0.534, 0.131
    );
}

float3x3 matrixDesaturate() {
    return float3x3(
        0.299, 0.587, 0.114,
        0.299, 0.587, 0.114,
        0.299, 0.587, 0.114
    );
}

float3x3 matrixNegative() {
    return float3x3(
        -1.0, 0.0, 0.0,
        0.0, -1.0, 0.0,
        0.0, 0.0, -1.0
    );
}

// --- Channel Mixer ---

float3 applyChannelMix(float3 color, float3 redOut, float3 greenOut, float3 blueOut) {
    return float3(
        dot(color, redOut),
        dot(color, greenOut),
        dot(color, blueOut)
    );
}

// --- Split Toning ---

// Apply different colors to shadows and highlights
float3 applySplitTone(float3 color, float3 shadowColor, float3 highlightColor, float balance) {
    float luma = dot(color, float3(0.299, 0.587, 0.114));
    
    // Balance shifts the crossover point
    float shadowMask = 1.0 - smoothstep(balance - 0.2, balance + 0.2, luma);
    float highlightMask = smoothstep(balance - 0.2, balance + 0.2, luma);
    
    float3 tinted = color;
    tinted = lerp(tinted, tinted * shadowColor, shadowMask * 0.5);
    tinted = lerp(tinted, tinted * highlightColor, highlightMask * 0.5);
    
    return tinted;
}

#endif
