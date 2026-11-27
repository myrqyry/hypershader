// hypershader Standard Library - Lighting Utilities
// PBR and toon lighting helper functions

#ifndef _STDLIB_LIGHTING_H_
#define _STDLIB_LIGHTING_H_

// === DATA STRUCTURES ===

struct Light {
    float3 direction;
    float3 color;
    float intensity;
};

// === COMMON DOT PRODUCTS ===

// Clamped dot product
float dotClamped(float3 a, float3 b) {
    return max(dot(a, b), 0.0);
}

// Half-Lambert (wrap lighting)
float halfLambert(float NdotL) {
    return NdotL * 0.5 + 0.5;
}

// === SPHERICAL HARMONICS ===

// Nonlinear SH evaluation (from Mochie/Poiyomi)
// Better quality ambient lighting
float shNonlinear(float L0, float3 L1, float3 normal) {
    float R0 = L0;
    float3 R1 = 0.5 * L1;
    float lenR1 = length(R1);
    float q = dot(normalize(R1), normal) * 0.5 + 0.5;
    q = max(0.0, q);
    float p = 1.0 + 2.0 * lenR1 / R0;
    float a = (1.0 - lenR1 / R0) / (1.0 + lenR1 / R0);
    return R0 * (a + (1.0 - a) * (p + 1.0) * pow(q, p));
}

// Get SH length (ambient intensity)
float3 shLength(float4 SHA_R, float4 SHA_G, float4 SHA_B,
                float4 SHB_R, float4 SHB_G, float4 SHB_B) {
    float3 x = float3(length(SHA_R), length(SHA_G), length(SHA_B));
    float3 x1 = float3(length(SHB_R), length(SHB_G), length(SHB_B));
    return x + x1;
}

// === LIGHT ATTENUATION ===

// Smooth distance attenuation
float distanceAttenuation(float distance, float range) {
    float d = distance / range;
    float d2 = d * d;
    return saturate(1.0 - d2 * d2) / (d2 + 1.0);
}

// Point light attenuation
float pointLightAttenuation(float3 lightPos, float3 worldPos, float range) {
    float dist = distance(lightPos, worldPos);
    return distanceAttenuation(dist, range);
}

// Spotlight attenuation with cone falloff
float spotlightAttenuation(float3 lightDir, float3 spotDir, float innerAngle, float outerAngle) {
    float cosAngle = dot(-lightDir, spotDir);
    float cosInner = cos(innerAngle);
    float cosOuter = cos(outerAngle);
    return saturate((cosAngle - cosOuter) / (cosInner - cosOuter));
}

// === SHADOW UTILITIES ===

// Soft shadow falloff
float shadowFalloff(float shadowDistance, float fadeStart, float fadeEnd) {
    return 1.0 - saturate((shadowDistance - fadeStart) / (fadeEnd - fadeStart));
}

// === PBR BRDF FUNCTIONS ===

// GGX/Trowbridge-Reitz Normal Distribution Function
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith GGX Geometry Function (single direction)
float G1_Smith(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith GGX Geometry Function (combined)
float G_Smith(float NdotV, float NdotL, float roughness) {
    return G1_Smith(NdotV, roughness) * G1_Smith(NdotL, roughness);
}

// Fresnel-Schlick
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Fresnel-Schlick with roughness (for IBL)
float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// Complete Cook-Torrance BRDF
float3 CookTorrance(float NdotH, float NdotV, float NdotL, float LdotH,
                    float roughness, float3 F0) {
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(LdotH, F0);
    
    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    
    return numerator / denominator;
}

// === TOON/ANIME LIGHTING ===

// Hard edge toon shading
float toonStep(float NdotL, float threshold, float smoothness) {
    return smoothstep(threshold - smoothness, threshold + smoothness, NdotL);
}

// Multi-step toon shading
float toonSteps(float NdotL, int steps) {
    return floor(NdotL * float(steps)) / float(steps - 1);
}

// Ramp-based toon shading (sample gradient texture)
float3 toonRamp(sampler2D rampTex, float NdotL) {
    return texture(rampTex, float2(NdotL, 0.5)).rgb;
}

// === ENERGY CONSERVATION ===

// Diffuse energy conservation (metallic workflow)
float3 diffuseEnergyConservation(float3 albedo, float3 F0, float metallic) {
    float3 kS = F0; // Specular contribution
    float3 kD = (1.0 - kS) * (1.0 - metallic); // Diffuse contribution
    return albedo * kD;
}

// F0 from metallic
float3 computeF0(float3 albedo, float metallic) {
    return lerp(float3(0.04), albedo, metallic);
}

// === SPECULAR ANTIALIASING ===

// Reduce specular aliasing based on geometric roughness
float specularAA(float3 normal, float roughness) {
    float3 dndu = ddx(normal);
    float3 dndv = ddy(normal);
    float variance = max(dot(dndu, dndu), dot(dndv, dndv));
    float kernelRoughness = min(2.0 * variance, 0.18);
    return saturate(roughness + kernelRoughness);
}

// === ANISOTROPIC ===

// Anisotropic GGX
float D_GGX_Anisotropic(float NdotH, float TdotH, float BdotH, float anisotropy, float roughness) {
    float aspect = sqrt(1.0 - 0.9 * anisotropy);
    float ax = roughness / aspect;
    float ay = roughness * aspect;
    
    float d = TdotH * TdotH / (ax * ax) + BdotH * BdotH / (ay * ay) + NdotH * NdotH;
    return 1.0 / (PI * ax * ay * d * d);
}

// === SUBSURFACE APPROXIMATION ===

// Quick subsurface scattering approximation
float3 subsurfaceApprox(float3 lightDir, float3 viewDir, float3 normal, 
                         float3 subsurfaceColor, float thickness, float distortion, float power) {
    float3 scatterDir = normalize(lightDir + normal * distortion);
    float VdotS = saturate(dot(viewDir, -scatterDir));
    float scatter = pow(VdotS, power) * thickness;
    return subsurfaceColor * scatter;
}

// === UNIFIED LIGHTING CALCULATION ===

// Comprehensive lighting calculation for a single light source
float3 calculateLighting(float3 p, float3 normal, float3 viewDir, Material mat, Light light) {
    float3 lightDir = light.direction;
    float3 halfDir = normalize(viewDir + lightDir);

    float NdotL = saturate(dot(normal, lightDir));
    float NdotV = saturate(dot(normal, viewDir));
    float NdotH = saturate(dot(normal, halfDir));
    float LdotH = saturate(dot(lightDir, halfDir));

    // 1. Emissive component
    float3 color = mat.emissive;

    // 2. Diffuse component (Lambertian with energy conservation)
    float3 F0 = computeF0(mat.albedo, mat.metallic);
    float3 diffuse = diffuseEnergyConservation(mat.albedo, F0, mat.metallic) * NdotL;

    // 3. Specular component (Cook-Torrance)
    float3 specular = CookTorrance(NdotH, NdotV, NdotL, LdotH, mat.roughness, F0) * NdotL;

    // Combine and apply light properties
    color += (diffuse + specular) * light.color * light.intensity;

    return color;
}

#endif
