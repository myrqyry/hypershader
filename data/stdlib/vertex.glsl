// hypershader Standard Library - Vertex Effects
// Vertex manipulation and distortion techniques

#ifndef _STDLIB_VERTEX_H_
#define _STDLIB_VERTEX_H_

// === VERTEX DISTORTION MODES ===

// Wave distortion
float3 vertexWave(float3 vertex, float3 frequency, float3 speed, float3 strength, float time) {
    float3 offset;
    offset.x = sin(vertex.y * frequency.x + time * speed.x) * strength.x;
    offset.y = sin(vertex.x * frequency.y + time * speed.y) * strength.y;
    offset.z = sin(vertex.x * frequency.z + time * speed.z) * strength.z;
    return vertex + offset;
}

// Jumble/chaos distortion
float3 vertexJumble(float3 vertex, float3 frequency, float3 strength, float speed, float time) {
    float3 offset;
    offset.x = sin(vertex.x * frequency.x) * cos(vertex.y * frequency.x) * strength.x;
    offset.y = cos(vertex.y * frequency.y) * sin(vertex.z * frequency.y) * strength.y;
    offset.z = sin(vertex.z * frequency.z) * cos(vertex.x * frequency.z) * strength.z;
    return vertex + offset * sin(time * speed);
}

// Breathing/pulsing effect
float3 vertexBreathing(float3 vertex, float3 normal, float strength, float speed, float time) {
    float breath = (sin(time * speed) + 1.0) * 0.5;
    return vertex + normal * breath * strength;
}

// Wind simulation (requires noise texture in practice, this is simplified)
float3 vertexWind(float3 vertex, float3 worldPos, float3 windDir, 
                  float strength, float speed, float scale, float time) {
    // Simplified procedural wind
    float noise = sin(worldPos.x * scale + time * speed) * sin(worldPos.z * scale + time * speed * 0.7);
    float sineWave = sin(time * speed + worldPos.x + worldPos.z);
    float3 offset = normalize(windDir) * (noise + sineWave) * strength;
    return vertex + offset;
}

// Vertex color masked distortion
float3 vertexMaskedDistortion(float3 vertex, float3 offset, float4 vertexColor, int maskChannel) {
    float mask = 1.0;
    switch (maskChannel) {
        case 1: mask = vertexColor.r; break;
        case 2: mask = vertexColor.g; break;
        case 3: mask = vertexColor.b; break;
        case 4: mask = (vertexColor.r + vertexColor.g + vertexColor.b) / 3.0; break;
    }
    return vertex + offset * mask;
}

// === PS1-STYLE VERTEX EFFECTS ===

// Clip-space vertex snapping (PS1 jitter)
float4 ps1VertexSnap(float4 clipPos, float precision) {
    clipPos.xy /= clipPos.w;
    clipPos.xy = round(clipPos.xy * precision) / precision;
    clipPos.xy *= clipPos.w;
    return clipPos;
}

// World-space vertex snapping
float3 ps1WorldSnap(float3 worldPos, float precision) {
    return ceil(worldPos * precision) / precision;
}

// === BILLBOARD ===

// Camera-facing billboard (full rotation)
float3 billboard(float3 vertex, float3 objectCenter, float3 cameraPos) {
    float3 forward = normalize(cameraPos - objectCenter);
    float3 up = float3(0.0, 1.0, 0.0);
    float3 right = normalize(cross(up, forward));
    up = cross(forward, right);
    
    return objectCenter + right * vertex.x + up * vertex.y;
}

// Cylindrical billboard (Y-axis locked)
float3 billboardCylindrical(float3 vertex, float3 objectCenter, float3 cameraPos) {
    float3 forward = normalize(float3(cameraPos.x - objectCenter.x, 0.0, cameraPos.z - objectCenter.z));
    float3 right = cross(float3(0.0, 1.0, 0.0), forward);
    
    return objectCenter + right * vertex.x + float3(0.0, 1.0, 0.0) * vertex.y;
}

// === FLATTEN / 2D MODE ===

// Flatten model to face camera (like Lyuma's waifu2d)
float3 flattenModel(float3 vertex, float3 objectCenter, float3 cameraPos, 
                    float facing, float depthCorrection, float amount) {
    float facingAngle = facing * -PI * 0.5;
    float s = sin(facingAngle);
    float c = cos(facingAngle);
    float3 targetForward = float3(s, 0.0, c);
    
    // Billboard towards camera
    float3 billboardForward = normalize(cameraPos - objectCenter);
    float3 billboardUp = float3(0.0, 1.0, 0.0);
    float3 billboardRight = normalize(cross(billboardUp, billboardForward));
    billboardUp = cross(billboardForward, billboardRight);
    
    float3 flatPos = objectCenter;
    flatPos += billboardRight * vertex.x;
    flatPos += billboardUp * vertex.y;
    flatPos -= billboardForward * vertex.z * depthCorrection;
    
    // Original world position
    // (Would need object to world matrix in real implementation)
    float3 originalPos = vertex; // Placeholder
    
    return lerp(originalPos, flatPos, amount);
}

// === EXTRUSION ===

// Extrude along normal
float3 extrudeNormal(float3 vertex, float3 normal, float amount) {
    return vertex + normal * amount;
}

// Extrude with mask (for outlines)
float3 extrudeMasked(float3 vertex, float3 normal, float amount, float mask) {
    return vertex + normal * amount * mask;
}

// === SCALE EFFECTS ===

// Scale from pivot
float3 scaleFromPivot(float3 vertex, float3 pivot, float3 scale) {
    return (vertex - pivot) * scale + pivot;
}

// Animated scale pulse
float3 scalePulse(float3 vertex, float3 pivot, float minScale, float maxScale, float speed, float time) {
    float t = (sin(time * speed) + 1.0) * 0.5;
    float scale = lerp(minScale, maxScale, t);
    return (vertex - pivot) * scale + pivot;
}

// === ROTATION ===

// Rotate around Y axis
float3 rotateY(float3 vertex, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float3(
        vertex.x * c + vertex.z * s,
        vertex.y,
        -vertex.x * s + vertex.z * c
    );
}

// Rotate around arbitrary axis
float3 rotateAxis(float3 vertex, float3 axis, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    float t = 1.0 - c;
    
    float3x3 rot = float3x3(
        t * axis.x * axis.x + c,           t * axis.x * axis.y - s * axis.z, t * axis.x * axis.z + s * axis.y,
        t * axis.x * axis.y + s * axis.z,  t * axis.y * axis.y + c,          t * axis.y * axis.z - s * axis.x,
        t * axis.x * axis.z - s * axis.y,  t * axis.y * axis.z + s * axis.x, t * axis.z * axis.z + c
    );
    
    return mul(rot, vertex);
}

// Animated rotation
float3 rotateAnimated(float3 vertex, float3 pivot, float3 axis, float speed, float time) {
    float3 centered = vertex - pivot;
    float angle = time * speed;
    centered = rotateAxis(centered, normalize(axis), angle);
    return centered + pivot;
}

#endif
