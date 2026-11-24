// HyperShader Standard Library - Pathing & Animation Effects
// Animated path/flow effects for textures

#ifndef _STDLIB_PATHING_H_
#define _STDLIB_PATHING_H_

// === PATH ANIMATION MODES ===

// Fill mode: progressive reveal
float pathFill(float pathValue, float time, float speed, float offset) {
    float pathTime = frac(time * speed + offset);
    return step(pathValue, pathTime);
}

// Path mode: traveling line
float pathLine(float pathValue, float time, float speed, float offset, float width) {
    float pathTime = frac(time * speed + offset);
    return 1.0 - saturate(abs(pathTime - pathValue) / width);
}

// Loop mode: seamless looping path
float pathLoop(float pathValue, float time, float speed, float offset, float width) {
    float pathTime = frac(time * speed + offset);
    float dist = abs(pathTime - pathValue);
    dist = min(dist, 1.0 - dist); // Wrap around
    return 1.0 - saturate(dist / width);
}

// Ping-pong mode: bouncing path
float pathPingPong(float pathValue, float time, float speed, float offset, float width) {
    float pathTime = 1.0 - abs(1.0 - 2.0 * frac(time * speed + offset)); // 0->1->0
    return 1.0 - saturate(abs(pathTime - pathValue) / width);
}

// Trail mode: fading trail behind path
float pathTrail(float pathValue, float time, float speed, float offset, float width) {
    float pathTime = frac(time * speed + offset);
    float trailDist = pathTime - pathValue;
    return smoothstep(0.0, width, trailDist) - smoothstep(width, width + 0.001, trailDist);
}

// Converge mode: paths meet in middle
float pathConverge(float pathValue, float time, float speed, float offset, float width) {
    float pathTime = frac(time * speed + offset);
    float convergeTime = abs(1.0 - 2.0 * pathTime); // 1->0->1
    float convergeDist = abs(convergeTime - abs(1.0 - 2.0 * pathValue));
    return 1.0 - saturate(convergeDist / width);
}

// === PATHING SYSTEM ===

// Complete pathing effect
// pathValue: 0-1 from gradient/texture
// mode: 0=fill, 1=path, 2=loop, 3=pingpong, 4=trail, 5=converge
float calculatePath(float pathValue, float time, float speed, float offset, 
                    float width, float softness, int mode, float startPos, float endPos) {
    float alpha = 0.0;
    
    // Remap time to start/end range
    float pathTime = frac(time * speed + offset);
    pathTime = lerp(startPos, endPos, pathTime);
    
    switch (mode) {
        case 0: // Fill
            alpha = step(pathValue, pathTime);
            break;
        case 1: // Path
            alpha = 1.0 - saturate(abs(pathTime - pathValue) / width);
            break;
        case 2: // Loop
            float loopDist = abs(pathTime - pathValue);
            loopDist = min(loopDist, 1.0 - loopDist);
            alpha = 1.0 - saturate(loopDist / width);
            break;
        case 3: // Ping-pong
            float ppTime = 1.0 - abs(1.0 - 2.0 * frac(time * speed + offset));
            ppTime = lerp(startPos, endPos, ppTime);
            alpha = 1.0 - saturate(abs(ppTime - pathValue) / width);
            break;
        case 4: // Trail
            float trailDist = pathTime - pathValue;
            alpha = smoothstep(0.0, width, trailDist) - smoothstep(width, width + 0.001, trailDist);
            break;
        case 5: // Converge
            float convTime = abs(1.0 - 2.0 * pathTime);
            float convDist = abs(convTime - abs(1.0 - 2.0 * pathValue));
            alpha = 1.0 - saturate(convDist / width);
            break;
    }
    
    return smoothstep(0.0, softness, alpha);
}

// === CIRCUIT BOARD / TECH LINES ===

// Generate tech/circuit line pattern
float circuitPattern(float2 uv, float scale, float lineWidth, float time, float speed) {
    float2 gridUV = uv * scale;
    float2 cellUV = frac(gridUV);
    float2 cellID = floor(gridUV);
    
    // Random direction per cell
    float rnd = frac(sin(dot(cellID, float2(12.9898, 78.233))) * 43758.5453);
    
    float line = 0.0;
    
    // Horizontal or vertical line based on random
    if (rnd > 0.5) {
        line = abs(cellUV.y - 0.5) < lineWidth ? 1.0 : 0.0;
    } else {
        line = abs(cellUV.x - 0.5) < lineWidth ? 1.0 : 0.0;
    }
    
    // Animate the circuit
    float pathProgress = frac(time * speed + rnd);
    float animatedLine = line * smoothstep(0.0, 0.5, abs(cellUV.x - pathProgress));
    
    return animatedLine;
}

// === FLOW MAP ===

// Sample with flow map distortion
float2 flowUV(float2 uv, float2 flowDir, float time, float speed, float strength) {
    float phase0 = frac(time * speed);
    float phase1 = frac(time * speed + 0.5);
    
    float2 uv0 = uv + flowDir * phase0 * strength;
    float2 uv1 = uv + flowDir * phase1 * strength;
    
    // Blend factor for seamless loop
    float blend = abs(phase0 * 2.0 - 1.0);
    
    return lerp(uv0, uv1, blend);
}

// Animated caustics (dual-layer flow)
float2 causticsUV(float2 uv, float time, float speed, float scale) {
    float2 uv1 = uv * scale + float2(time * speed * 0.3, time * speed * 0.2);
    float2 uv2 = uv * scale * 1.4 + float2(-time * speed * 0.25, time * speed * 0.15);
    
    return uv1 + uv2 * 0.5;
}

#endif
