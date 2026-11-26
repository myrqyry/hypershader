// hypershader Standard Library - Distortion Functions
// UV manipulation and warping algorithms

#ifndef _STDLIB_DISTORTION_H_
#define _STDLIB_DISTORTION_H_

// --- Swirl/Twist Distortion ---
// Classic polar coordinate rotation that varies with distance
// center: effect origin, angle: max rotation in radians, radius: effect size
float2 uvSwirl(float2 uv, float2 center, float angle, float radius) {
    float2 delta = uv - center;
    float dist = length(delta);
    
    if (dist < radius) {
        // Rotation angle decreases as we approach the edge
        float ratio = (radius - dist) / radius;
        float theta = ratio * ratio * angle; // Quadratic falloff for smoothness
        
        float s = sin(theta);
        float c = cos(theta);
        delta = float2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);
    }
    
    return center + delta;
}

// --- Bulge/Pinch Distortion ---
// magnitude > 0 = bulge (magnify), < 0 = pinch (shrink)
float2 uvBulgePinch(float2 uv, float2 center, float radius, float magnitude) {
    float2 delta = uv - center;
    float dist = length(delta);
    
    if (dist < radius && dist > 0.0001) {
        float ratio = dist / radius;
        
        // Bulge: push pixels outward from center
        // Pinch: pull pixels toward center
        float distortion;
        if (magnitude > 0.0) {
            // Bulge - smooth outward push
            distortion = pow(ratio, 1.0 + magnitude);
        } else {
            // Pinch - smooth inward pull  
            distortion = pow(ratio, 1.0 / (1.0 - magnitude));
        }
        
        delta = normalize(delta) * distortion * radius;
    }
    
    return center + delta;
}

// --- Wave Distortion ---
// Sinusoidal displacement along an axis
float2 uvWave(float2 uv, float frequency, float amplitude, float phase, bool horizontal) {
    float offset = sin(((horizontal ? uv.y : uv.x) * frequency) + phase) * amplitude;
    
    if (horizontal) {
        return uv + float2(offset, 0.0);
    } else {
        return uv + float2(0.0, offset);
    }
}

// --- Ripple Distortion ---
// Circular waves emanating from a point
float2 uvRipple(float2 uv, float2 center, float frequency, float amplitude, float phase) {
    float2 delta = uv - center;
    float dist = length(delta);
    
    if (dist > 0.0001) {
        float wave = sin(dist * frequency - phase) * amplitude;
        float2 dir = normalize(delta);
        return uv + dir * wave;
    }
    
    return uv;
}

// --- Barrel/Pincushion Distortion ---
// Lens distortion simulation (k > 0 = barrel, k < 0 = pincushion)
float2 uvBarrelDistort(float2 uv, float2 center, float k) {
    float2 delta = uv - center;
    float r2 = dot(delta, delta);
    float distortion = 1.0 + k * r2;
    return center + delta * distortion;
}

// --- Chromatic Barrel Distortion ---
// Returns three UVs for R, G, B channels with different distortion
void uvChromaticBarrel(float2 uv, float2 center, float k, out float2 uvR, out float2 uvG, out float2 uvB) {
    uvR = uvBarrelDistort(uv, center, k * 1.0);
    uvG = uvBarrelDistort(uv, center, k * 0.5);
    uvB = uvBarrelDistort(uv, center, k * 0.0);
}

// --- Drunk/Wobble Effect ---
// Grid-based displacement with smooth interpolation
float2 uvDrunk(float2 uv, float time, float speed, float strength, float2 texelSize) {
    const float GRID = 5.0;
    
    // Grid cell and fractional position
    float2 gridPos = uv * GRID;
    float2 cell = floor(gridPos);
    float2 frac_pos = frac(gridPos);
    
    // Smooth interpolation factor
    float2 blend = smoothstep(0.0, 1.0, frac_pos);
    
    // Generate displacement at grid corners using hash
    float2 tl = float2(
        sin(time * speed + cell.x * 1.3 + cell.y * 2.1) * strength,
        cos(time * speed + cell.x * 2.7 + cell.y * 1.5) * strength
    );
    float2 tr = float2(
        sin(time * speed + (cell.x + 1.0) * 1.3 + cell.y * 2.1) * strength,
        cos(time * speed + (cell.x + 1.0) * 2.7 + cell.y * 1.5) * strength
    );
    float2 bl = float2(
        sin(time * speed + cell.x * 1.3 + (cell.y + 1.0) * 2.1) * strength,
        cos(time * speed + cell.x * 2.7 + (cell.y + 1.0) * 1.5) * strength
    );
    float2 br = float2(
        sin(time * speed + (cell.x + 1.0) * 1.3 + (cell.y + 1.0) * 2.1) * strength,
        cos(time * speed + (cell.x + 1.0) * 2.7 + (cell.y + 1.0) * 1.5) * strength
    );
    
    // Bilinear interpolation
    float2 top = lerp(tl, tr, blend.x);
    float2 bot = lerp(bl, br, blend.x);
    float2 offset = lerp(top, bot, blend.y);
    
    return uv + offset * texelSize * 100.0;
}

// --- Kaleidoscope ---
// Mirror/fold UV coordinates to create symmetry
float2 uvKaleidoscope(float2 uv, float2 center, int segments) {
    float2 delta = uv - center;
    float angle = atan2(delta.y, delta.x);
    float radius = length(delta);
    
    // Segment angle
    float segmentAngle = TAU / float(segments);
    
    // Fold into first segment
    angle = mod(angle, segmentAngle);
    
    // Mirror every other segment
    if (mod(floor(atan2(delta.y, delta.x) / segmentAngle), 2.0) > 0.5) {
        angle = segmentAngle - angle;
    }
    
    return center + float2(cos(angle), sin(angle)) * radius;
}

// --- Tile/Mirror ---
// Repeat UV with optional mirroring
float2 uvTile(float2 uv, float2 tiles, bool mirror) {
    float2 result = frac(uv * tiles);
    
    if (mirror) {
        float2 cell = floor(uv * tiles);
        // Mirror on odd cells
        if (mod(cell.x, 2.0) > 0.5) result.x = 1.0 - result.x;
        if (mod(cell.y, 2.0) > 0.5) result.y = 1.0 - result.y;
    }
    
    return result;
}

// --- Pixelate UV ---
// Snap UV to grid for retro effect
float2 uvPixelate(float2 uv, float2 resolution, float pixelSize) {
    float2 pixels = resolution / pixelSize;
    return floor(uv * pixels) / pixels;
}

// --- CRT Screen Curvature ---
// Simulate curved CRT monitor
float2 uvCRTCurve(float2 uv, float curvature) {
    float2 centered = uv * 2.0 - 1.0;
    float2 curved = centered * (1.0 + curvature * dot(centered, centered));
    return curved * 0.5 + 0.5;
}

#endif
