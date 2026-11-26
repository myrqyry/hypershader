// hypershader Standard Library - Materials & Surface Effects
// Advanced material rendering techniques

#ifndef _STDLIB_MATERIALS_H_
#define _STDLIB_MATERIALS_H_

// === DATA STRUCTURES ===

struct Material {
    float3 albedo;
    float metallic;
    float roughness;
    float3 emissive;
};

// === FRESNEL EFFECTS ===

// Basic Fresnel (Schlick approximation)
float fresnel(float3 viewDir, float3 normal, float power) {
    return pow(1.0 - saturate(dot(viewDir, normal)), power);
}

// Fresnel with bias and scale control
float fresnelFull(float3 viewDir, float3 normal, float bias, float scale, float power) {
    float f = 1.0 - saturate(dot(viewDir, normal));
    return bias + scale * pow(f, power);
}

// === SUBSURFACE SCATTERING (SSS) ===

// Simple SSS approximation
// thickness: 0-1 (from thickness map or constant)
// distortion: how much light wraps around surface
// power: falloff sharpness
float3 subsurfaceScattering(float3 lightDir, float3 viewDir, float3 normal, 
                            float3 lightColor, float3 sssColor, 
                            float thickness, float distortion, float power, float strength) {
    float3 scatterDir = normalize(lightDir + normal * distortion);
    float scatterDot = saturate(dot(viewDir, -scatterDir));
    float scatterFalloff = pow(scatterDot, power);
    return lightColor * sssColor * scatterFalloff * strength * thickness;
}

// === CLEARCOAT ===

