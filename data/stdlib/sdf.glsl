// hypershader Standard Library - Signed Distance Fields

#ifndef _STDLIB_SDF_H_
#define _STDLIB_SDF_H_

// === PRIMITIVE SHAPES ===

float sdSphere(float3 p, float s) {
    return length(p) - s;
}

float sdBox(float3 p, float3 b) {
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdTorus(float3 p, float2 t) {
    float2 q = float2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

// Cylinder (infinite along Y axis)
float sdCylinder(float3 p, float2 rh) {
    float2 d = abs(float2(length(p.xz), p.y)) - rh;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// Capped cylinder
float sdCappedCylinder(float3 p, float h, float r) {
    float2 d = abs(float2(length(p.xz), p.y)) - float2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// Cone (infinite along Y axis)
float sdCone(float3 p, float2 c) {
    // c must be normalized, c.x = radius at base, c.y = height
    float q = length(p.xz);
    return dot(c, float2(q, p.y));
}

// Capped cone
float sdCappedCone(float3 p, float h, float r1, float r2) {
    float2 q = float2(length(p.xz), p.y);
    float2 k1 = float2(r2, h);
    float2 k2 = float2(r2 - r1, 2.0 * h);
    float2 ca = float2(q.x - min(q.x, (q.y < 0.0) ? r1 : r2), abs(q.y) - h);
    float2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot(k2, k2), 0.0, 1.0);
    float s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
    return s * sqrt(min(dot(ca, ca), dot(cb, cb)));
}

// Capsule (cylinder with rounded ends)
float sdCapsule(float3 p, float3 a, float3 b, float r) {
    float3 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

// Rounded box
float sdRoundBox(float3 p, float3 b, float r) {
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

// Octahedron
float sdOctahedron(float3 p, float s) {
    p = abs(p);
    float m = p.x + p.y + p.z - s;
    float3 q;
    if (3.0 * p.x < m) q = p.xyz;
    else if (3.0 * p.y < m) q = p.yzx;
    else if (3.0 * p.z < m) q = p.zxy;
    else return m * 0.57735027;

    float k = clamp(0.5 * (q.z - q.y + s), 0.0, s);
    return length(float3(q.x, q.y - s + k, q.z - k));
}

// Pyramid
float sdPyramid(float3 p, float h) {
    float m2 = h * h + 0.25;

    p.xz = abs(p.xz);
    p.xz = (p.z > p.x) ? p.zx : p.xz;
    p.xz -= 0.5;

    float3 q = float3(p.z, h * p.y - 0.5 * p.x, h * p.x + 0.5 * p.y);

    float s = max(-q.x, 0.0);
    float t = clamp((q.y - 0.5 * p.z) / (m2 + 0.25), 0.0, 1.0);

    float a = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
    float b = m2 * (q.x + 0.5 * t) * (q.x + 0.5 * t) + (q.y - m2 * t) * (q.y - m2 * t);

    float d2 = min(q.y, -q.x * m2 - q.y * 0.5) > 0.0 ? 0.0 : min(a, b);

    return sqrt((d2 + q.z * q.z) / m2) * sign(max(q.z, -p.y));
}

// Triangular prism
float sdTriPrism(float3 p, float2 h) {
    float3 q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025 + p.y * 0.5, -p.y) - h.x * 0.5);
}

// Hexagonal prism
float sdHexPrism(float3 p, float2 h) {
    const float3 k = float3(-0.8660254, 0.5, 0.57735);
    p = abs(p);
    p.xy -= 2.0 * min(dot(k.xy, p.xy), 0.0) * k.xy;
    float2 d = float2(
        length(p.xy - float2(clamp(p.x, -k.z * h.x, k.z * h.x), h.x)) * sign(p.y - h.x),
        p.z - h.y
    );
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// === OPERATIONS ===

float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return lerp(b, a, h) - k * h * (1.0 - h);
}

// Smooth union (standard name for smin in many shaders)
float opSmoothUnion(float a, float b, float k) {
    return smin(a, b, k);
}

// Smooth maximum
float smax(float a, float b, float k) {
    return -smin(-a, -b, k);
}

// Polynomial smooth minimum
float sminPoly(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return lerp(b, a, h) - k * h * h * (3.0 - 2.0 * h);
}

// Exponential smooth minimum
float sminExp(float a, float b, float k) {
    float res = exp(-k * a) + exp(-k * b);
    return -log(res) / k;
}

// Power smooth minimum
float sminPower(float a, float b, float k, float n) {
    a = pow(a, n);
    b = pow(b, n);
    return pow((a * b) / (a + b), 1.0 / n);
}

// Chamfer operations
float opChamfer(float a, float b, float r) {
    return min(min(a, b), (a + b - r) * 0.7071067811865476);
}

float opRound(float a, float b, float r) {
    float2 u = max(float2(r - a, r - b), float2(0.0));
    return max(r, min(a, b)) - length(u);
}

// === TRANSFORMATIONS ===

// Rotation around X axis
float3 opRotateX(float3 p, float a) {
    float c = cos(a), s = sin(a);
    return float3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}

// Rotation around Y axis
float3 opRotateY(float3 p, float a) {
    float c = cos(a), s = sin(a);
    return float3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

// Rotation around Z axis
float3 opRotateZ(float3 p, float a) {
    float c = cos(a), s = sin(a);
    return float3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

// Translation
float3 opTranslate(float3 p, float3 t) {
    return p - t;
}

// Scale
float3 opScale(float3 p, float s) {
    return p / s;
}

// === COMPLEX SHAPES ===

// Link (two spheres connected by cylinder)
float sdLink(float3 p, float le, float r1, float r2) {
    float3 q = float3(p.x, max(abs(p.y) - le, 0.0), p.z);
    return length(float2(length(q.xy) - r1, q.z)) - r2;
}

// Cross (three cylinders intersecting)
float sdCross(float3 p, float3 b) {
    float da = sdBox(p, b);
    float db = sdBox(opRotateZ(p, 0.7853981633974483), b);
    float dc = sdBox(opRotateY(p, 0.7853981633974483), b);
    return min(da, min(db, dc));
}

// Heart shape
float sdHeart(float3 p) {
    p.y -= 0.5;
    float2 q = float2(length(p.xz), p.y);
    float d = abs(q.x * q.x + q.y * q.y - 1.0);
    return d - 0.1;
}

// Gear (parametric)
float sdGear(float3 p, float r, int teeth, float thickness) {
    float2 q = float2(length(p.xz) - r, p.y);
    float angle = atan2(p.x, p.z);
    float tooth_angle = 2.0 * PI / float(teeth);
    float local_angle = mod(angle + tooth_angle * 0.5, tooth_angle) - tooth_angle * 0.5;
    float tooth_radius = r + 0.1 * cos(local_angle * 5.0);
    return length(float2(length(p.xz) - tooth_radius, abs(p.y))) - thickness;
}

// === BONE/CURVE BASED ===

// Hermite curve distance
float sdHermite(float3 p, float3 a, float3 b, float3 c, float3 d, float thickness) {
    // Simplified implementation - full Hermite would be more complex
    float3 ab = b - a;
    float3 bc = c - b;
    float3 cd = d - c;

    float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    float3 pos = lerp(a, b, t);

    return length(p - pos) - thickness;
}

// Bezier curve distance (quadratic)
float sdBezier(float3 p, float3 a, float3 b, float3 c, float thickness) {
    // Find closest point on quadratic Bezier curve
    float2 uv = float2(0.0);
    float minDist = 1000.0;

    for (int i = 0; i < 16; i++) {
        float t = float(i) / 15.0;
        float3 point = lerp(lerp(a, b, t), lerp(b, c, t), t);
        float dist = length(p - point);
        if (dist < minDist) {
            minDist = dist;
            uv = float2(t, dist);
        }
    }

    return minDist - thickness;
}

// === RAYMARCHING HELPERS ===

// mapScene is supplied by the consuming effect. OBS effect files do not
// accept GLSL-style standalone function declarations here.

// Calculate normal using central differences
float3 calcNormal(float3 p) {
    const float eps = 0.001;
    const float2 h = float2(eps, 0.0);
    return normalize(float3(
        mapScene(p + h.xyy) - mapScene(p - h.xyy),
        mapScene(p + h.yxy) - mapScene(p - h.yxy),
        mapScene(p + h.yyx) - mapScene(p - h.yyx)
    ));
}

// Ambient Occlusion approximation
float calcAO(float3 pos, float3 nor) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; i++) {
        float h = 0.01 + 0.12 * float(i) / 4.0;
        float d = mapScene(pos + h * nor);
        occ += (h - d) * sca;
        sca *= 0.95;
    }
    return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}

#endif
