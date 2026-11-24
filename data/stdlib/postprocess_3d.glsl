// HyperShader Standard Library - 3D Post-Processing Effects
// Advanced post-processing for 3D rendering: bloom, DOF, motion blur, etc.

#ifndef _STDLIB_POSTPROCESS_3D_H_
#define _STDLIB_POSTPROCESS_3D_H_

// === BLOOM EFFECT ===

// Extract bright areas for bloom. Softness is clamped so a zero-valued
// control still produces a valid knee instead of a divide-by-zero.
float3 extractBrightAreas(float3 color, float threshold, float softness) {
    float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));
    float safeSoftness = max(softness, 0.0001);
    float knee = threshold - safeSoftness;
    float soft = saturate((brightness - knee) / safeSoftness);
    return color * soft;
}

// Karis weighting keeps isolated hot pixels from dominating the wide bloom
// footprint. Alpha-Piscium uses the same mechanism in its real mip-pyramid
// bloom; this helper is the portable single-pass form for HyperShader shaders.
// These helpers sample the native global image: include hypershader_standard.glsl first.
float bloomKarisWeight(float3 color) {
    float luma = dot(max(color, 0.0), float3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

float4 bloomWeightedBrightTap(float2 uv, float threshold,
                              float softness, float spatialWeight) {
    float3 bright = extractBrightAreas(image.Sample(textureSampler, uv).rgb, threshold, softness);
    float weight = spatialWeight * bloomKarisWeight(bright);
    return float4(bright * weight, weight);
}

// Uses HyperShader's native global image/textureSampler for OBS compatibility.
// Texture arguments cannot be sampled reliably from user-defined functions
// across the supported OBS graphics backends.
// 13-tap wide gather derived from the downsample footprint used by modern
// bloom pyramids. Radius is expressed in source texels.
float3 bloomGather13(float2 uv, float2 resolution, float radius,
                     float threshold, float softness) {
    float2 px = (max(radius, 0.0) / max(resolution, float2(1.0, 1.0)));
    float4 sum = float4(0.0);

    sum += bloomWeightedBrightTap(uv, threshold, softness, 0.125);

    sum += bloomWeightedBrightTap(uv + float2(-2.0,  0.0) * px, threshold, softness, 0.0625);
    sum += bloomWeightedBrightTap(uv + float2( 2.0,  0.0) * px, threshold, softness, 0.0625);
    sum += bloomWeightedBrightTap(uv + float2( 0.0, -2.0) * px, threshold, softness, 0.0625);
    sum += bloomWeightedBrightTap(uv + float2( 0.0,  2.0) * px, threshold, softness, 0.0625);

    sum += bloomWeightedBrightTap(uv + float2(-2.0, -2.0) * px, threshold, softness, 0.03125);
    sum += bloomWeightedBrightTap(uv + float2( 2.0, -2.0) * px, threshold, softness, 0.03125);
    sum += bloomWeightedBrightTap(uv + float2(-2.0,  2.0) * px, threshold, softness, 0.03125);
    sum += bloomWeightedBrightTap(uv + float2( 2.0,  2.0) * px, threshold, softness, 0.03125);

    sum += bloomWeightedBrightTap(uv + float2(-1.0, -1.0) * px, threshold, softness, 0.125);
    sum += bloomWeightedBrightTap(uv + float2( 1.0, -1.0) * px, threshold, softness, 0.125);
    sum += bloomWeightedBrightTap(uv + float2(-1.0,  1.0) * px, threshold, softness, 0.125);
    sum += bloomWeightedBrightTap(uv + float2( 1.0,  1.0) * px, threshold, softness, 0.125);

    return sum.rgb / max(sum.a, 0.0001);
}

// Single-pass multi-scale bloom approximation for the stdlib. A real
// downsample/upsample pyramid needs render-target orchestration from the host;
// emuLensFX implements that path natively. This version keeps the same useful
// wide-footprint + Karis-stabilized behavior inside one shader invocation.
float3 applyBloom(float2 uv, float2 resolution, float intensity,
                  float threshold, float softness) {
    float3 scene = image.Sample(textureSampler, uv).rgb;
    if (intensity <= 0.0) return scene;
    float3 nearBloom = bloomGather13(uv, resolution, 1.0, threshold, softness);
    float3 midBloom = bloomGather13(uv, resolution, 2.0, threshold, softness);
    float3 farBloom = bloomGather13(uv, resolution, 4.0, threshold, softness);
    float3 bloom = nearBloom * 0.50 + midBloom * 0.30 + farBloom * 0.20;
    return scene + bloom * max(intensity, 0.0);
}

// === DEPTH OF FIELD ===

// Circle of confusion calculation
float calculateCoC(float depth, float focusDistance, float focusRange, float maxCoC) {
    float coc = abs(depth - focusDistance) / depth;
    coc = smoothstep(focusRange, focusRange * 2.0, coc);
    return clamp(coc * maxCoC, 0.0, maxCoC);
}

// Bokeh shape function (hexagon)
float bokehHexagon(float2 uv, float size) {
    uv = abs(uv);
    float2 q = uv - float2(0.5, 0.0) * size;
    return max(q.x * 0.57735 + q.y * 0.5, q.y) - size * 0.5;
}

// Bokeh shape function (circle)
float bokehCircle(float2 uv, float size) {
    return length(uv) - size * 0.5;
}

// Depth of field with bokeh
float3 applyDepthOfField(sampler2D colorTex, sampler2D depthTex, float2 uv, float2 resolution,
                        float focusDistance, float focusRange, float maxCoC,
                        float bokehShape, int samples) {

    float centerDepth = texture(depthTex, uv).r;
    float centerCoC = calculateCoC(centerDepth, focusDistance, focusRange, maxCoC);

    if (centerCoC < 0.001) {
        return texture(colorTex, uv).rgb;
    }

    float3 result = float3(0.0);
    float totalWeight = 0.0;

    float2 texelSize = 1.0 / resolution;

    for (int i = 0; i < samples; i++) {
        // Golden angle spiral for sample distribution
        float angle = float(i) * 2.39996; // Golden angle in radians
        float radius = sqrt(float(i) / float(samples - 1)) * centerCoC;

        float2 offset = float2(cos(angle), sin(angle)) * radius;
        float2 sampleUV = uv + offset * texelSize;

        // Sample color and depth
        float3 sampleColor = texture(colorTex, sampleUV).rgb;
        float sampleDepth = texture(depthTex, sampleUV).r;
        float sampleCoC = calculateCoC(sampleDepth, focusDistance, focusRange, maxCoC);

        // Bokeh weight based on shape
        float bokehWeight = 1.0;
        if (bokehShape > 0.5) {
            bokehWeight = 1.0 - smoothstep(0.0, 1.0, bokehHexagon(offset / centerCoC, 1.0));
        } else {
            bokehWeight = 1.0 - smoothstep(0.0, 1.0, bokehCircle(offset / centerCoC, 1.0));
        }

        // Weight by CoC and bokeh shape
        float weight = sampleCoC * bokehWeight;
        result += sampleColor * weight;
        totalWeight += weight;
    }

    return totalWeight > 0.0 ? result / totalWeight : texture(colorTex, uv).rgb;
}

// === MOTION BLUR ===

// Velocity buffer motion blur
float3 applyMotionBlur(sampler2D colorTex, sampler2D velocityTex, float2 uv, float2 resolution,
                      float intensity, int samples) {

    float2 velocity = texture(velocityTex, uv).rg;
    velocity *= intensity;

    if (length(velocity) < 0.001) {
        return texture(colorTex, uv).rgb;
    }

    float3 result = texture(colorTex, uv).rgb;
    float2 texelSize = 1.0 / resolution;

    for (int i = 1; i < samples; i++) {
        float t = float(i) / float(samples - 1);
        float2 offset = velocity * t * texelSize;
        result += texture(colorTex, uv - offset).rgb;
    }

    return result / float(samples);
}

// Camera motion blur (simulated)
float3 applyCameraMotionBlur(sampler2D colorTex, float2 uv, float2 resolution,
                           float3 cameraVelocity, float intensity, int samples) {

    float2 velocity = cameraVelocity.xy * intensity;
    if (length(velocity) < 0.001) {
        return texture(colorTex, uv).rgb;
    }

    float3 result = texture(colorTex, uv).rgb;
    float2 texelSize = 1.0 / resolution;

    for (int i = 1; i < samples; i++) {
        float t = float(i) / float(samples - 1);
        float2 offset = velocity * t * texelSize;
        result += texture(colorTex, uv - offset).rgb;
    }

    return result / float(samples);
}

// === CHROMATIC ABERRATION ===

// Radial chromatic aberration
float3 applyChromaticAberration(sampler2D tex, float2 uv, float2 resolution, float intensity) {
    float2 center = float2(0.5, 0.5);
    float2 dir = uv - center;
    float dist = length(dir);

    // Sample different channels with different offsets
    float2 offset = dir * intensity * dist * dist;

    float r = texture(tex, uv - offset * 0.8).r;
    float g = texture(tex, uv - offset * 0.4).g;
    float b = texture(tex, uv + offset * 0.4).b;

    return float3(r, g, b);
}

// Spectral chromatic aberration (more realistic)
float3 applySpectralChromaticAberration(sampler2D tex, float2 uv, float2 resolution, float intensity) {
    float2 center = float2(0.5, 0.5);
    float2 dir = uv - center;
    float dist = length(dir);

    // Different wavelengths have different refractive indices
    float2 offsetR = dir * intensity * dist * dist * 1.0;
    float2 offsetG = dir * intensity * dist * dist * 0.98;
    float2 offsetB = dir * intensity * dist * dist * 0.96;

    float r = texture(tex, uv - offsetR).r;
    float g = texture(tex, uv - offsetG).g;
    float b = texture(tex, uv + offsetB).b;

    return float3(r, g, b);
}

// === VIGNETTE ===

// Simple vignette
float3 applyVignette(float3 color, float2 uv, float intensity, float smoothness) {
    float2 center = float2(0.5, 0.5);
    float dist = length(uv - center);
    float vignette = 1.0 - smoothstep(0.5 - smoothness, 0.5 + smoothness, dist);
    return color * lerp(1.0, vignette, intensity);
}

// Photographic vignette (elliptical)
float3 applyPhotographicVignette(float3 color, float2 uv, float intensity, float smoothness, float aspect) {
    float2 center = float2(0.5, 0.5);
    float2 dir = (uv - center) * float2(aspect, 1.0);
    float dist = length(dir);
    float vignette = 1.0 - smoothstep(0.5 - smoothness, 0.5 + smoothness, dist);
    return color * lerp(1.0, vignette, intensity);
}

// === FILM GRAIN ===

// Film grain effect
float3 applyFilmGrain(float3 color, float2 uv, float intensity, float time) {
    float2 grainUV = uv * float2(1920.0, 1080.0); // Screen resolution
    float grain = hash12(grainUV + time * 100.0) * 2.0 - 1.0;
    grain *= intensity;

    // Apply grain in linear space
    return color + grain;
}

// Colored film grain
float3 applyColoredFilmGrain(float3 color, float2 uv, float intensity, float time) {
    float2 grainUV = uv * float2(1920.0, 1080.0);
    float3 grain = float3(
        hash12(grainUV + time * 100.0),
        hash12(grainUV + time * 100.0 + 1.0),
        hash12(grainUV + time * 100.0 + 2.0)
    ) * 2.0 - 1.0;

    grain *= intensity;
    return color + grain;
}

// === COLOR GRADING ===

// Simple color grading (lift, gamma, gain)
float3 applyColorGrading(float3 color, float3 lift, float3 gamma, float3 gain) {
    color = pow(color, 1.0 / gamma);
    color *= gain;
    color += lift;
    return color;
}

// Contrast adjustment
float3 applyContrast(float3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

// Saturation adjustment
float3 applySaturation(float3 color, float saturation) {
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    return lerp(float3(luminance), color, saturation);
}

// === LENS FLARE ===

// Simple lens flare
float3 applyLensFlare(float3 color, float2 uv, float2 lightPos, float intensity) {
    float2 dir = uv - lightPos;
    float dist = length(dir);

    // Ghost flares
    float3 flare = float3(0.0);
    for (int i = 1; i <= 5; i++) {
        float scale = 1.0 / float(i);
        float2 ghostPos = lightPos + dir * scale;
        if (all(ghostPos >= 0.0) && all(ghostPos <= 1.0)) {
            float ghost = texture(iChannel0, ghostPos).r; // Use scene texture for ghost shape
            flare += float3(0.8, 0.6, 1.0) * ghost * intensity / float(i);
        }
    }

    // Central glow
    float glow = 1.0 / (1.0 + dist * 10.0);
    flare += float3(1.0, 0.9, 0.8) * glow * intensity;

    return color + flare;
}

// === SCREEN SPACE AMBIENT OCCLUSION (SSAO) ===

// Simple SSAO
float calculateSSAO(sampler2D depthTex, float2 uv, float2 resolution, float radius, int samples) {
    float depth = texture(depthTex, uv).r;
    float occlusion = 0.0;

    float2 texelSize = 1.0 / resolution;

    for (int i = 0; i < samples; i++) {
        float angle = (float(i) / float(samples)) * 2.0 * PI;
        float2 offset = float2(cos(angle), sin(angle)) * radius * texelSize;

        float sampleDepth = texture(depthTex, uv + offset).r;
        float diff = depth - sampleDepth;

        if (diff > 0.0) {
            occlusion += 1.0 / (1.0 + diff * 100.0);
        }
    }

    return 1.0 - occlusion / float(samples);
}

// === TONEMAPPING ===

// Reinhard tonemapping
float3 reinhardTonemap(float3 color, float exposure) {
    color *= exposure;
    return color / (1.0 + color);
}

// ACES filmic tonemapping (approximation)
float3 acesTonemap(float3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    color = (color * (a * color + b)) / (color * (c * color + d) + e);
    return clamp(color, 0.0, 1.0);
}

// Uncharted 2 tonemapping
float3 uncharted2Tonemap(float3 color, float exposure) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;

    color *= exposure;
    color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
    return color;
}

// === COMBINED POST-PROCESSING PIPELINE ===

// Full post-processing pipeline
float3 applyPostProcessing(float3 color, sampler2D colorTex, sampler2D depthTex,
                          sampler2D velocityTex, float2 uv, float2 resolution,
                          // Bloom parameters
                          float bloomIntensity, float bloomThreshold, float bloomSoftness,
                          // DOF parameters
                          float focusDistance, float focusRange, float maxCoC, float bokehShape,
                          // Motion blur parameters
                          float motionBlurIntensity,
                          // Chromatic aberration parameters
                          float chromaticAberrationIntensity,
                          // Other parameters
                          float vignetteIntensity, float filmGrainIntensity,
                          float contrast, float saturation, float exposure) {

    // Start with original color
    float3 result = color;

    // Apply bloom
    if (bloomIntensity > 0.0) {
        result = applyBloom(uv, resolution, bloomIntensity, bloomThreshold, bloomSoftness);
    }

    // Apply depth of field
    if (maxCoC > 0.0) {
        result = applyDepthOfField(colorTex, depthTex, uv, resolution,
                                 focusDistance, focusRange, maxCoC, bokehShape, 16);
    }

    // Apply motion blur
    if (motionBlurIntensity > 0.0) {
        result = applyMotionBlur(colorTex, velocityTex, uv, resolution, motionBlurIntensity, 8);
    }

    // Apply chromatic aberration
    if (chromaticAberrationIntensity > 0.0) {
        result = applyChromaticAberration(colorTex, uv, resolution, chromaticAberrationIntensity);
    }

    // Apply vignette
    if (vignetteIntensity > 0.0) {
        result = applyVignette(result, uv, vignetteIntensity, 0.3);
    }

    // Apply film grain
    if (filmGrainIntensity > 0.0) {
        result = applyFilmGrain(result, uv, filmGrainIntensity, iTime);
    }

    // Color grading
    result = applyContrast(result, contrast);
    result = applySaturation(result, saturation);

    // Tonemapping
    result = acesTonemap(result * exposure);

    return result;
}

#endif