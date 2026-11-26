// hypershader Standard Library - Video Filters

#ifndef _STDLIB_FILTERS_H_
#define _STDLIB_FILTERS_H_

// Inline hash for Film Grain (avoid #include which OBS doesn't support)
float hash13_filters(float3 p3) {
    p3  = frac(p3 * .1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return frac((p3.x + p3.y) * p3.z);
}

// --- Global Filter Settings ---
uniform float fil_vignette_radius;   // @min 0.0 @max 1.5 @default 0.9 @label "Vignette Radius"
uniform float fil_vignette_softness; // @min 0.0 @max 1.0 @default 0.5 @label "Vignette Softness"
uniform float fil_grain_strength;    // @min 0.0 @max 0.2 @default 0.00 @label "Film Grain"

// --- Vignette ---
// darken corners. radius ~ 0.5-1.0, softness ~ 0.2-0.8
float3 applyVignette(float3 color, float2 uv, float radius, float softness) {
    float dist = distance(uv, float2(0.5, 0.5));
    float vig = smoothstep(radius, radius - softness, dist);
    return color * vig;
}

// --- Chromatic Aberration ---
// splits channels. strength ~ 0.005 - 0.02
float3 applyChromaticAberration(Texture2D tex, float2 uv, float strength) {
    float2 redUV = uv - float2(strength, 0.0);
    float2 blueUV = uv + float2(strength, 0.0);
    
    float r = texture(tex, redUV).r;
    float g = texture(tex, uv).g;
    float b = texture(tex, blueUV).b;
    
    return float3(r, g, b);
}

// --- Film Grain ---
// strength ~ 0.03 - 0.1
float3 applyFilmGrain(float3 color, float2 uv, float time, float strength) {
    // Generate high frequency noise
    float n = hash13_filters(float3(uv.xy, time)) * 2.0 - 1.0;
    return color + n * strength;
}

// --- Scanlines ---
// count ~ 50.0 - 200.0, opacity ~ 0.5
float3 applyScanlines(float3 color, float2 uv, float count, float opacity) {
    float s = sin(uv.y * count * 6.28 + hs_time * 10.0);
    float line = (s * 0.5 + 0.5);
    return color * (1.0 - line * opacity);
}

// --- Pixelate ---
// pixels = number of horizontal pixels (e.g. 320.0)
float2 uvPixelate(float2 uv, float pixels) {
    float dx = 1.0 / pixels;
    float dy = dx * (hs_resolution.y / hs_resolution.x); // preserve aspect
    return float2(floor(uv.x / dx) * dx, floor(uv.y / dy) * dy);
}

#endif