// GGX Distribution for clearcoat
float GTR2(float NdotH, float roughness) {
    float a2 = roughness * roughness;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith GGX geometry term
float smithG_GGX(float NdotV, float roughness) {
    float a = roughness * roughness;
    float b = NdotV * NdotV;
    return 2.0 * NdotV / (NdotV + sqrt(a + b - a * b));
}

// Fresnel term
float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Full clearcoat calculation
float3 clearcoat(float3 normal, float3 viewDir, float3 lightDir, float3 lightColor,
                 float roughness, float strength, float3 tint) {
    float3 halfDir = normalize(viewDir + lightDir);
    float NdotH = saturate(dot(normal, halfDir));
    float NdotL = saturate(dot(normal, lightDir));
    float NdotV = saturate(dot(normal, viewDir));
    float LdotH = saturate(dot(lightDir, halfDir));
    
    float3 F0 = float3(0.04); // Dielectric clearcoat
    float3 F = fresnelSchlick(LdotH, F0);
    float D = GTR2(NdotH, max(roughness * roughness, 0.002));
    float G = smithG_GGX(NdotL, roughness) * smithG_GGX(NdotV, roughness);
    
    return F * D * G * lightColor * tint * strength;
}

// === RIMLIGHT ===

// Basic rim lighting
float3 rimlight(float3 normal, float3 viewDir, float3 color, float width, float intensity) {
    float rim = 1.0 - saturate(dot(normal, viewDir));
    rim = pow(rim, width);
    return rim * color * intensity;
}

// Light-based rim (only shows on lit side)
float3 rimlightLitSide(float3 normal, float3 viewDir, float3 lightDir, 
                        float3 color, float width, float intensity) {
    float rim = 1.0 - saturate(dot(normal, viewDir));
    rim = pow(rim, width);
    float lightMask = saturate(dot(normal, lightDir));
    return rim * lightMask * color * intensity;
}

// === IRIDESCENCE ===

// Sinebow color generation (smooth rainbow)
float3 sinebow(float t) {
    return 0.5 + 0.5 * cos(TAU * (t + float3(0.0, 0.33, 0.67)));
}

// Iridescence effect (like oil slick, soap bubbles)
float3 iridescence(float3 normal, float3 viewDir, float power, float frequency) {
    float f = 1.0 - saturate(dot(normal, viewDir));
    float interference = pow(f, power);
    return sinebow(interference * frequency);
}

// Iridescence with parallax shift (more realistic)
float3 iridescenceParallax(float3 normal, float3 viewDir, float parallax, float power, float frequency) {
    float3 shifted = normalize(normal + viewDir * parallax);
    float baseF = 1.0 - saturate(dot(normal, viewDir));
    float shiftedF = 1.0 - saturate(dot(shifted, viewDir));
    float interference = abs(shiftedF - baseF);
    return sinebow(pow(interference, power) * frequency);
}

// === MATCAP ===

// Calculate matcap UV from view-space normal
float2 matcapUV(float3 viewNormal) {
    return viewNormal.xy * 0.5 + 0.5;
}

// === PARALLAX MAPPING ===

// Simple offset parallax
float2 parallaxOffset(float2 uv, float3 viewDir, float3 tangent, float3 bitangent, 
                      float height, float strength) {
    float3 viewDirTS = float3(dot(viewDir, tangent), dot(viewDir, bitangent), 0.0);
    return uv - viewDirTS.xy * (height * strength);
}

// Steep parallax occlusion mapping (iterative)
float2 parallaxOcclusionMapping(sampler2D heightMap, float2 uv, 
                                 float3 viewDir, float3 tangent, float3 bitangent, float3 normal,
                                 float strength, int steps) {
    float3 viewDirTS = float3(
        dot(viewDir, tangent), 
        dot(viewDir, bitangent), 
        dot(viewDir, normal)
    );
    
    float stepSize = 1.0 / float(steps);
    float2 stepOffset = -viewDirTS.xy * strength * stepSize;
    
    float currentHeight = 1.0;
    float2 currentOffset = float2(0.0);
    
    for (int i = 0; i < steps; i++) {
        currentHeight -= stepSize;
        currentOffset += stepOffset;
        float surfaceHeight = texture(heightMap, uv + currentOffset).r;
        
        if (surfaceHeight > currentHeight) {
            // Interpolate for smoother result
            float2 prevOffset = currentOffset - stepOffset;
            float prevHeight = currentHeight + stepSize;
            float prevSurface = texture(heightMap, uv + prevOffset).r;
            
            float t = (prevHeight - prevSurface) / (stepSize + surfaceHeight - prevSurface);
            return uv + lerp(prevOffset, currentOffset, t);
        }
    }
    
    return uv + currentOffset;
}

// Interior mapping (virtual room simulation inside a flat surface)
float3 interiorMapping(samplerCube cubemap, float2 uv, float3 viewDir, 
                        float3 tangent, float3 bitangent, float3 normal,
                        float tiling, float depth) {
    float3 viewDirTS = float3(
        dot(viewDir, tangent),
        dot(viewDir, bitangent),
        dot(viewDir, normal)
    );
    
    float2 roomUV = frac(uv * tiling) * 2.0 - 1.0;
    float3 rayPos = float3(roomUV, 1.0);
    float3 rayDir = -viewDirTS;
    rayDir.z /= max(depth, 0.001);
    
    // Box intersection
    float3 invDir = 1.0 / rayDir;
    float3 k = abs(invDir) - rayPos * invDir;
    float kMin = min(min(k.x, k.y), k.z);
    float3 hitPos = rayPos + kMin * rayDir;
    
    return texture(cubemap, hitPos).rgb;
}

// === DISSOLVE ===

// Edge glow dissolve
float4 dissolveGlow(float dissolveValue, float progress, float edgeWidth, float3 edgeColor, float sharpness) {
    float dist = abs(dissolveValue - progress);
    float halfWidth = max(edgeWidth, 0.0001) * 0.5;
    
    // Fade in/out at extremes
    float fadeIn = smoothstep(0.0, halfWidth, progress);
    float fadeOut = 1.0 - smoothstep(1.0 - halfWidth, 1.0, progress);
    float masterIntensity = fadeIn * fadeOut;
    
    // Edge gradient
    float baseGradient = 1.0 - smoothstep(0.0, halfWidth, dist);
    float hardnessPower = lerp(1.0, 16.0, sharpness);
    float edgeGlow = pow(baseGradient, hardnessPower) * masterIntensity;
    
    float surfaceAlpha = step(progress, dissolveValue);
    
    return float4(edgeColor * edgeGlow, max(surfaceAlpha, edgeGlow));
}

// Smooth fade dissolve
float dissolveFade(float dissolveValue, float progress, float softness) {
    float halfWidth = softness * 0.5;
    return smoothstep(progress - halfWidth, progress + halfWidth, dissolveValue);
}

// === GLITTER/SPARKLE ===

// Hash functions for procedural glitter
float hash1D(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float2 hash2D(float2 p) {
    return frac(sin(float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)))) * 43758.5453);
}

// Procedural glitter
float3 glitter(float2 uv, float3 viewDir, float frequency, float threshold, 
               float size, float flickerSpeed, float brightness, float contrast, float3 tint, float time) {
    float2 gridUV = uv * frequency;
    float2 cellID = floor(gridUV);
    float2 cellUV = frac(gridUV);
    
    // Find closest voronoi point
    float minDist = 1.0;
    float2 closestID = cellID;
    
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float2 neighbor = float2(x, y);
            float2 pointPos = hash2D(cellID + neighbor);
            float dist = length(neighbor + pointPos - cellUV);
            if (dist < minDist) {
                minDist = dist;
                closestID = cellID + neighbor;
            }
        }
    }
    
    float flakeID = hash1D(closestID);
    if (flakeID < threshold) return float3(0.0);
    
    // Glitter mask
    float glitterMask = saturate((size - minDist) / max(fwidth(minDist), 0.001));
    if (glitterMask <= 0.0) return float3(0.0);
    
    // Animated sparkle
    float t = time * flickerSpeed + flakeID * 100.0;
    float3 glitterNormal = normalize(float3(sin(t * 1.2), cos(t * 1.7), sin(t * 1.5)));
    float sparkle = pow(saturate(dot(viewDir, glitterNormal)), contrast);
    
    return glitterMask * sparkle * brightness * tint;
}

#endif
