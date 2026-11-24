// HyperShader Standard Library - Volumetric Rendering
// Advanced volumetric effects for clouds, smoke, fog, and atmospheric scattering

#ifndef _STDLIB_VOLUMETRIC_H_
#define _STDLIB_VOLUMETRIC_H_

// === NOISE FUNCTIONS FOR VOLUMETRICS ===

// 3D Value noise
float noise3D(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);

    // Smooth interpolation
    f = f * f * (3.0 - 2.0 * f);

    float n = i.x + i.y * 57.0 + 113.0 * i.z;
    return lerp(
        lerp(
            lerp(hash1D(float2(n + 0.0, 0.0)), hash1D(float2(n + 1.0, 0.0)), f.x),
            lerp(hash1D(float2(n + 57.0, 57.0)), hash1D(float2(n + 58.0, 57.0)), f.x),
            f.y
        ),
        lerp(
            lerp(hash1D(float2(n + 113.0, 113.0)), hash1D(float2(n + 114.0, 113.0)), f.x),
            lerp(hash1D(float2(n + 170.0, 170.0)), hash1D(float2(n + 171.0, 170.0)), f.x),
            f.y
        ),
        f.z
    );
}

// Fractal noise (FBM)
float fbm3D(float3 p, int octaves, float lacunarity, float gain) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise3D(p * frequency);
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return value;
}

// Turbulence noise
float turbulence3D(float3 p, int octaves, float lacunarity, float gain) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * abs(noise3D(p * frequency));
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return value;
}

// === CLOUD/VOLUME DENSITY FUNCTIONS ===

// Basic cloud density
float cloudDensity(float3 p, float time) {
    // Base noise
    float density = fbm3D(p * 0.01 + time * 0.1, 4, 2.0, 0.5);

    // Add some turbulence
    density += turbulence3D(p * 0.05 + time * 0.05, 3, 2.0, 0.3) * 0.3;

    // Shape the cloud (flatten bottom, fade top)
    float height_factor = smoothstep(0.0, 2.0, p.y) * (1.0 - smoothstep(3.0, 5.0, p.y));

    return density * height_factor;
}

// Smoke density (more turbulent)
float smokeDensity(float3 p, float time) {
    float3 wind = float3(0.1, 0.05, 0.0) * time;

    // Base turbulence
    float density = turbulence3D(p * 0.02 + wind, 5, 2.2, 0.4);

    // Add rising motion
    density += fbm3D(p * 0.01 + float3(0, time * 0.1, 0), 3, 2.0, 0.5) * 0.3;

    // Fade with height
    density *= exp(-p.y * 0.1);

    return density;
}

// Fog density (uniform with variation)
float fogDensity(float3 p, float time) {
    // Slow moving fog
    float density = fbm3D(p * 0.005 + time * 0.02, 3, 2.0, 0.6);

    // Add some wisps
    density += fbm3D(p * 0.02 + time * 0.05, 2, 2.5, 0.4) * 0.2;

    return density * 0.3;
}

// Fire density (hot rising plasma)
float fireDensity(float3 p, float time) {
    // Rising motion
    float3 rising = float3(0, time * 0.5, 0);

    // Turbulent flame
    float density = turbulence3D(p * 0.03 + rising, 4, 2.0, 0.5);

    // Flame shape (wider at bottom, narrower at top)
    float shape = 1.0 - smoothstep(-1.0, 2.0, p.y);
    shape *= 1.0 - exp(-length(p.xz) * 0.5);

    return density * shape;
}

// === LIGHTING AND SCATTERING ===

// Beer-Lambert law for light absorption
float beerLambert(float density, float absorption) {
    return exp(-density * absorption);
}

// Henyey-Greenstein phase function (anisotropic scattering)
float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

// Multiple scattering approximation
float3 multipleScattering(float3 lightEnergy, float3 albedo, float density) {
    // Simple approximation: some light gets scattered multiple times
    float3 scattered = lightEnergy * albedo * density;
    return scattered + scattered * albedo * 0.5; // Secondary scattering
}

// === VOLUMETRIC RENDERING ===

