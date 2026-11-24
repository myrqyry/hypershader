// HyperShader Standard Library - UV & Mapping Utilities
// Advanced UV manipulation and texture mapping techniques

#ifndef _STDLIB_MAPPING_H_
#define _STDLIB_MAPPING_H_

// === UV MANIPULATION ===

// Rotate UV around center
float2 uvRotate(float2 uv, float angleDegrees) {
    float2 centered = uv - 0.5;
    float angle = angleDegrees * (PI / 180.0);
    float s = sin(angle);
    float c = cos(angle);
    // Inline rotation (float2x2 unsupported in OBS): mul([[c,-s],[s,c]], centered)
    float2 rotated = float2(c * centered.x - s * centered.y,
                            s * centered.x + c * centered.y);
    return rotated + 0.5;
}

// Scale UV from center
float2 uvScale(float2 uv, float2 scale) {
    return (uv - 0.5) * scale + 0.5;
}

// Offset and scroll UV
float2 uvOffset(float2 uv, float2 offset, float2 scroll, float time) {
    return uv + offset + scroll * time;
}

// Combined UV manipulation
float2 uvManipulate(float2 uv, float rotation, float2 scale, float2 offset, float2 scroll, float time) {
    float2 result = uv;
    
    if (rotation != 0.0) {
        result = uvRotate(result, rotation);
    }
    
    result = result * scale + offset;
    result += scroll * time;
    
    return result;
}

// === TRIPLANAR MAPPING ===

// Calculate triplanar weights
float3 triplanarWeights(float3 normal, float sharpness) {
    float3 weights = pow(abs(normal), float3(sharpness));
    return weights / (weights.x + weights.y + weights.z);
}

// Get triplanar UVs
void triplanarUVs(float3 worldPos, float3 position, float scale, float3 rotation,
                  out float2 uvX, out float2 uvY, out float2 uvZ) {
    // Apply rotation (simplified euler rotation)
    float3 angles = rotation * (PI / 180.0);
    float3 s = sin(angles);
    float3 c = cos(angles);
    
    float3x3 rotX = float3x3(1, 0, 0, 0, c.x, -s.x, 0, s.x, c.x);
    float3x3 rotY = float3x3(c.y, 0, s.y, 0, 1, 0, -s.y, 0, c.y);
    float3x3 rotZ = float3x3(c.z, -s.z, 0, s.z, c.z, 0, 0, 0, 1);
    
    float3 localPos = mul(rotZ, mul(rotY, mul(rotX, worldPos - position)));
    
    uvX = localPos.yz / scale;
    uvY = localPos.xz / scale;
    uvZ = localPos.xy / scale;
}

// Sample texture with triplanar mapping
float4 sampleTriplanar(sampler2D tex, float3 worldPos, float3 normal,
                        float3 position, float scale, float3 rotation, float sharpness) {
    float2 uvX, uvY, uvZ;
    triplanarUVs(worldPos, position, scale, rotation, uvX, uvY, uvZ);
    
    float3 weights = triplanarWeights(normal, sharpness);
    
    float4 sampleX = texture(tex, uvX) * weights.x;
    float4 sampleY = texture(tex, uvY) * weights.y;
    float4 sampleZ = texture(tex, uvZ) * weights.z;
    
    return sampleX + sampleY + sampleZ;
}

// === FLIPBOOK / SPRITE SHEET ===

// Calculate flipbook UV for current frame
float2 flipbookUV(float2 uv, float columns, float rows, float frame) {
    float totalFrames = columns * rows;
    float currentFrame = floor(mod(frame, totalFrames));
    
    float col = mod(currentFrame, columns);
    float row = floor(currentFrame / columns);
    
    // Flip row for bottom-to-top ordering
    row = (rows - 1.0) - row;
    
    float2 cellSize = 1.0 / float2(columns, rows);
    return uv * cellSize + float2(col, row) * cellSize;
}

// Animated flipbook UV
float2 flipbookAnimated(float2 uv, float columns, float rows, float fps, float time, float scrub) {
    float frame = floor(frac(fps * time + scrub) * columns * rows);
    return flipbookUV(uv, columns, rows, frame);
}

// === FLOW MAP ===

// Sample with flow map distortion (seamless loop)
float2 flowMapUV(float2 uv, float2 flowDir, float time, float speed, float strength) {
    float phase0 = frac(time * speed);
    float phase1 = frac(time * speed + 0.5);
    
    float2 uv0 = uv + flowDir * phase0 * strength;
    float2 uv1 = uv + flowDir * phase1 * strength;
    
    // Blend weight for seamless transition
    float blend = abs(phase0 * 2.0 - 1.0);
    
    return lerp(uv0, uv1, blend);
}

// Get flow direction from flow map texture
float2 decodeFlowMap(float4 flowSample, float strength) {
    return (flowSample.rg * 2.0 - 1.0) * strength;
}

// === SCREEN-SPACE UV ===

// Convert screen position to UV
float2 screenToUV(float4 screenPos) {
    return screenPos.xy / screenPos.w;
}

// Screen-space tiled UV
float2 screenTiledUV(float4 screenPos, float2 tiling) {
    return frac(screenToUV(screenPos) * tiling);
}

// === POLAR COORDINATES ===

// Cartesian to polar UV
float2 cartesianToPolar(float2 uv, float2 center) {
    float2 centered = uv - center;
    float angle = atan2(centered.y, centered.x) / TAU + 0.5;
    float radius = length(centered) * 2.0;
    return float2(angle, radius);
}

// Polar to cartesian UV
float2 polarToCartesian(float2 polar, float2 center) {
    float angle = (polar.x - 0.5) * TAU;
    float radius = polar.y * 0.5;
    return center + float2(cos(angle), sin(angle)) * radius;
}

// === DECAL PROJECTION ===

// Apply decal in UV space
float2 decalUV(float2 baseUV, float2 position, float2 scale, float rotation) {
    float2 localUV = baseUV - position;
    
    // Rotate
    float angle = -rotation * (PI / 180.0);
    float s = sin(angle);
    float c = cos(angle);
    // Inline rotation (float2x2 unsupported in OBS): mul([[c,-s],[s,c]], localUV)
    localUV = float2(c * localUV.x - s * localUV.y,
                     s * localUV.x + c * localUV.y);
    
    // Scale and center
    localUV = localUV / max(scale, 0.0001) + 0.5;
    
    return localUV;
}

// Check if UV is valid for decal (within 0-1 range)
bool isValidDecalUV(float2 uv) {
    return all(uv >= 0.0) && all(uv <= 1.0);
}

// === PARALLAX OFFSET ===

// Simple parallax offset
float2 parallaxSimple(float2 uv, float3 viewDirTangent, float height, float strength) {
    return uv - viewDirTangent.xy * (height * strength);
}

// === TILE & WRAP ===

// Mirror wrap UV (seamless tiling)
float2 uvMirror(float2 uv) {
    float2 t = floor(uv);
    float2 f = frac(uv);
    
    // Mirror on odd tiles
    float2 mirror = mod(t, 2.0);
    return lerp(f, 1.0 - f, mirror);
}

// Clamp UV to edge (no repeat)
float2 uvClampEdge(float2 uv) {
    return clamp(uv, 0.001, 0.999);
}

#endif