// Single scattering volumetric ray march
float3 volumetricRaymarch(float3 rayOrigin, float3 rayDir, float maxDist,
                         float stepSize, int steps, float3 lightDir, float3 lightColor,
                         float absorption, float scattering, float g) {

    float3 accumulatedLight = float3(0.0);
    float transmittance = 1.0;
    float t = 0.0;

    for (int i = 0; i < steps; i++) {
        float3 pos = rayOrigin + rayDir * t;

        // Sample density (implement your density function here)
        float density = cloudDensity(pos, iTime);

        if (density > 0.001) {
            // Light scattering
            float3 lightPos = pos + lightDir * 0.1; // Offset for shadow sampling
            float lightDensity = cloudDensity(lightPos, iTime);
            float lightTransmittance = beerLambert(lightDensity, absorption);

            float3 scatteredLight = lightColor * lightTransmittance * scattering;

            // Phase function
            float cosTheta = dot(rayDir, lightDir);
            float phase = henyeyGreenstein(cosTheta, g);

            // Accumulate light
            float3 stepLight = scatteredLight * phase * density * stepSize;
            accumulatedLight += stepLight * transmittance;

            // Update transmittance
            transmittance *= beerLambert(density, absorption);
        }

        t += stepSize;

        if (t > maxDist || transmittance < 0.001) break;
    }

    return accumulatedLight;
}

// Multiple scattering volumetric ray march
float3 volumetricRaymarchMulti(float3 rayOrigin, float3 rayDir, float maxDist,
                               float stepSize, int steps, float3 lightDir, float3 lightColor,
                               float absorption, float scattering, float g, float3 albedo) {

    float3 accumulatedLight = float3(0.0);
    float transmittance = 1.0;
    float t = 0.0;

    for (int i = 0; i < steps; i++) {
        float3 pos = rayOrigin + rayDir * t;

        float density = cloudDensity(pos, iTime);

        if (density > 0.001) {
            // Multiple scattering
            float3 scatteredLight = multipleScattering(lightColor, albedo, density) * scattering;

            // Phase function
            float cosTheta = dot(rayDir, lightDir);
            float phase = henyeyGreenstein(cosTheta, g);

            // Accumulate
            float3 stepLight = scatteredLight * phase * density * stepSize;
            accumulatedLight += stepLight * transmittance;

            // Update transmittance
            transmittance *= beerLambert(density, absorption);
        }

        t += stepSize;

        if (t > maxDist || transmittance < 0.001) break;
    }

    return accumulatedLight;
}

// === ATMOSPHERIC SCATTERING ===

// Rayleigh scattering for sky
float3 rayleighScattering(float3 rayDir, float3 sunDir, float3 sunColor, float intensity) {
    float cosTheta = dot(rayDir, sunDir);
    float rayleigh = 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
    return sunColor * rayleigh * intensity;
}

// Mie scattering for haze
float3 mieScattering(float3 rayDir, float3 sunDir, float3 sunColor, float g, float intensity) {
    float cosTheta = dot(rayDir, sunDir);
    float mie = henyeyGreenstein(cosTheta, g);
    return sunColor * mie * intensity;
}

// Combined atmospheric scattering
float3 atmosphericScattering(float3 rayDir, float3 sunDir, float3 sunColor,
                           float rayleighIntensity, float mieIntensity, float mieG) {
    float3 rayleigh = rayleighScattering(rayDir, sunDir, sunColor, rayleighIntensity);
    float3 mie = mieScattering(rayDir, sunDir, sunColor, mieG, mieIntensity);
    return rayleigh + mie;
}

// === UTILITY FUNCTIONS ===

// Convert density to alpha (for blending)
float densityToAlpha(float density, float absorption) {
    return 1.0 - beerLambert(density, absorption);
}

// Volumetric shadow sampling
float volumetricShadow(float3 pos, float3 lightDir, float maxDist, float stepSize, int steps) {
    float shadow = 1.0;
    float t = 0.0;

    for (int i = 0; i < steps; i++) {
        float3 samplePos = pos + lightDir * t;
        float density = cloudDensity(samplePos, iTime);
        shadow *= beerLambert(density, 1.0);
        t += stepSize;

        if (t > maxDist || shadow < 0.001) break;
    }

    return shadow;
}

// === PRESETS ===

// Cloud preset
struct VolumetricCloud {
    float density;
    float absorption;
    float scattering;
    float g; // Phase function anisotropy
    float3 albedo;
};

VolumetricCloud cloudPreset() {
    VolumetricCloud cloud;
    cloud.density = 1.0;
    cloud.absorption = 0.1;
    cloud.scattering = 0.8;
    cloud.g = 0.3;
    cloud.albedo = float3(0.9, 0.95, 1.0);
    return cloud;
}

// Smoke preset
VolumetricCloud smokePreset() {
    VolumetricCloud smoke;
    smoke.density = 0.8;
    smoke.absorption = 0.2;
    smoke.scattering = 0.9;
    smoke.g = -0.2;
    smoke.albedo = float3(0.3, 0.3, 0.3);
    return smoke;
}

// Fire preset
VolumetricCloud firePreset() {
    VolumetricCloud fire;
    fire.density = 1.2;
    fire.absorption = 0.05;
    fire.scattering = 1.0;
    fire.g = 0.0;
    fire.albedo = float3(1.0, 0.6, 0.2);
    return fire;
}

#endif